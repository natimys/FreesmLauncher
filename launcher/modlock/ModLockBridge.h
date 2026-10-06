// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>

class ModLockBridge final : public QObject {
    Q_OBJECT

   public:
    static constexpr int Protocol = 1;

    explicit ModLockBridge(QString root, QObject* parent = nullptr);

    bool start(const QString& operation, const QJsonObject& params = {});
    bool startCompatibilityCheck();
    bool cancel();
    bool isActive() const;

   signals:
    void progress(const QString& requestId, const QString& message);
    void completed(const QString& requestId, const QJsonObject& result);
    void failed(const QString& requestId, const QJsonObject& error);
    void diagnostic(const QString& message);
    void finished();

   private:
    void sendRequest();
    void readStandardOutput();
    void readStandardError();
    void handleLine(const QByteArray& line);
    void fail(QString code, QString message);
    void processFinished(int exitCode, QProcess::ExitStatus status);

    QString m_root;
    QString m_requestId;
    QString m_operation;
    QJsonObject m_params;
    QProcess m_process;
    QByteArray m_outputBuffer;
    QByteArray m_errorBuffer;
    bool m_started = false;
    bool m_terminalEventReceived = false;
    bool m_finishedEmitted = false;
};
