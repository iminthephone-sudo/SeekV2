#pragma once

// Built-in browser sessions, one per SEEK profile.
//
// Staff sign in to LinkedIn or Indeed on the site's own page inside SEEK's browser
// (Qt WebEngine). SEEK never sees or stores the password: the site's sign-in cookie
// lives in that profile's browser storage on this computer, and imports and job
// searches for that profile run with it. Each SEEK profile gets its own storage, so
// participants' accounts never mix. Without Qt WebEngine, available() is false and
// the rest is a no-op.

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
        QString domain;  // cookie domain, for per-site sign-out
    };

    static WebSessions* instance();
    ~WebSessions() override;
    static bool available();
    static QList<Site> sites();
    static Site site(const QString& key);
    // "signed_in" / "signed_out" when a URL shows it (after a redirect), "" when it doesn't tell.
    static QString statusFromUrl(const QString& siteKey, const QUrl& url);

    // Browser storage for a SEEK profile (shared storage when the id is empty). nullptr without WebEngine.
    QWebEngineProfile* profile(const QString& seekProfileId);
    // Ask the site whether this profile is signed in; done("signed_in" | "signed_out" | "unknown").
    void check(const QString& seekProfileId, const QString& siteKey, QObject* context,
               std::function<void(const QString&)> done);
    // Remove the site's cookies from this profile's storage.
    void signOut(const QString& seekProfileId, const QString& siteKey);
    // Wipe a deleted profile's cookies and cache.
    void forget(const QString& seekProfileId);

private:
    explicit WebSessions(QObject* parent = nullptr);
    struct Private;
    Private* d;
};
