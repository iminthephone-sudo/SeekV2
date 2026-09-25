#include "pages/JobsPage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "ui/BrowserImportDialog.h"

namespace {
const QStringList kStatuses = {"saved", "applying", "applied", "interview", "offer", "hired", "closed"};

QString statusLabel(const QString& s) {
    static const QHash<QString, QString> labels = {
        {"saved", QObject::tr("Saved")},       {"applying", QObject::tr("Applying")}, {"applied", QObject::tr("Applied")},
        {"interview", QObject::tr("Interview")}, {"offer", QObject::tr("Offer")},     {"hired", QObject::tr("Hired")},
        {"closed", QObject::tr("Closed")}};
    return labels.value(s, s);
}
}  // namespace

JobsPage::JobsPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);
    col->addWidget(ui::pageHeader(tr("Job postings"),
                                  tr("Import a posting from its link, or paste the description. SEEK extracts the "
                                     "keywords and flags fair-chance and background-check language.")));

    // -- import bar ------------------------------------------------------------------------
    auto* importCard = new Card;
    auto* row = new QHBoxLayout;
    m_url = new QLineEdit;
    m_url->setPlaceholderText(tr("Paste a job link, e.g. https://careers.example.com/jobs/warehouse-associate"));
    m_url->setClearButtonEnabled(true);
    m_fetch = ui::button(tr("Import from link"), "link", true);
    auto* pasteToggle = ui::button(tr("Paste description"), "clipboard");
    row->addWidget(m_url, 1);
    row->addWidget(m_fetch);
    row->addWidget(pasteToggle);
    importCard->body()->addLayout(row);
    auto* progRow = new QHBoxLayout;
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    m_progressText = ui::label(QString(), "Muted");
    progRow->addWidget(m_progressText);
    progRow->addWidget(m_progress, 1);
    importCard->body()->addLayout(progRow);
    m_progress->hide();
    m_progressText->hide();
    col->addWidget(importCard);

    m_pasteCard = new Card;
    auto* pasteRow = new QHBoxLayout;
    m_pasteTitle = new QLineEdit;
    m_pasteTitle->setPlaceholderText(tr("Job title"));
    m_pasteCompany = new QLineEdit;
    m_pasteCompany->setPlaceholderText(tr("Company"));
    m_pasteLocation = new QLineEdit;
    m_pasteLocation->setPlaceholderText(tr("Location (optional)"));
    pasteRow->addWidget(m_pasteTitle, 2);
    pasteRow->addWidget(m_pasteCompany, 2);
    pasteRow->addWidget(m_pasteLocation, 1);
    m_pasteCard->body()->addLayout(pasteRow);
    m_pasteText = new QPlainTextEdit;
    m_pasteText->setPlaceholderText(tr("Paste the full job description here — responsibilities, requirements, everything."));
    m_pasteText->setMinimumHeight(160);
    m_pasteCard->body()->addWidget(m_pasteText);
    auto* pasteGo = ui::button(tr("Analyze && save"), "magic", true);
    auto* pasteRow2 = new QHBoxLayout;
    pasteRow2->addStretch(1);
    pasteRow2->addWidget(pasteGo);
    m_pasteCard->body()->addLayout(pasteRow2);
    m_pasteCard->hide();
    col->addWidget(m_pasteCard);

    // -- list + detail ------------------------------------------------------------------------
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    col->addWidget(split, 1);

    auto* listCard = new Card;
    listCard->setMinimumWidth(260);
    listCard->setMaximumWidth(380);
    listCard->body()->addWidget(ui::label(tr("Saved postings"), "CardTitle"));
    m_list = new QListWidget;
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    listCard->body()->addWidget(m_list, 1);
    split->addWidget(listCard);

    auto* detailCard = new Card;
    m_detailStack = new QStackedWidget;
    detailCard->body()->addWidget(m_detailStack);
    auto* empty = ui::label(tr("Import or select a posting to see its keywords."), "Muted");
    empty->setAlignment(Qt::AlignCenter);
    m_detailStack->addWidget(empty);

    auto* detailScroll = new QScrollArea;
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    auto* detail = new QWidget;
    auto* dl = new QVBoxLayout(detail);
    dl->setContentsMargins(0, 0, 8, 0);
    dl->setSpacing(10);
    auto* head = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    m_title = ui::label(QString(), "SectionTitle");
    m_meta = ui::label(QString(), "Muted");
    titles->addWidget(m_title);
    titles->addWidget(m_meta);
    head->addLayout(titles, 1);
    m_status = new QComboBox;
    for (const QString& s : kStatuses) m_status->addItem(statusLabel(s), s);
    head->addWidget(ui::label(tr("Status"), "Muted"), 0, Qt::AlignVCenter);
    head->addWidget(m_status, 0, Qt::AlignVCenter);
    dl->addLayout(head);
    m_signals = new QHBoxLayout;
    m_signals->setSpacing(6);
    dl->addLayout(m_signals);

    auto* actions = new QHBoxLayout;
    auto* optimize = ui::button(tr("Match with a profile"), "bullseye", true);
    auto* letter = ui::button(tr("Write cover letter"), "envelope-paper");
    m_openLink = ui::button(tr("Open posting"), "box-arrow-up-right");
    auto* del = ui::flatButton("trash", tr("Delete posting"));
    actions->addWidget(optimize);
    actions->addWidget(letter);
    actions->addWidget(m_openLink);
    actions->addStretch(1);
    actions->addWidget(del);
    dl->addLayout(actions);

    dl->addWidget(ui::label(tr("Keywords (bold outline = stated as required)"), "CardTitle"));
    auto* chipHost = new QWidget;
    m_keywords = new FlowLayout(chipHost);
    dl->addWidget(chipHost);
    dl->addWidget(ui::label(tr("Description"), "CardTitle"));
    m_description = new QTextBrowser;
    m_description->setMinimumHeight(220);
    dl->addWidget(m_description, 1);
    dl->addWidget(ui::label(tr("Notes"), "CardTitle"));
    m_notes = new QPlainTextEdit;
    m_notes->setPlaceholderText(tr("Contact person, application deadline, follow-up date…"));
    m_notes->setMaximumHeight(100);
    dl->addWidget(m_notes);
    detailScroll->setWidget(detail);
    m_detailStack->addWidget(detailScroll);
    split->addWidget(detailCard);
    split->setStretchFactor(1, 1);

    // -- wiring --------------------------------------------------------------------------------
    connect(m_fetch, &QPushButton::clicked, this, &JobsPage::fetch);
    connect(m_url, &QLineEdit::returnPressed, this, &JobsPage::fetch);
    connect(pasteToggle, &QPushButton::clicked, this, [this] {
        m_pasteCard->setVisible(!m_pasteCard->isVisible());
        if (m_pasteCard->isVisible()) m_pasteText->setFocus();
    });
    connect(pasteGo, &QPushButton::clicked, this, &JobsPage::analyzePaste);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item) showJob(item->data(Qt::UserRole).toString());
    });
    connect(m_status, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_job.isEmpty() && m_status->currentData().toString() != m_job.value("status").toString())
            updateJob({{"status", m_status->currentData().toString()}});
    });
    auto* notesTimer = new QTimer(this);
    notesTimer->setSingleShot(true);
    notesTimer->setInterval(800);
    connect(m_notes, &QPlainTextEdit::textChanged, notesTimer, qOverload<>(&QTimer::start));
    connect(notesTimer, &QTimer::timeout, this, [this] {
        if (!m_job.isEmpty() && m_notes->toPlainText() != m_job.value("notes").toString())
            updateJob({{"notes", m_notes->toPlainText()}});
    });
    connect(optimize, &QPushButton::clicked, this, [this] {
        m_ctx->navigate("optimize", {{"job_id", m_job.value("id").toString()}, {"run", true}});
    });
    connect(letter, &QPushButton::clicked, this, [this] {
        m_ctx->navigate("letters", {{"job_id", m_job.value("id").toString()}});
    });
    connect(m_openLink, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_job.value("url").toString())); });
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_job.isEmpty()) return;
        if (QMessageBox::question(this, tr("Delete posting"), tr("Delete “%1”?").arg(m_job.value("title").toString())) != QMessageBox::Yes)
            return;
        m_ctx->bridge()->call("job.delete", {{"job_id", m_job.value("id").toString()}}, [this](const QJsonValue&, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Deleting posting"), e);
            m_job = {};
            m_detailStack->setCurrentIndex(0);
            m_ctx->refreshJobs();
        }, this);
    });
    connect(ctx, &AppContext::jobsChanged, this, &JobsPage::rebuildList);
    connect(Theme::instance(), &Theme::changed, this, &JobsPage::rebuildList);  // re-tint list icons
    connect(ctx->bridge(), &SeekBridge::progress, this, [this](const QString& id, int pct, const QString& msg) {
        if (id != m_fetchRequest) return;
        m_progress->setValue(pct);
        m_progressText->setText(msg);
    });
}

void JobsPage::activate(const QVariantMap& args) {
    rebuildList();
    QString id = args.value("job_id").toString();
    if (id.isEmpty() && m_job.isEmpty() && !m_ctx->jobs().isEmpty()) id = m_ctx->jobs().first().toObject().value("id").toString();
    if (!id.isEmpty()) showJob(id);
}

void JobsPage::fetch() {
    const QString url = m_url->text().trimmed();
    if (url.isEmpty()) {
        m_url->setFocus();
        return;
    }
    m_fetch->setEnabled(false);
    m_progress->setValue(5);
    m_progress->show();
    m_progressText->setText(tr("Starting…"));
    m_progressText->show();
    m_fetchRequest = m_ctx->bridge()->call("job.fetch", {{"url", url}}, [this, url](const QJsonValue& r, const BridgeError& e) {
        m_fetch->setEnabled(true);
        m_progress->hide();
        m_progressText->hide();
        m_fetchRequest.clear();
        if (e.isError()) {
            // The site turned away SEEK's downloader (Indeed does); a real browser gets through.
            if (e.code == "fetch_blocked" && BrowserImportDialog::available()) return importInBrowser(url);
            m_ctx->toast(e.message, AppContext::ToastKind::Warning);
            if (e.code == "fetch_failed" || e.code == "fetch_blocked") offerPaste();
            return;
        }
        imported(r.toObject());
    }, this);
}

void JobsPage::importInBrowser(const QString& url) {
    m_ctx->bridge()->call("job.page_url", {{"url", url}}, [this, url](const QJsonValue& r, const BridgeError& e) {
        auto* dlg = new BrowserImportDialog(QUrl(e.isError() ? url : r.toString()), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &BrowserImportDialog::pageCaptured, this, [this](const QString& html, const QUrl& pageUrl) {
            const QJsonObject params{{"html", html}, {"url", pageUrl.toString()}};
            m_ctx->bridge()->call("job.from_html", params, [this](const QJsonValue& job, const BridgeError& err) {
                if (!err.isError()) return imported(job.toObject());
                m_ctx->toast(err.message, AppContext::ToastKind::Warning);
                offerPaste();
            }, this);
        });
        connect(dlg, &QDialog::rejected, this, &JobsPage::offerPaste);
        dlg->open();
    }, this);
}

void JobsPage::imported(const QJsonObject& job) {
    m_url->clear();
    m_ctx->toast(tr("Imported “%1” — %2 keywords found.").arg(job.value("title").toString())
                     .arg(job.value("keywords").toArray().size()), AppContext::ToastKind::Success);
    m_ctx->refreshJobs();
    populate(job);
}

void JobsPage::offerPaste() {
    m_pasteCard->show();
    m_pasteText->setFocus();
}

void JobsPage::analyzePaste() {
    const QString text = m_pasteText->toPlainText().trimmed();
    if (text.split(QRegularExpression("\\s+")).size() < 25) {
        m_ctx->toast(tr("Paste the whole description (at least a few sentences) so keywords can be found."),
                     AppContext::ToastKind::Warning);
        return;
    }
    const QJsonObject params{{"text", text}, {"title", m_pasteTitle->text().trimmed()},
                             {"company", m_pasteCompany->text().trimmed()}, {"location", m_pasteLocation->text().trimmed()},
                             {"url", m_url->text().trimmed()}};
    m_ctx->bridge()->call("job.from_text", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Analyzing posting"), e);
        for (QLineEdit* l : {m_pasteTitle, m_pasteCompany, m_pasteLocation, m_url}) l->clear();
        m_pasteText->clear();
        m_pasteCard->hide();
        m_ctx->toast(tr("Posting saved."), AppContext::ToastKind::Success);
        m_ctx->refreshJobs();
        populate(r.toObject());
    }, this);
}

void JobsPage::rebuildList() {
    const QString keep = m_job.value("id").toString();
    QSignalBlocker block(m_list);
    m_list->clear();
    for (const QJsonValue& v : m_ctx->jobs()) {
        const QJsonObject j = v.toObject();
        QString text = j.value("title").toString();
        QStringList meta;
        if (!j.value("company").toString().isEmpty()) meta << j.value("company").toString();
        meta << statusLabel(j.value("status").toString());
        text += "\n" + meta.join("  ·  ");
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, j.value("id").toString());
        if (j.value("fair_chance").toBool()) {
            item->setIcon(Theme::instance()->icon("shield-check", Theme::instance()->p().success));
            item->setToolTip(tr("Fair-chance language found in this posting"));
        } else {
            item->setIcon(Theme::instance()->icon("briefcase"));
        }
        if (j.value("id").toString() == keep) m_list->setCurrentItem(item);
    }
    if (m_ctx->jobs().isEmpty()) {
        auto* hint = new QListWidgetItem(tr("No postings yet."), m_list);
        hint->setFlags(Qt::NoItemFlags);
    }
}

void JobsPage::showJob(const QString& id) {
    m_ctx->bridge()->call("job.get", {{"job_id", id}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Opening posting"), e);
        populate(r.toObject());
    }, this);
}

void JobsPage::populate(const QJsonObject& job) {
    m_job = job;
    m_detailStack->setCurrentIndex(1);
    m_title->setText(job.value("title").toString());
    QStringList meta;
    for (const char* key : {"company", "location", "employment_type", "salary"}) {
        const QString v = job.value(QLatin1String(key)).toString();
        if (!v.isEmpty()) meta << v;
    }
    m_meta->setText(meta.join("  ·  "));
    {
        QSignalBlocker block(m_status);
        m_status->setCurrentIndex(qMax(0, m_status->findData(job.value("status").toString())));
    }
    {
        QSignalBlocker block(m_notes);
        m_notes->setPlainText(job.value("notes").toString());
    }
    m_openLink->setVisible(!job.value("url").toString().isEmpty());

    ui::clearLayout(m_signals);
    const QJsonObject sig = job.value("signals").toObject();
    if (sig.value("fair_chance").toBool()) m_signals->addWidget(ui::badge(tr("✓ Fair-chance employer language"), "good"));
    if (sig.value("background_check").toBool()) m_signals->addWidget(ui::badge(tr("Background check mentioned"), "warn"));
    if (sig.value("drug_screen").toBool()) m_signals->addWidget(ui::badge(tr("Drug screen"), "warn"));
    if (sig.value("driving_record").toBool()) m_signals->addWidget(ui::badge(tr("Driving record check"), "warn"));
    if (sig.value("license_required").toBool()) m_signals->addWidget(ui::badge(tr("Driver's license required"), "accent"));
    for (const QJsonValue& ex : sig.value("exclusions").toArray())
        m_signals->addWidget(ui::badge(tr("Exclusion: “%1”").arg(ex.toString()), "bad"));
    m_signals->addStretch(1);

    ui::clearLayout(m_keywords);
    for (const QJsonValue& v : job.value("keywords").toArray()) {
        const QJsonObject k = v.toObject();
        QString tip = k.value("kind").toString() == "skill" ? k.value("category").toString() : tr("Phrase from the posting");
        if (k.value("required").toBool()) tip += tr(" · required");
        m_keywords->addWidget(ui::chip(k.value("term").toString(), k.value("required").toBool() ? "required" : QString(), tip));
    }
    QString html;
    for (const QString& line : job.value("description").toString().split('\n')) {
        const QString esc = line.toHtmlEscaped();
        if (esc.trimmed().isEmpty()) html += "<br/>";
        else if (esc.startsWith(QChar(0x2022))) html += "<p style='margin:2px 0 2px 14px'>" + esc + "</p>";
        else html += "<p style='margin:4px 0'>" + esc + "</p>";
    }
    m_description->setHtml(html);
    rebuildList();
}

void JobsPage::updateJob(const QJsonObject& changes) {
    const QString id = m_job.value("id").toString();
    m_ctx->bridge()->call("job.update", {{"job_id", id}, {"changes", changes}}, [this, changes](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Updating posting"), e);
        m_job = r.toObject();
        if (changes.contains("status")) {
            m_ctx->refreshJobs();
            m_ctx->toast(tr("Status: %1").arg(statusLabel(m_job.value("status").toString())), AppContext::ToastKind::Success);
        }
    }, this);
}
