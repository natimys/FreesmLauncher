// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockBridge.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace {
constexpr qsizetype MaxEventBytes = 1024 * 1024;
}

ModLockBridge::ModLockBridge(QString root, QObject* parent) : QObject(parent), m_root(std::move(root))
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::started, this, &ModLockBridge::sendRequest);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ModLockBridge::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ModLockBridge::readStandardError);
    connect(&m_process, &QProcess::finished, this, &ModLockBridge::processFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            fail("component_unavailable", m_process.errorString());
            m_finishedEmitted = true;
            emit finished();
        }
    });
}

bool ModLockBridge::start(const QString& operation, const QJsonObject& params)
{
    if (m_started || operation.trimmed().isEmpty() || m_root.isEmpty())
        return false;

    m_started = true;
    m_operation = operation;
    m_params = params;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const QString executable = QCoreApplication::applicationDirPath() + "/modlock.exe";
    m_process.start(executable, { "bridge", "--protocol", QString::number(Protocol), "--root", m_root }, QIODevice::ReadWrite);
    return true;
}

bool ModLockBridge::startCompatibilityCheck()
{
    return start("capabilities");
}

bool ModLockBridge::cancel()
{
    if (!m_process.isOpen() || m_terminalEventReceived || m_process.state() == QProcess::NotRunning)
        return false;

    const QJsonObject request{{"type", "cancel"}, {"id", m_requestId}};
    return m_process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n') != -1;
}

bool ModLockBridge::isActive() const
{
    return m_started && !m_finishedEmitted;
}

void ModLockBridge::sendRequest()
{
    const QJsonObject request{{"type", "request"}, {"id", m_requestId}, {"operation", m_operation}, {"params", m_params}};
    if (m_process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n') == -1)
        fail("process", m_process.errorString());
}

void ModLockBridge::readStandardOutput()
{
    m_outputBuffer.append(m_process.readAllStandardOutput());
    if (m_outputBuffer.size() > MaxEventBytes && !m_outputBuffer.contains('\n')) {
        cancel();
        fail("invalid_response", tr("ModLock sent an oversized protocol event."));
        m_outputBuffer.clear();
        return;
    }

    qsizetype newline = -1;
    while ((newline = m_outputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_outputBuffer.left(newline);
        m_outputBuffer.remove(0, newline + 1);
        if (line.size() > MaxEventBytes) {
            cancel();
            fail("invalid_response", tr("ModLock sent an oversized protocol event."));
            return;
        }
        handleLine(line);
    }
}

void ModLockBridge::readStandardError()
{
    m_errorBuffer.append(m_process.readAllStandardError());
    qsizetype newline = -1;
    while ((newline = m_errorBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_errorBuffer.left(newline);
        if (line.endsWith('\r'))
            line.chop(1);
        emit diagnostic(QString::fromUtf8(line));
        m_errorBuffer.remove(0, newline + 1);
    }
    if (m_errorBuffer.size() > MaxEventBytes) {
        emit diagnostic(QString::fromUtf8(m_errorBuffer.left(MaxEventBytes)));
        m_errorBuffer.clear();
    }
}

void ModLockBridge::handleLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        cancel();
        fail("invalid_response", tr("ModLock returned invalid JSON: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject event = document.object();
    if (event.value("protocol").toInt(-1) != Protocol || event.value("id").toString() != m_requestId) {
        cancel();
        fail("unsupported_protocol", tr("The ModLock component uses an incompatible bridge protocol."));
        return;
    }

    const QString type = event.value("type").toString();
    if (type == "progress") {
        emit progress(m_requestId, event.value("message").toString());
        return;
    }
    if (type != "result" || m_terminalEventReceived) {
        cancel();
        fail("invalid_response", tr("ModLock returned an unexpected protocol event."));
        return;
    }

    m_terminalEventReceived = true;
    const auto error = event.value("error");
    if (error.isObject()) {
        emit failed(m_requestId, error.toObject());
    } else if (event.value("result").isObject()) {
        const QJsonObject result = event.value("result").toObject();
        if (m_operation == "capabilities" &&
            (result.value("protocol").toInt(-1) != Protocol || result.value("loader_protocol").toInt(-1) != Protocol)) {
            emit failed(m_requestId,
                        {{"code", "unsupported_protocol"},
                         {"message", tr("The bundled ModLock component is incompatible with this launcher.")}});
        } else {
            emit completed(m_requestId, result);
        }
    } else {
        emit failed(m_requestId, {{"code", "invalid_response"}, {"message", tr("ModLock returned an empty result.")}});
    }
}

void ModLockBridge::fail(QString code, QString message)
{
    if (m_terminalEventReceived)
        return;
    m_terminalEventReceived = true;
    emit failed(m_requestId, {{"code", std::move(code)}, {"message", std::move(message)}});
}

void ModLockBridge::processFinished(int exitCode, QProcess::ExitStatus status)
{
    readStandardOutput();
    readStandardError();
    if (!m_errorBuffer.isEmpty()) {
        emit diagnostic(QString::fromUtf8(m_errorBuffer));
        m_errorBuffer.clear();
    }
    if (!m_outputBuffer.isEmpty() && !m_terminalEventReceived) {
        handleLine(m_outputBuffer);
        m_outputBuffer.clear();
    }
    if (!m_terminalEventReceived) {
        fail("process", status == QProcess::CrashExit ? tr("The ModLock component stopped unexpectedly.")
                                                       : tr("The ModLock component exited with code %1.").arg(exitCode));
    }
    if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished();
    }
}
