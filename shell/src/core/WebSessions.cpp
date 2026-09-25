#include "core/WebSessions.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#ifdef SEEK_HAS_WEBENGINE
#include <QNetworkCookie>
#include <QWebEngineCookieStore>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#endif

struct WebSessions::Private {
#ifdef SEEK_HAS_WEBENGINE
    QHash<QString, QWebEngineProfile*> profiles;
    QHash<QString, QList<QNetworkCookie>> cookies;  // per profile, kept current from the cookie store
#endif
};

WebSessions::WebSessions(QObject* parent) : QObject(parent), d(new Private) {}

WebSessions::~WebSessions() { delete d; }

WebSessions* WebSessions::instance() {
    static WebSessions* inst = new WebSessions(qApp);
    return inst;
}

bool WebSessions::available() {
#ifdef SEEK_HAS_WEBENGINE
    return true;
#else
    return false;
#endif
}

QList<WebSessions::Site> WebSessions::sites() {
    return {
        {QStringLiteral("linkedin"), QStringLiteral("LinkedIn"), QUrl(QStringLiteral("https://www.linkedin.com/login")),
         QUrl(QStringLiteral("https://www.linkedin.com/feed/")), QStringLiteral("linkedin.com")},
        {QStringLiteral("indeed"), QStringLiteral("Indeed"), QUrl(QStringLiteral("https://secure.indeed.com/auth")),
         QUrl(QStringLiteral("https://profile.indeed.com/")), QStringLiteral("indeed.com")},
    };
}

WebSessions::Site WebSessions::site(const QString& key) {
    for (const Site& s : sites())
        if (s.key == key) return s;
    return {};
}

QString WebSessions::statusFromUrl(const QString& siteKey, const QUrl& url) {
    const QString host = url.host().toLower();
    const QString path = url.path().toLower();
    if (siteKey == "linkedin") {
        if (!host.endsWith("linkedin.com")) return {};
        static const QRegularExpression out(QStringLiteral("^/(login|uas|checkpoint|authwall|signup|m/login)"));
        static const QRegularExpression in(QStringLiteral("^/(feed|jobs|in/|mynetwork|messaging|notifications)"));
        if (out.match(path).hasMatch()) return QStringLiteral("signed_out");
        if (in.match(path).hasMatch()) return QStringLiteral("signed_in");
        return {};
    }
    if (siteKey == "indeed") {
        if (!host.endsWith("indeed.com")) return {};
        if (host == "secure.indeed.com" || path.contains("/auth") || path.contains("/account/login"))
            return QStringLiteral("signed_out");
        return QStringLiteral("signed_in");
    }
    return {};
}

#ifdef SEEK_HAS_WEBENGINE

QWebEngineProfile* WebSessions::profile(const QString& seekProfileId) {
    const QString key = seekProfileId.isEmpty() ? QStringLiteral("shared") : seekProfileId;
    if (QWebEngineProfile* p = d->profiles.value(key)) return p;
    // Ids are [A-Za-z0-9_-] (the engine enforces it), so they are safe as a storage folder name.
    auto* p = new QWebEngineProfile(QStringLiteral("seek-web-") + key, this);
    // The default user agent advertises "QtWebEngine/6.x", which bot filters treat as automation.
    p->setHttpUserAgent(p->httpUserAgent().remove(QRegularExpression(QStringLiteral("QtWebEngine/\\S+\\s*"))));
    p->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    auto* store = p->cookieStore();
    connect(store, &QWebEngineCookieStore::cookieAdded, this, [this, key](const QNetworkCookie& c) {
        auto& list = d->cookies[key];
        for (const QNetworkCookie& existing : std::as_const(list))
            if (existing.hasSameIdentifier(c)) return;
        list.append(c);
    });
    connect(store, &QWebEngineCookieStore::cookieRemoved, this, [this, key](const QNetworkCookie& c) {
        auto& list = d->cookies[key];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const QNetworkCookie& x) { return x.hasSameIdentifier(c); }),
                   list.end());
    });
    store->loadAllCookies();
    d->profiles.insert(key, p);
    return p;
}

void WebSessions::check(const QString& seekProfileId, const QString& siteKey, QObject* context,
                        std::function<void(const QString&)> done) {
    const Site s = site(siteKey);
    if (s.key.isEmpty()) return done(QStringLiteral("unknown"));
    // A page with no view loads headless; it only needs to follow the redirects.
    auto* page = new QWebEnginePage(profile(seekProfileId), this);
    auto* settle = new QTimer(page);
    settle->setSingleShot(true);
    settle->setInterval(2000);  // wait for script redirects after the load finishes
    auto* giveUp = new QTimer(page);
    giveUp->setSingleShot(true);
    giveUp->setInterval(30000);
    QPointer<QObject> guard(context);
    auto finish = [page, guard, done, siteKey](const QString& forced) {
        if (page->property("seekDone").toBool()) return;
        page->setProperty("seekDone", true);
        QString status = forced;
        if (status.isEmpty() && page->property("seekHttpError").toBool()) status = QStringLiteral("unknown");
        if (status.isEmpty()) {
            status = statusFromUrl(siteKey, page->url());
            // A bot check or error page says nothing about the sign-in.
            static const QRegularExpression challenge(QStringLiteral("just a moment|attention required|security check|"
                                                                     "verify you are human|access denied"),
                                                      QRegularExpression::CaseInsensitiveOption);
            if (challenge.match(page->title()).hasMatch() || status.isEmpty()) status = QStringLiteral("unknown");
        }
        page->deleteLater();
        if (guard) done(status);
    };
    // An error page (HTTP 4xx/5xx, LinkedIn's 999, no connection) says nothing about the sign-in,
    // even when its URL looks like a signed-in page.
    connect(page, &QWebEnginePage::loadingChanged, page, [page](const QWebEngineLoadingInfo& info) {
        if (info.status() == QWebEngineLoadingInfo::LoadStartedStatus) page->setProperty("seekHttpError", false);
        const bool httpError = info.errorDomain() == QWebEngineLoadingInfo::HttpStatusCodeDomain && info.errorCode() >= 400;
        if (info.status() == QWebEngineLoadingInfo::LoadFailedStatus || httpError) page->setProperty("seekHttpError", true);
    });
    connect(page, &QWebEnginePage::loadFinished, page, [settle](bool) { settle->start(); });
    connect(settle, &QTimer::timeout, page, [finish] { finish({}); });
    connect(giveUp, &QTimer::timeout, page, [finish] { finish(QStringLiteral("unknown")); });
    giveUp->start();
    page->load(s.check);
}

void WebSessions::signOut(const QString& seekProfileId, const QString& siteKey) {
    const Site s = site(siteKey);
    QWebEngineProfile* p = profile(seekProfileId);
    const QString key = seekProfileId.isEmpty() ? QStringLiteral("shared") : seekProfileId;
    const QList<QNetworkCookie> list = d->cookies.value(key);
    for (const QNetworkCookie& c : list) {
        const QString domain = c.domain().toLower();
        if (domain == s.domain || domain.endsWith("." + s.domain)) p->cookieStore()->deleteCookie(c);
    }
}

void WebSessions::forget(const QString& seekProfileId) {
    if (seekProfileId.isEmpty()) return;
    QWebEngineProfile* p = profile(seekProfileId);
    p->cookieStore()->deleteAllCookies();
    p->clearHttpCache();
    p->clearAllVisitedLinks();
}

#else  // built without Qt WebEngine

QWebEngineProfile* WebSessions::profile(const QString&) { return nullptr; }
void WebSessions::check(const QString&, const QString&, QObject* context, std::function<void(const QString&)> done) {
    QPointer<QObject> guard(context);
    QTimer::singleShot(0, this, [guard, done] {
        if (guard) done(QStringLiteral("unknown"));
    });
}
void WebSessions::signOut(const QString&, const QString&) {}
void WebSessions::forget(const QString&) {}

#endif
