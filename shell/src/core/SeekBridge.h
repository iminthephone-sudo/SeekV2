#pragma once

// C++ side of the SEEK bridge contract (docs/BRIDGE_CONTRACT.md).
//
// The shell owns the Python process lifecycle: it launches exactly one
//   python python-backend/seek_cpp_bridge.py --no-qt
// writes one JSON request per line to stdin, and reads SEEK_JSON: /
// SEEK_PROGRESS: packets from stdout. stderr is diagnostics only. There is no
// other path from C++ to Python.

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <functional>

struct BridgeError {
    QString code;
    QString message;
    bool isError() const { return !code.isEmpty(); }
};

class SeekBridge : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Ready, Failed };
    Q_ENUM(State)

    using Callback = std::function<void(const QJsonValue& result, const BridgeError& error)>;

    explicit SeekBridge(QObject* parent = nullptr);
    ~SeekBridge() override;

    void start();
    void stop();
    void restart();

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    QJsonObject status() const { return m_status; }
    QString pythonPath() const { return m_python; }
    QString backendDir() const { return m_backendDir; }
    QString lastError() const { return m_lastError; }
    QStringList diagnostics() const { return m_diagnostics; }

    // Sends a request. `context` guards the callback: if that QObject is
    // destroyed before the reply arrives, the callback is skipped.
    QString call(const QString& method, const QJsonObject& params, Callback callback, QObject* context = nullptr);
    QString call(const QString& method, Callback callback, QObject* context = nullptr) {
        return call(method, QJsonObject(), std::move(callback), context);
    }

    static QString locateBackend();
    static QString locatePython(const QString& backendDir);

signals:
    void stateChanged(SeekBridge::State state);
    void ready(const QJsonObject& status);
    void progress(const QString& requestId, int percent, const QString& message);
    void diagnostic(const QString& line);
    void busyChanged(bool busy);

private:
    struct Pending {
        QString method;
        Callback callback;
        QPointer<QObject> context;
        bool guarded = false;
    };

    void setState(State state);
    void onStdout();
    void onStderr();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void handlePacket(const QByteArray& line);
    void failAll(const QString& code, const QString& message);
    void writeRequest(const QByteArray& line);

    QProcess* m_process = nullptr;
    State m_state = State::Stopped;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QHash<QString, Pending> m_pending;
    QList<QByteArray> m_queue;  // requests sent before the engine said "ready"
    QJsonObject m_status;
    QString m_python;
    QString m_backendDir;
    QString m_lastError;
    QStringList m_diagnostics;
    quint64 m_nextId = 1;
    int m_autoRestarts = 0;
    bool m_stopping = false;
};
