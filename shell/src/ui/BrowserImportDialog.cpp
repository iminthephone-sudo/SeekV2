#include "ui/BrowserImportDialog.h"

#ifdef SEEK_HAS_WEBENGINE
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

#include "ui/Widgets.h"

namespace {
// True once the page shows a job description: the same containers and JSON-LD the engine reads.
const char* kHasPostingJs = R"JS(
(() => !!document.querySelector('#jobDescriptionText, .show-more-less-html__markup, [data-automation-id=jobPostingDescription]')
    || [...document.querySelectorAll('script[type="application/ld+json"]')].some(s => s.textContent.includes('JobPosting')))()
)JS";

QWebEngineProfile* importProfile() {
    // Persistent, so a passed Cloudflare check (its clearance cookie) carries over to the next import.
    static QWebEngineProfile* profile = [] {
        auto* p = new QWebEngineProfile(QStringLiteral("seek-import"), qApp);
        // The default user agent advertises "QtWebEngine/6.x", which bot filters treat as automation.
        p->setHttpUserAgent(p->httpUserAgent().remove(QRegularExpression(QStringLiteral("QtWebEngine/\\S+\\s*"))));
        return p;
    }();
    return profile;
}
}  // namespace

bool BrowserImportDialog::available() { return true; }

BrowserImportDialog::BrowserImportDialog(const QUrl& url, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Import posting — %1").arg(url.host()));
    resize(1100, 780);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(10);
    auto* intro = ui::label(tr("%1 only shows postings to web browsers, so SEEK opened it here. If a "
                               "“Verify you are human” box appears, tick it. SEEK imports the posting as soon as the "
                               "job description shows up, or click “Import this page”.").arg(url.host()), "Muted");
    intro->setWordWrap(true);
    col->addWidget(intro);

    m_view = new QWebEngineView;
    m_view->setPage(new QWebEnginePage(importProfile(), m_view));
    col->addWidget(m_view, 1);

    auto* row = new QHBoxLayout;
    m_status = ui::label(tr("Loading…"), "Muted");
    m_import = ui::button(tr("Import this page"), "download", true);
    m_import->setEnabled(false);
    auto* cancel = ui::button(tr("Cancel"));
    row->addWidget(m_status, 1);
    row->addWidget(cancel);
    row->addWidget(m_import);
    col->addLayout(row);

    // Some boards render the description with JavaScript after the load finishes, and a
    // challenge page reloads into the real one, so keep checking while the dialog is open.
    m_poll = new QTimer(this);
    m_poll->setInterval(1500);
    connect(m_poll, &QTimer::timeout, this, &BrowserImportDialog::checkForPosting);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_import->setEnabled(true);
        m_status->setText(ok ? tr("Waiting for the job description…") : tr("The page didn't finish loading."));
        checkForPosting();
        m_poll->start();
    });
    connect(m_import, &QPushButton::clicked, this, &BrowserImportDialog::capture);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_view->load(url);
}

void BrowserImportDialog::checkForPosting() {
    if (m_captured) return;
    m_view->page()->runJavaScript(QString::fromUtf8(kHasPostingJs), [this](const QVariant& found) {
        if (found.toBool()) capture();
    });
}

void BrowserImportDialog::capture() {
    if (m_captured) return;
    m_captured = true;
    m_poll->stop();
    m_import->setEnabled(false);
    m_status->setText(tr("Reading the posting…"));
    // outerHTML rather than the downloaded source: it includes what the page's scripts rendered.
    m_view->page()->runJavaScript(QStringLiteral("document.documentElement.outerHTML"), [this](const QVariant& html) {
        emit pageCaptured(html.toString(), m_view->url());
        accept();
    });
}

#else  // built without Qt WebEngine

bool BrowserImportDialog::available() { return false; }
BrowserImportDialog::BrowserImportDialog(const QUrl& url, QWidget* parent) : QDialog(parent) { Q_UNUSED(url); }
void BrowserImportDialog::checkForPosting() {}
void BrowserImportDialog::capture() {}

#endif
