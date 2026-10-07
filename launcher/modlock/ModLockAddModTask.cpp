// SPDX-License-Identifier: GPL-3.0-only
#include "ModLockAddModTask.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "Application.h"
#include "ModLockBridge.h"
#include "net/ApiDownload.h"
#include "net/NetJob.h"

ModLockAddModTask::ModLockAddModTask(QString gameRoot, QJsonObject mod, QJsonArray targets, QObject* parent)
    : Task(false), m_gameRoot(QDir(std::move(gameRoot)).absolutePath()), m_mod(std::move(mod)), m_targets(std::move(targets))
{
    setParent(parent);
}

void ModLockAddModTask::executeTask()
{
    const QString stagingRoot = QDir(m_gameRoot).filePath(QStringLiteral(".modlock/staging"));
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_stageDirectory = QDir(stagingRoot).filePath(requestId);
    m_stagedPath = QDir(m_stageDirectory).filePath(QStringLiteral("download.bin"));
    if (m_gameRoot.isEmpty() || m_targets.isEmpty() || m_mod.value("url").toString().isEmpty() ||
        m_mod.value("filename").toString().isEmpty() || !QDir().mkpath(m_stageDirectory)) {
        cleanupStage();
        emitFailed(tr("Could not prepare a ModLock staging download."));
        return;
    }

    setAbortable(true);
    setStatus(tr("Staging mod for ModLock"));
    m_download = new NetJob(tr("ModLock mod staging"), APPLICATION->network(), 1);
    auto download = Net::ApiDownload::makeFile(QUrl(m_mod.value("url").toString()), m_stagedPath);
    m_download->addNetAction(download);
    connect(m_download, &Task::succeeded, this, &ModLockAddModTask::onDownloadSucceeded);
    connect(m_download, &Task::failed, this, &ModLockAddModTask::onDownloadFailed);
    connect(m_download, &Task::aborted, this, [this] {
        cleanupStage();
        setAbortable(false);
        emitAborted();
    });
    m_download->start();
}

bool ModLockAddModTask::abort()
{
    if (m_bridge && m_bridge->isActive()) {
        m_cancelRequested = m_bridge->cancel();
        return m_cancelRequested;
    }
    if (m_download && m_download->isRunning())
        return m_download->abort();
    cleanupStage();
    setAbortable(false);
    emitAborted();
    return true;
}

void ModLockAddModTask::onDownloadSucceeded()
{
    m_download = nullptr;
    QFile file(m_stagedPath);
    if (!file.open(QIODevice::ReadOnly)) {
        onDownloadFailed(file.errorString());
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            onDownloadFailed(file.errorString());
            return;
        }
        hash.addData(chunk);
    }
    file.close();
    m_mod.insert("sha256", QString::fromLatin1(hash.result().toHex()));
    const QString relativePath = QDir(m_gameRoot).relativeFilePath(m_stagedPath).replace('\\', '/');
    setStatus(tr("Adding mod to ModLock desired state"));
    m_bridge = new ModLockBridge(m_gameRoot, this);
    connect(m_bridge, &ModLockBridge::progress, this, [this](const QString&, const QString& message) { setDetails(message); });
    connect(m_bridge, &ModLockBridge::failed, this, [this](const QString&, const QJsonObject& error) {
        m_bridgeFailed = true;
        m_bridgeError = error.value("message").toString(tr("ModLock could not add this mod."));
        m_cancelRequested = error.value("code").toString() == "cancelled";
    });
    connect(m_bridge, &ModLockBridge::finished, this, &ModLockAddModTask::onBridgeFinished);
    const QJsonObject params{{"mod", m_mod}, {"targets", m_targets}, {"staged_file", relativePath}};
    if (!m_bridge->start("add-mod", params)) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
        onDownloadFailed(tr("Could not start the ModLock component."));
    }
}

void ModLockAddModTask::onDownloadFailed(const QString& message)
{
    if (m_download) {
        m_download->deleteLater();
        m_download = nullptr;
    }
    cleanupStage();
    setAbortable(false);
    emitFailed(message);
}

void ModLockAddModTask::onBridgeFinished()
{
    if (m_bridge) {
        m_bridge->deleteLater();
        m_bridge = nullptr;
    }
    cleanupStage();
    setAbortable(false);
    if (m_bridgeFailed && m_cancelRequested)
        emitAborted();
    else if (m_bridgeFailed)
        emitFailed(m_bridgeError.isEmpty() ? tr("ModLock could not add this mod.") : m_bridgeError);
    else
        emitSucceeded();
    m_bridgeFailed = false;
    m_bridgeError.clear();
    m_cancelRequested = false;
}

void ModLockAddModTask::cleanupStage()
{
    if (!m_stagedPath.isEmpty())
        QFile::remove(m_stagedPath);
    if (!m_stageDirectory.isEmpty())
        QDir().rmdir(m_stageDirectory);
}
