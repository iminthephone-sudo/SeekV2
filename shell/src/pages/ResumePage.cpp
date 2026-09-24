#include "pages/ResumePage.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMenu>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

namespace {

QWidget* scrollColumn(QVBoxLayout*& layout) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget;
    layout = new QVBoxLayout(inner);
    layout->setContentsMargins(4, 10, 10, 10);
    layout->setSpacing(10);
    scroll->setWidget(inner);
    return scroll;
}

QString lastDir() {
    const QString saved = QSettings().value("ui/lastExportDir").toString();
    return saved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : saved;
}

}  // namespace

bool ResumePage::writePdf(const QString& path, const QString& content, bool plainText) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::Letter));
    writer.setPageMargins(QMarginsF(16, 14, 16, 14), QPageLayout::Millimeter);
    writer.setResolution(300);
    writer.setCreator(QStringLiteral("SEEK"));
    QTextDocument doc;
    doc.setDocumentMargin(0);
    QFont base("Calibri");
    base.setPointSizeF(10.5);
    doc.setDefaultFont(base);
    if (plainText) {
        QFont f("Georgia");
        f.setPointSizeF(11);
        doc.setDefaultFont(f);
        doc.setPlainText(content);
    } else {
        doc.setHtml(content);
    }
    // Paginate explicitly to the printable area (in screen pixels, which print() scales from).
    // Left unpaginated, QTextDocument::print() adds its own 2 cm margin and page numbers.
    const qreal screenDpi = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->logicalDotsPerInch() : 96.0;
    doc.setPageSize(writer.pageLayout().paintRect(QPageLayout::Inch).size() * screenDpi);
    doc.print(&writer);
    return true;
}

ResumePage::ResumePage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);

    auto* tools = new QWidget;
    auto* tl = new QHBoxLayout(tools);
    tl->setContentsMargins(0, 0, 0, 0);
    m_profile = new QComboBox;
    m_profile->setMinimumWidth(240);
    m_template = new QComboBox;
    m_template->addItem(tr("Classic"), "classic");
    m_template->addItem(tr("Modern"), "modern");
    m_template->addItem(tr("Compact"), "compact");
    auto* edit = ui::button(tr("Edit profile"), "pencil");
    auto* copy = ui::flatButton("clipboard", tr("Copy as plain text"));
    auto* exportBtn = ui::button(tr("Export"), "download", true);
    auto* menu = new QMenu(exportBtn);
    menu->addAction(Theme::instance()->icon("filetype-pdf"), tr("PDF (print-ready)"), this, [this] { exportAs("pdf"); });
    menu->addAction(tr("Word document (.docx)"), this, [this] { exportAs("docx"); });
    menu->addAction(tr("Web page (.html)"), this, [this] { exportAs("html"); });
    menu->addAction(tr("Plain text (.txt) — for online forms"), this, [this] { exportAs("txt"); });
    menu->addAction(tr("Markdown (.md)"), this, [this] { exportAs("md"); });
    exportBtn->setMenu(menu);
    exportBtn->setProperty("hasMenu", true);
    tl->addWidget(m_profile);
    tl->addWidget(m_template);
    tl->addWidget(edit);
    tl->addWidget(copy);
    tl->addWidget(exportBtn);
    col->addWidget(ui::pageHeader(tr("Resume builder"), tr("Live preview of the selected profile. Edit content on the "
                                                            "Profiles page; export when it looks right."), tools));

    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    col->addWidget(split, 1);

    m_preview = new QTextBrowser;
    m_preview->setObjectName("Paper");
    m_preview->setOpenExternalLinks(true);
    m_preview->setMinimumWidth(460);
    split->addWidget(m_preview);

    auto* sideCard = new Card;
    sideCard->setMinimumWidth(340);
    sideCard->body()->setContentsMargins(10, 10, 6, 10);
    m_side = new QTabWidget;
    m_side->setDocumentMode(true);
    sideCard->body()->addWidget(m_side);
    split->addWidget(sideCard);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    // Coach tab
    QVBoxLayout* coach = nullptr;
    m_side->addTab(scrollColumn(coach), Theme::instance()->icon("stars"), tr("Coach"));
    auto* scoreRow = new QHBoxLayout;
    m_score = new ScoreRing;
    m_score->setFixedSize(120, 120);
    scoreRow->addWidget(m_score);
    m_checks = new QVBoxLayout;
    m_checks->setSpacing(4);
    scoreRow->addLayout(m_checks, 1);
    coach->addLayout(scoreRow);
    coach->addWidget(ui::label(tr("Bullet feedback"), "CardTitle"));
    m_bullets = new QVBoxLayout;
    m_bullets->setSpacing(8);
    coach->addLayout(m_bullets);
    coach->addStretch(1);

    // Fair-chance tab
    QVBoxLayout* fc = nullptr;
    m_side->addTab(scrollColumn(fc), Theme::instance()->icon("shield-check"), tr("Fair-chance review"));
    m_fcSummary = ui::label(QString());
    fc->addWidget(m_fcSummary);
    m_findings = new QVBoxLayout;
    m_findings->setSpacing(8);
    fc->addLayout(m_findings);
    fc->addWidget(ui::label(tr("Guidance for staff and participants"), "CardTitle"));
    m_guidance = new QVBoxLayout;
    m_guidance->setSpacing(8);
    fc->addLayout(m_guidance);
    fc->addStretch(1);

    connect(m_profile, &QComboBox::currentIndexChanged, this, &ResumePage::loadProfile);
    connect(m_template, &QComboBox::currentIndexChanged, this, &ResumePage::render);
    connect(edit, &QPushButton::clicked, this, [this] { m_ctx->navigate("profiles", {{"profile_id", profileId()}}); });
    connect(copy, &QPushButton::clicked, this, [this] {
        m_ctx->bridge()->call("resume.render", {{"profile_id", profileId()}, {"format", "text"}},
                              [this](const QJsonValue& r, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Copying"), e);
            QApplication::clipboard()->setText(r.toObject().value("content").toString());
            m_ctx->toast(tr("Plain-text resume copied — paste it into online application forms."), AppContext::ToastKind::Success);
        }, this);
    });
    connect(ctx, &AppContext::profilesChanged, this, [this] { m_ctx->fillProfileCombo(m_profile); });
}

QString ResumePage::profileId() const { return m_profile->currentData().toString(); }

void ResumePage::activate(const QVariantMap& args) {
    const QString before = profileId();
    m_ctx->fillProfileCombo(m_profile, args.value("profile_id").toString());
    if (args.value("tab").toString() == "fairchance") m_side->setCurrentIndex(1);
    // fillProfileCombo already triggered loadProfile() if the selection moved.
    if (profileId() == before) loadProfile();
}

void ResumePage::loadProfile() {
    const QString id = profileId();
    if (id.isEmpty()) return render();
    // Start from the profile's saved template, then render.
    m_ctx->bridge()->call("profile.get", {{"profile_id", id}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Opening profile"), e);
        const QJsonObject p = r.toObject();
        m_fullName = p.value("contact").toObject().value("full_name").toString();
        const int idx = m_template->findData(p.value("options").toObject().value("template").toString());
        QSignalBlocker block(m_template);
        if (idx >= 0) m_template->setCurrentIndex(idx);
        block.unblock();
        render();
    }, this);
}

void ResumePage::render() {
    const QString id = profileId();
    if (id.isEmpty()) {
        m_preview->setHtml(tr("<p style='color:#6b7280'>Create a profile first — the resume preview appears here.</p>"));
        return;
    }
    m_ctx->bridge()->call("resume.render",
                          {{"profile_id", id}, {"format", "html"}, {"template", m_template->currentData().toString()}},
                          [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Rendering resume"), e);
        m_html = r.toObject().value("content").toString();
        const int scroll = m_preview->verticalScrollBar()->value();
        m_preview->setHtml(m_html);
        m_preview->verticalScrollBar()->setValue(scroll);
    }, this);
    analyze();
    review();
}

void ResumePage::analyze() {
    m_ctx->bridge()->call("resume.analyze", {{"profile_id", profileId()}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        const QJsonObject a = r.toObject();
        m_score->setScore(a.value("score").toInt(), tr("resume score"));
        ui::clearLayout(m_checks);
        const auto& t = Theme::instance()->p();
        for (const QJsonValue& v : a.value("checks").toArray()) {
            const QJsonObject c = v.toObject();
            const bool ok = c.value("ok").toBool();
            auto* row = new QHBoxLayout;
            auto* icon = new QLabel;
            icon->setPixmap(Theme::instance()->pixmap(ok ? "check-circle" : "exclamation-triangle", 14, ok ? t.success : t.warning));
            auto* text = ui::label(c.value("label").toString());
            if (!ok) text->setToolTip(c.value("tip").toString());
            row->addWidget(icon);
            row->addWidget(text, 1);
            m_checks->addLayout(row);
        }
        ui::clearLayout(m_bullets);
        const QJsonArray bullets = a.value("bullets").toArray();
        if (bullets.isEmpty()) m_bullets->addWidget(ui::label(tr("Add experience bullets to get feedback."), "Muted"));
        for (const QJsonValue& v : bullets) {
            const QJsonObject b = v.toObject();
            const QJsonArray issues = b.value("issues").toArray();
            auto* card = new Card;
            card->body()->setContentsMargins(12, 10, 12, 10);
            auto* head = new QHBoxLayout;
            head->addWidget(ui::label(b.value("text").toString()), 1);
            const int s = b.value("score").toInt();
            head->addWidget(ui::badge(QString::number(s), s >= 75 ? "good" : s >= 50 ? "warn" : "bad"), 0, Qt::AlignTop);
            card->body()->addLayout(head);
            auto* where = ui::label(b.value("entry").toString(), "Muted");
            card->body()->addWidget(where);
            for (const QJsonValue& issue : issues) card->body()->addWidget(ui::label("• " + issue.toString(), "Muted"));
            // Weakest bullets first is more useful; the engine returns profile order, so insert weak ones on top.
            if (s < 75) m_bullets->insertWidget(0, card);
            else m_bullets->addWidget(card);
        }
    }, this);
}

void ResumePage::review() {
    m_ctx->bridge()->call("fairchance.review", {{"profile_id", profileId()}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        const QJsonObject rv = r.toObject();
        m_fcSummary->setText(rv.value("summary").toString());
        ui::clearLayout(m_findings);
        for (const QJsonValue& v : rv.value("findings").toArray()) {
            const QJsonObject f = v.toObject();
            auto* card = new Card;
            card->body()->setContentsMargins(12, 10, 12, 10);
            auto* head = new QHBoxLayout;
            head->addWidget(ui::label(f.value("location").toString(), "CardTitle"), 1);
            head->addWidget(ui::badge(f.value("term").toString(), "warn"));
            card->body()->addLayout(head);
            card->body()->addWidget(ui::label("“" + f.value("text").toString() + "”", "Muted"));
            card->body()->addWidget(ui::label(f.value("suggestion").toString()));
            if (f.contains("title_idea"))
                card->body()->addWidget(ui::label(tr("Idea: list the role as “%1”.").arg(f.value("title_idea").toString())));
            m_findings->addWidget(card);
        }
        for (const QJsonValue& v : rv.value("gaps").toArray()) {
            const QJsonObject g = v.toObject();
            auto* card = new Card;
            card->body()->setContentsMargins(12, 10, 12, 10);
            auto* head = new QHBoxLayout;
            head->addWidget(ui::label(tr("Gap: %1 – %2").arg(g.value("from").toString(), g.value("to").toString()), "CardTitle"), 1);
            head->addWidget(ui::badge(tr("%1 months").arg(g.value("months").toInt()), "accent"));
            card->body()->addLayout(head);
            card->body()->addWidget(ui::label(g.value("suggestion").toString(), "Muted"));
            m_findings->addWidget(card);
        }
        ui::clearLayout(m_guidance);
        for (const QJsonValue& v : rv.value("guidance").toArray()) {
            const QJsonObject gd = v.toObject();
            auto* card = new Card;
            card->body()->setContentsMargins(12, 10, 12, 10);
            card->body()->addWidget(ui::label(gd.value("title").toString(), "CardTitle"));
            card->body()->addWidget(ui::label(gd.value("body").toString(), "Muted"));
            m_guidance->addWidget(card);
        }
    }, this);
}

QString ResumePage::suggestedFileName(const QString& ext) const {
    QString base = m_fullName.isEmpty() ? m_profile->currentText() : m_fullName;
    base = base.remove(QRegularExpression(R"([\\/:*?"<>|])")).trimmed();
    return QDir(lastDir()).filePath(base + tr(" Resume.") + ext);
}

void ResumePage::exportAs(const QString& format) {
    const QString id = profileId();
    if (id.isEmpty()) return;
    const QHash<QString, QString> filters = {
        {"pdf", tr("PDF (*.pdf)")}, {"docx", tr("Word document (*.docx)")}, {"html", tr("Web page (*.html)")},
        {"txt", tr("Text (*.txt)")}, {"md", tr("Markdown (*.md)")}};
    const QString path = QFileDialog::getSaveFileName(this, tr("Export resume"), suggestedFileName(format), filters.value(format));
    if (path.isEmpty()) return;
    QSettings().setValue("ui/lastExportDir", QFileInfo(path).absolutePath());
    if (format == "pdf") {
        // The shell prints the exact HTML shown in the preview.
        writePdf(path, m_html);
        m_ctx->toast(tr("Saved %1").arg(QDir::toNativeSeparators(path)), AppContext::ToastKind::Success);
        return;
    }
    m_ctx->bridge()->call("resume.export",
                          {{"profile_id", id}, {"format", format}, {"path", path}, {"template", m_template->currentData().toString()}},
                          [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Export"), e);
        m_ctx->toast(tr("Saved %1").arg(QDir::toNativeSeparators(r.toObject().value("path").toString())),
                     AppContext::ToastKind::Success);
    }, this);
}
