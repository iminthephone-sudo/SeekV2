// SEEK — job-readiness toolkit for justice outreach.
//
// Runtime contract (see Agent-memory-Canon.md and docs/ARCHITECTURE.md):
//   * This C++ shell owns every window. Python never opens UI.
//   * The shell launches exactly one engine process:
//       python python-backend/seek_cpp_bridge.py --no-qt
//     and talks to it only through the SEEK bridge contract.
//   * If the engine can't start, the UI still opens and explains why.

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QStyleFactory>
#include <QTimer>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("SEEK"));
    // Screenshot runs keep their own settings so they never touch a real user's preferences.
    QApplication::setApplicationName(qEnvironmentVariableIsSet("SEEK_AUTOSHOT") ? QStringLiteral("SEEK-autoshot")
                                                                                : QStringLiteral("SEEK"));
    QApplication::setApplicationVersion(QStringLiteral(SEEK_VERSION));
    // Fusion renders stylesheets consistently on every platform.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    Theme::instance()->apply();
    app.setWindowIcon(Theme::instance()->appIcon());

    AppContext ctx;
    MainWindow window(&ctx);
    window.show();
    ctx.bridge()->start();

    // Developer aid: SEEK_AUTOSHOT=<folder> saves a PNG of every page (light and
    // dark) once the engine is ready, then exits. Used for docs and UI checks.
    const QString shotDir = qEnvironmentVariable("SEEK_AUTOSHOT");
    if (!shotDir.isEmpty()) {
        QDir().mkpath(shotDir);
        window.resize(1400, 900);
        QObject::connect(ctx.bridge(), &SeekBridge::ready, &window, [&window, &ctx, shotDir] {
            const QStringList pages = {"home", "profiles", "resume", "jobs", "optimize", "letters", "interview", "history", "settings"};
            auto* step = new int(0);
            auto* timer = new QTimer(&window);
            QObject::connect(timer, &QTimer::timeout, &window, [=, &window, &ctx]() mutable {
                const int n = *step;
                const int total = int(pages.size()) * 2;
                if (n > 0) {
                    const int prev = n - 1;
                    const QString mode = prev < pages.size() ? "light" : "dark";
                    window.grab().save(QDir(shotDir).filePath(QStringLiteral("%1-%2.png").arg(pages.at(prev % pages.size()), mode)));
                }
                if (n >= total) {
                    timer->stop();
                    delete step;
                    QApplication::quit();
                    return;
                }
                if (n == 0) Theme::instance()->setMode(Theme::Mode::Light);
                if (n == pages.size()) Theme::instance()->setMode(Theme::Mode::Dark);
                ctx.navigate(pages.at(n % pages.size()), {{"run", true}, {"latest", true}});
                ++*step;
            });
            timer->start(1500);
        });
    }

    const int code = app.exec();
    ctx.bridge()->stop();
    return code;
}
