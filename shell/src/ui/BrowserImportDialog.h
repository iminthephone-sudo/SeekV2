#pragma once

// Built-in browser for importing postings from sites that refuse non-browser clients (Indeed's
// Cloudflare check, for one). The page loads in Qt WebEngine — a real Chromium — so the check
// passes on its own or with one "I'm human" tick, and the rendered HTML goes to the engine's
// `job.from_html`. Needs Qt WebEngine at build time; without it available() is false and the
// Jobs page falls back to "Paste description".

#include <QDialog>
#include <QUrl>

class QLabel;
class QPushButton;
class QTimer;
class QWebEngineView;

class BrowserImportDialog : public QDialog {
    Q_OBJECT
public:
    static bool available();
    explicit BrowserImportDialog(const QUrl& url, QWidget* parent = nullptr);

signals:
    // Emitted once, just before the dialog accepts.
    void pageCaptured(const QString& html, const QUrl& url);

private:
    void checkForPosting();
    void capture();

    QWebEngineView* m_view = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_import = nullptr;
    QTimer* m_poll = nullptr;
    bool m_captured = false;
};
