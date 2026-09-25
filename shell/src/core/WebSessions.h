#pragma once

// Built-in browser sessions, one per SEEK profile and job site.
//
// Staff sign in to LinkedIn or Indeed on the site's own page inside SEEK's browser
// (Qt WebEngine). SEEK never sees or stores the password: the site's sign-in cookie
// lives in browser storage on this computer, and imports and job searches for that
// profile run with it. Storage is separate per SEEK profile *and* per site
// ("seek-web-<profile>-linkedin", "-indeed", "-web" for everything else), so
// participants' accounts never mix, and signing out of one site wipes exactly that
// site's storage — including cookies saved in earlier sessions, which the cookie
// store never reports back. Without Qt WebEngine, available() is false and the rest
// is a no-op.

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QWebEngineProfile;

class WebSessions : public QObject {
    Q_OBJECT
public:
    struct Site {
        QString key;     // "linkedin" | "indeed"
        QString name;    // shown to staff
        QUrl signIn;     // the site's own sign-in page
        QUrl check;      // a page that redirects to sign-in when signed out
        QString domain;  // registrable domain, to pick the storage for a URL
    };

    static WebSessions* instance();
    // Destroys every browser page and profile. Call once after the main window is gone and before
    // QApplication is destroyed (Qt WebEngine must be torn down in that order).
    static void shutdown();
    ~WebSessions() override;
    static bool available();
    static QList<Site> sites();
    static Site site(const QString& key);
    // "linkedin", "indeed" or "web": which storage a URL belongs to.
    static QString siteFor(const QUrl& url);
    // "signed_in" / "signed_out" when a URL shows it (after a redirect), "" when it doesn't tell.
    static QString statusFromUrl(const QString& siteKey, const QUrl& url);

    // Browser storage for a SEEK profile and site (shared storage when the id is empty). nullptr without WebEngine.
    QWebEngineProfile* profile(const QString& seekProfileId, const QString& siteKey);
    QWebEngineProfile* profileForUrl(const QString& seekProfileId, const QUrl& url) {
        return profile(seekProfileId, siteFor(url));
    }
    // Ask the site whether this profile is signed in; done("signed_in" | "signed_out" | "unknown").
    void check(const QString& seekProfileId, const QString& siteKey, QObject* context,
               std::function<void(const QString&)> done);
    // Sign out of one site: wipes that site's cookies and cache for this profile.
    void signOut(const QString& seekProfileId, const QString& siteKey);
    // A profile was deleted: wipe its cookies and cache now, and its storage folders at the next start.
    void forget(const QString& seekProfileId);

private:
    explicit WebSessions(QObject* parent = nullptr);
    struct Private;
    Private* d;
};
