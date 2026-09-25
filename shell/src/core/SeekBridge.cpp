#include "core/SeekBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr char kJsonPrefix[] = "SEEK_JSON:";
constexpr char kProgressPrefix[] = "SEEK_PROGRESS:";
constexpr char kBridgeScript[] = "seek_cpp_bridge.py";
constexpr int kMaxDiagnostics = 2000;
constexpr int kMaxAutoRestarts = 2;
}  // namespace

SeekBridge::SeekBridge(QObject* parent) : QObject(parent) {}

SeekBridge::~SeekBridge() {
    m_stopping = true;
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->write("{\"id\":\"bye\",\"method\":\"system.shutdown\"}\n");
            m_process->closeWriteChannel();
            if (!m_process->waitForFinished(1500)) m_process->kill();
        }
    }
}

QString SeekBridge::locateBackend() {
    QStringList candidates;
    const QString env = qEnvironmentVariable("SEEK_BACKEND_DIR");
    if (!env.isEmpty()) candidates << env;
    const QString configured = QSettings().value("engine/backendDir").toString();
    if (!configured.isEmpty()) candidates << configured;
    const QDir app(QCoreApplication::applicationDirPath());
    // Packaged layout first (python-backend next to the exe), then dev build trees.
    for (const char* rel : {"python-backend", "../python-backend", "../../python-backend",
                               "../../../python-backend", "../Resources/python-backend"})
        candidates << app.filePath(QString::fromLatin1(rel));
    candidates << QDir::current().filePath("python-backend");
#ifdef SEEK_BACKEND_SOURCE_DIR
    candidates << QStringLiteral(SEEK_BACKEND_SOURCE_DIR);
#endif
    for (const QString& dir : candidates) {
        if (QFileInfo::exists(QDir(dir).filePath(kBridgeScript))) return QDir(dir).canonicalPath();
    }
    return {};
}

QString SeekBridge::locatePython(const QString& backendDir) {
    const QString configured = QSettings().value("engine/python").toString();
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) return configured;
    const QString env = qEnvironmentVariable("SEEK_PYTHON");
    if (!env.isEmpty()) return env;
    // A project-local virtualenv wins over whatever is on PATH.
    const QDir backend(backendDir);
    for (const char* rel : {".venv/Scripts/python.exe", ".venv/bin/python3", ".venv/bin/python",
                               "../.venv/Scripts/python.exe", "../.venv/bin/python3"}) {
        const QString path = backend.filePath(QString::fromLatin1(rel));
        if (QFileInfo(path).isExecutable()) return QFileInfo(path).absoluteFilePath();
    }
#ifdef Q_OS_WIN
    const QStringList names = {"python", "py", "python3"};
#else
    const QStringList names = {"python3", "python"};
#endif
    for (const QString& name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        // Skip the Windows Store stub (WindowsApps\python.exe) which just opens the Store.
        if (!found.isEmpty() && !found.contains("WindowsApps", Qt::CaseInsensitive)) return found;
    }
    return {};
}

void SeekBridge::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(state);
}

void SeekBridge::start() {
    if (m_process && m_process->state() != QProcess::NotRunning) return;
    m_stopping = false;
    m_backendDir = locateBackend();
    if (m_backendDir.isEmpty()) {
        m_lastError = tr("Couldn't find the SEEK engine (python-backend/%1). Reinstall SEEK or set its folder in "
                         "Settings.").arg(kBridgeScript);
        emit diagnostic("[shell] " + m_lastError);
        setState(State::Failed);
        return;
    }
    m_python = locatePython(m_backendDir);
    if (m_python.isEmpty()) {
        m_lastError = tr("Python 3 was not found. Install Python 3.10+ and run: pip install -r "
                         "python-backend/requirements.txt");
        emit diagnostic("[shell] " + m_lastError);
        setState(State::Failed);
        return;
    }

    delete m_process;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_backendDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &SeekBridge::onStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &SeekBridge::onStderr);
    connect(m_process, &QProcess::finished, this, &SeekBridge::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            m_lastError = tr("Couldn't start Python at %1.").arg(m_python);
            emit diagnostic("[shell] " + m_lastError);
            failAll("engine_stopped", m_lastError);
            setState(State::Failed);
        }
    });

    QStringList args = {QDir(m_backendDir).filePath(kBridgeScript), "--no-qt"};
    const QString dataDir = QSettings().value("engine/dataDir").toString();
    if (!dataDir.isEmpty()) args << "--data-dir" << dataDir;
    QString program = m_python;
    if (QFileInfo(program).baseName() == "py") args.prepend("-3");  // Windows launcher
    emit diagnostic(QStringLiteral("[shell] launching %1 %2").arg(program, args.join(' ')));
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    setState(State::Starting);
    m_process->start(program, args);
}

void SeekBridge::stop() {
    m_stopping = true;
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        setState(State::Stopped);
        return;
    }
    m_process->write("{\"id\":\"bye\",\"method\":\"system.shutdown\"}\n");
    m_process->closeWriteChannel();
    QTimer::singleShot(2000, m_process, [p = QPointer<QProcess>(m_process)] {
        if (p && p->state() != QProcess::NotRunning) p->kill();
    });
}

void SeekBridge::restart() {
    m_autoRestarts = 0;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        connect(m_process, &QProcess::finished, this, [this] { QTimer::singleShot(0, this, &SeekBridge::start); },
                Qt::SingleShotConnection);
        stop();
    } else {
        start();
    }
}

QString SeekBridge::call(const QString& method, const QJsonObject& params, Callback callback, QObject* context) {
    const QString id = QString::number(m_nextId++);
    QJsonObject request{{"id", id}, {"method", method}, {"params", params}};
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    m_pending.insert(id, Pending{method, std::move(callback), context, context != nullptr});
    if (m_pending.size() == 1) emit busyChanged(true);

    if (m_state == State::Ready) {
        writeRequest(line);
    } else if (m_state == State::Starting) {
        m_queue.append(line);
    } else {
        // Not running: fail asynchronously so callers see a uniform callback path.
        const QString msg = m_lastError.isEmpty() ? tr("The SEEK engine is not running.") : m_lastError;
        QTimer::singleShot(0, this, [this, id, msg] {
            auto it = m_pending.find(id);
            if (it == m_pending.end()) return;
            Pending p = it.value();
            m_pending.erase(it);
            if (m_pending.isEmpty()) emit busyChanged(false);
            if (p.callback && (!p.guarded || p.context)) p.callback(QJsonValue(), {"engine_stopped", msg});
        });
    }
    return id;
}

void SeekBridge::writeRequest(const QByteArray& line) {
    if (m_process && m_process->state() == QProcess::Running) m_process->write(line);
}

void SeekBridge::onStdout() {
    m_stdoutBuffer += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(nl).trimmed();
        m_stdoutBuffer.remove(0, nl + 1);
        if (!line.isEmpty()) handlePacket(line);
    }
}

void SeekBridge::onStderr() {
    m_stderrBuffer += m_process->readAllStandardError();
    int nl;
    while ((nl = m_stderrBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stderrBuffer.left(nl)).trimmed();
        m_stderrBuffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        m_diagnostics.append(line);
        if (m_diagnostics.size() > kMaxDiagnostics) m_diagnostics.removeFirst();
        emit diagnostic(line);
    }
}

void SeekBridge::handlePacket(const QByteArray& line) {
    const bool isJson = line.startsWith(kJsonPrefix);
    const bool isProgress = line.startsWith(kProgressPrefix);
    if (!isJson && !isProgress) {
        emit diagnostic("[shell] ignored non-protocol stdout: " + QString::fromUtf8(line.left(200)));
        return;
    }
    const QByteArray body = line.mid(isJson ? int(sizeof(kJsonPrefix) - 1) : int(sizeof(kProgressPrefix) - 1));
    QJsonParseError err{};
    const QJsonObject packet = QJsonDocument::fromJson(body, &err).object();
    if (err.error != QJsonParseError::NoError) {
        emit diagnostic("[shell] bad packet: " + err.errorString());
        return;
    }
    const QString id = packet.value("id").toString();
    if (isProgress) {
        emit progress(id, packet.value("percent").toInt(), packet.value("message").toString());
        return;
    }
    if (packet.value("event").toString() == "ready") {
        m_status = packet.value("result").toObject();
        m_autoRestarts = 0;
        setState(State::Ready);
        emit ready(m_status);
        for (const QByteArray& queued : std::as_const(m_queue)) writeRequest(queued);
        m_queue.clear();
        return;
    }
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        if (!packet.value("ok").toBool())
            emit diagnostic("[engine] " + packet.value("error").toObject().value("message").toString());
        return;
    }
    Pending pending = it.value();
    m_pending.erase(it);
    if (m_pending.isEmpty()) emit busyChanged(false);
    if (!pending.callback || (pending.guarded && !pending.context)) return;
    if (packet.value("ok").toBool()) {
        pending.callback(packet.value("result"), {});
    } else {
        const QJsonObject e = packet.value("error").toObject();
        pending.callback(QJsonValue(), {e.value("code").toString("error"), e.value("message").toString()});
    }
}

void SeekBridge::failAll(const QString& code, const QString& message) {
    auto pending = m_pending;
    m_pending.clear();
    m_queue.clear();
    if (!pending.isEmpty()) emit busyChanged(false);
    for (const Pending& p : std::as_const(pending)) {
        if (p.callback && (!p.guarded || p.context)) p.callback(QJsonValue(), {code, message});
    }
}

void SeekBridge::onFinished(int exitCode, QProcess::ExitStatus status) {
    onStderr();
    const bool crashed = !m_stopping;
    emit diagnostic(QStringLiteral("[shell] engine exited (code %1, %2)")
                        .arg(exitCode)
                        .arg(status == QProcess::CrashExit ? "crash" : "normal"));
    if (crashed) {
        m_lastError = tr("The SEEK engine stopped unexpectedly. See Settings → Logs.");
        if (!m_diagnostics.isEmpty()) m_lastError += "\n" + m_diagnostics.last();
    }
    failAll("engine_stopped", m_lastError.isEmpty() ? tr("The SEEK engine stopped.") : m_lastError);
    if (crashed && m_state == State::Ready && m_autoRestarts < kMaxAutoRestarts) {
        ++m_autoRestarts;
        emit diagnostic("[shell] restarting engine automatically");
        setState(State::Stopped);
        QTimer::singleShot(500, this, &SeekBridge::start);
        return;
    }
    setState(crashed ? State::Failed : State::Stopped);
}
