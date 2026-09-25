#pragma once

// Built-in browser (Qt WebEngine — a real Chromium) for sites that refuse non-browser
// clients, such as Indeed's Cloudflare check. Pages load with the SEEK profile's own
// browser session (see WebSessions), so a LinkedIn/Indeed sign-in carries over.
//
//  * BrowserImportDialog — shows a page and captures its rendered HTML once the thing
//    we want (a job description, a results list) is on screen, or on "Import this page".
//  * SiteSignInDialog    — the site's own sign-in page; SEEK never sees the password.
//  * PageGrabber         — loads a page without a window and captures it (search results).
//
// Without Qt WebEngine at build time, WebSessions::available() is false and callers fall
// back to "Paste description".

#include <QDialog>
#include <QElapsedTimer>
#include <QUrl>

class QLabel;
class QPushButton;
class QTimer;
class QWebEnginePage;
class QWebEngineView;

class BrowserImportDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        QString detectJs;  // JS expression: true once the page has what we want
        QString intro;     // shown above the page
        QString action;    // capture button text
    };
    static Options postingOptions(const QString& host);
    static Options searchOptions(const QString& host);

    BrowserImportDialog(const QUrl& url, const QString& seekProfileId, const Options& options, QWidget* parent = nullptr);
    ~BrowserImportDialog() override;
    void done(int result) override;

signals:
    // Emitted once, just before the dialog accepts.
    void pageCaptured(const QString& html, const QUrl& url);

private:
    void checkPage();
    void capture();

    Options m_options;
    QWebEngineView* m_view = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_import = nullptr;
    QTimer* m_poll = nullptr;
    bool m_captured = false;
    // Set when the dialog is closing: runJavaScript callbacks still fire (with an invalid value) while the
    // page is torn down, and must do nothing then.
    bool m_closing = false;
};

class SiteSignInDialog : public QDialog {
    Q_OBJECT
public:
    SiteSignInDialog(const QString& siteKey, const QString& seekProfileId, QWidget* parent = nullptr);

signals:
    // "signed_in" | "signed_out" | "unknown", emitted once when the dialog closes after sign-in.
    void finishedWith(const QString& status);

private:
    void finish(const QString& status);

    QString m_site;
    QString m_profileId;
    QWebEngineView* m_view = nullptr;
    QLabel* m_status = nullptr;
    bool m_done = false;
    bool m_checking = false;
};

class PageGrabber : public QObject {
    Q_OBJECT
public:
    // Starts loading immediately; deletes itself after emitting captured() or failed().
    PageGrabber(const QUrl& url, const QString& seekProfileId, const QString& detectJs, int timeoutMs, QObject* parent);
    ~PageGrabber() override;

signals:
    void captured(const QString& html, const QUrl& url);
    void failed(const QString& reason);  // "challenge" (a human check is showing) or "timeout"

private:
    void poll();
    void done();

    QWebEnginePage* m_page = nullptr;
    QString m_detectJs;
    QTimer* m_poll = nullptr;
    QTimer* m_timeout = nullptr;
    QElapsedTimer m_started;
    bool m_finished = false;
};
