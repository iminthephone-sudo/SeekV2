#include "ui/BrowserImportDialog.h"

#include "core/WebSessions.h"

#include <QTimer>

#ifdef SEEK_HAS_WEBENGINE
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include "ui/Widgets.h"

namespace {
// The same description containers and JSON-LD the engine reads.
const char* kPostingJs = R"JS(
(() => !!document.querySelector('#jobDescriptionText, .show-more-less-html__markup, #job-details, .jobs-description__content, [data-automation-id=jobPostingDescription]')
    || [...document.querySelectorAll('script[type="application/ld+json"]')].some(s => s.textContent.includes('JobPosting')))()
)JS";
// A results list (LinkedIn cards, Indeed cards or Indeed's embedded JSON), or an explicit "no results".
const char* kSearchJs = R"JS(
(() => !!document.querySelector('a[data-jk], [data-entity-urn*="jobPosting"], .job-search-card')
    || [...document.scripts].some(s => s.textContent.includes('mosaic-provider-jobcards'))
    || (document.body && /did not match any jobs|no matching jobs/i.test(document.body.innerText)))()
)JS";
// Cloudflare and similar "are you human" pages.
const char* kChallengeJs = R"JS(
(() => /just a moment|attention required|security check|verify you are human|additional verification/i.test(document.title)
    || !!document.querySelector('#challenge-form, #challenge-stage, iframe[src*="challenges.cloudflare.com"]'))()
)JS";
}  // namespace

// -- BrowserImportDialog ----------------------------------------------------------------------------
BrowserImportDialog::Options BrowserImportDialog::postingOptions(const QString& host) {
    return {QString::fromUtf8(kPostingJs),
            tr("%1 only shows postings to web browsers, so SEEK opened it here. If a “Verify you are human” box "
               "appears, tick it. SEEK imports the posting as soon as the job description shows up, or click "
               "“Import this page”.").arg(host),
            tr("Import this page")};
}

BrowserImportDialog::Options BrowserImportDialog::searchOptions(const QString& host) {
    return {QString::fromUtf8(kSearchJs),
            tr("%1 asked to confirm a person is searching. If a “Verify you are human” box appears, tick it. SEEK "
               "reads the results as soon as they show up.").arg(host),
            tr("Use these results")};
}

BrowserImportDialog::BrowserImportDialog(const QUrl& url, const QString& seekProfileId, const Options& options,
                                         QWidget* parent)
    : QDialog(parent), m_options(options) {
    setWindowTitle(tr("SEEK browser — %1").arg(url.host()));
    resize(1100, 780);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(10);
    auto* intro = ui::label(options.intro, "Muted");
    intro->setWordWrap(true);
    col->addWidget(intro);

    m_view = new QWebEngineView;
    m_view->setPage(new QWebEnginePage(WebSessions::instance()->profileForUrl(seekProfileId, url), m_view));
    col->addWidget(m_view, 1);

    auto* row = new QHBoxLayout;
    m_status = ui::label(tr("Loading…"), "Muted");
    m_import = ui::button(options.action, "download", true);
    m_import->setEnabled(false);
    auto* cancel = ui::button(tr("Cancel"));
    row->addWidget(m_status, 1);
    row->addWidget(cancel);
    row->addWidget(m_import);
    col->addLayout(row);

    // Some pages render with JavaScript after the load finishes, and a challenge page reloads
    // into the real one, so keep checking while the dialog is open.
    m_poll = new QTimer(this);
    m_poll->setInterval(1500);
    connect(m_poll, &QTimer::timeout, this, &BrowserImportDialog::checkPage);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_import->setEnabled(true);
        m_status->setText(ok ? tr("Waiting for the page…") : tr("The page didn't finish loading."));
        checkPage();
        m_poll->start();
    });
    connect(m_import, &QPushButton::clicked, this, &BrowserImportDialog::capture);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_view->load(url);
}

BrowserImportDialog::~BrowserImportDialog() { m_closing = true; }

void BrowserImportDialog::done(int result) {
    m_closing = true;
    m_poll->stop();
    QDialog::done(result);
}

void BrowserImportDialog::checkPage() {
    if (m_captured || m_closing) return;
    m_view->page()->runJavaScript(m_options.detectJs, [this](const QVariant& found) {
        if (!m_closing && found.toBool()) capture();
    });
}

void BrowserImportDialog::capture() {
    if (m_captured || m_closing) return;
    m_captured = true;
    m_poll->stop();
    m_import->setEnabled(false);
    m_status->setText(tr("Reading the page…"));
    const QUrl url = m_view->url();
    // outerHTML rather than the downloaded source: it includes what the page's scripts rendered.
    m_view->page()->runJavaScript(QStringLiteral("document.documentElement.outerHTML"), [this, url](const QVariant& html) {
        if (m_closing || !html.isValid()) return;  // cancelled, or the page is being destroyed
        emit pageCaptured(html.toString(), url);
        accept();
    });
}

// -- SiteSignInDialog -------------------------------------------------------------------------------
SiteSignInDialog::SiteSignInDialog(const QString& siteKey, const QString& seekProfileId, QWidget* parent)
    : QDialog(parent), m_site(siteKey), m_profileId(seekProfileId) {
    const WebSessions::Site site = WebSessions::site(siteKey);
    setWindowTitle(tr("Sign in to %1").arg(site.name));
    resize(1000, 760);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 14, 16, 14);
    auto* intro = ui::label(tr("Sign in on %1's own page below. SEEK never sees or saves the password — %1 keeps "
                               "the participant signed in inside SEEK's browser for this profile only, on this computer. "
                               "Use “Sign out” on the profile when you're done on a shared computer.").arg(site.name),
                            "Muted");
    intro->setWordWrap(true);
    col->addWidget(intro);
    m_view = new QWebEngineView;
    m_view->setPage(new QWebEnginePage(WebSessions::instance()->profile(seekProfileId, siteKey), m_view));
    col->addWidget(m_view, 1);
    auto* row = new QHBoxLayout;
    m_status = ui::label(tr("Waiting for sign-in…"), "Muted");
    auto* cancel = ui::button(tr("Cancel"));
    auto* doneBtn = ui::button(tr("I'm signed in"), "check2", true);
    row->addWidget(m_status, 1);
    row->addWidget(cancel);
    row->addWidget(doneBtn);
    col->addLayout(row);

    // Leaving the sign-in pages looks like success, but clicking the site's logo does too: confirm with the
    // site (a page that needs a sign-in) before anything is recorded.
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl& u) {
        if (m_done || m_checking || WebSessions::statusFromUrl(m_site, u) != "signed_in") return;
        m_checking = true;
        m_status->setText(tr("Checking the sign-in…"));
        WebSessions::instance()->check(m_profileId, m_site, this, [this](const QString& status) {
            m_checking = false;
            if (status == "signed_in") return finish(status);
            m_status->setText(tr("Not signed in yet — finish signing in, then click “I'm signed in”."));
        });
    });
    connect(doneBtn, &QPushButton::clicked, this, [this, doneBtn] {
        doneBtn->setEnabled(false);
        m_status->setText(tr("Checking with the site…"));
        WebSessions::instance()->check(m_profileId, m_site, this, [this](const QString& status) { finish(status); });
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_view->load(site.signIn);
}

void SiteSignInDialog::finish(const QString& status) {
    if (m_done) return;
    m_done = true;
    emit finishedWith(status);
    accept();
}

// -- PageGrabber ------------------------------------------------------------------------------------
PageGrabber::PageGrabber(const QUrl& url, const QString& seekProfileId, const QString& detectJs, int timeoutMs,
                         QObject* parent)
    : QObject(parent), m_detectJs(detectJs) {
    m_page = new QWebEnginePage(WebSessions::instance()->profileForUrl(seekProfileId, url), this);
    m_poll = new QTimer(this);
    m_poll->setInterval(1000);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(timeoutMs);
    m_started.start();
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (m_finished) return;
        poll();
        // Cloudflare's automatic check clears itself within a few seconds; an interactive one never does.
        if (m_started.elapsed() > 8000) {
            m_page->runJavaScript(QString::fromUtf8(kChallengeJs), [this](const QVariant& challenge) {
                if (!m_finished && challenge.isValid() && challenge.toBool()) {
                    m_finished = true;
                    emit failed(QStringLiteral("challenge"));
                    done();
                }
            });
        }
    });
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_finished) return;
        m_finished = true;
        emit failed(QStringLiteral("timeout"));
        done();
    });
    m_poll->start();
    m_timeout->start();
    m_page->load(url);
}

PageGrabber::~PageGrabber() { m_finished = true; }

void PageGrabber::poll() {
    m_page->runJavaScript(m_detectJs, [this](const QVariant& found) {
        if (m_finished || !found.toBool()) return;
        m_finished = true;
        const QUrl url = m_page->url();
        m_page->runJavaScript(QStringLiteral("document.documentElement.outerHTML"), [this, url](const QVariant& html) {
            if (!html.isValid()) return;  // the page is being destroyed (app closing)
            emit captured(html.toString(), url);
            done();
        });
    });
}

void PageGrabber::done() {
    m_poll->stop();
    m_timeout->stop();
    deleteLater();
}

#else  // built without Qt WebEngine

BrowserImportDialog::Options BrowserImportDialog::postingOptions(const QString&) { return {}; }
BrowserImportDialog::Options BrowserImportDialog::searchOptions(const QString&) { return {}; }
BrowserImportDialog::BrowserImportDialog(const QUrl&, const QString&, const Options& options, QWidget* parent)
    : QDialog(parent), m_options(options) {}
BrowserImportDialog::~BrowserImportDialog() = default;
void BrowserImportDialog::done(int result) { QDialog::done(result); }
void BrowserImportDialog::checkPage() {}
void BrowserImportDialog::capture() {}
SiteSignInDialog::SiteSignInDialog(const QString& siteKey, const QString& seekProfileId, QWidget* parent)
    : QDialog(parent), m_site(siteKey), m_profileId(seekProfileId) {}
void SiteSignInDialog::finish(const QString&) {}
PageGrabber::PageGrabber(const QUrl&, const QString&, const QString& detectJs, int, QObject* parent)
    : QObject(parent), m_detectJs(detectJs) {
    QTimer::singleShot(0, this, [this] {
        emit failed(QStringLiteral("unavailable"));
        deleteLater();
    });
}
PageGrabber::~PageGrabber() = default;
void PageGrabber::poll() {}
void PageGrabber::done() {}

#endif
