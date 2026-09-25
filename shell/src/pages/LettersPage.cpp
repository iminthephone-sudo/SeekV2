#include "pages/LettersPage.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "pages/ResumePage.h"

LettersPage::LettersPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);
    col->addWidget(ui::pageHeader(tr("Cover letters"),
                                  tr("Drafted from the participant's own bullets and skills, matched to the posting. "
                                     "Always read and personalise before sending.")));

    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    col->addWidget(split, 1);

    auto* listCard = new Card;
    listCard->setMinimumWidth(230);
    listCard->setMaximumWidth(330);
    listCard->body()->addWidget(ui::label(tr("Saved letters"), "CardTitle"));
    m_list = new QListWidget;
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    listCard->body()->addWidget(m_list, 1);
    auto* listTools = new QHBoxLayout;
    auto* newBtn = ui::button(tr("New"), "plus-lg");
    auto* del = ui::flatButton("trash", tr("Delete letter"));
    listTools->addWidget(newBtn);
    listTools->addStretch(1);
    listTools->addWidget(del);
    listCard->body()->addLayout(listTools);
    split->addWidget(listCard);

    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(14);

    auto* options = new Card;
    auto* grid = new QHBoxLayout;
    grid->setSpacing(20);
    auto* fa = new QFormLayout;
    auto* fb = new QFormLayout;
    for (QFormLayout* f : {fa, fb}) {
        f->setVerticalSpacing(8);
        f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    }
    m_profile = new QComboBox;
    m_job = new QComboBox;
    m_tone = new QComboBox;
    m_tone->addItem(tr("Professional"), "professional");
    m_tone->addItem(tr("Warm"), "warm");
    m_tone->addItem(tr("Direct"), "direct");
    m_fairChance = new QComboBox;
    m_fairChance->addItem(tr("Automatic (when the posting says fair-chance)"), "auto");
    m_fairChance->addItem(tr("Include a growth paragraph"), "include");
    m_fairChance->addItem(tr("Leave it out"), "omit");
    m_fairChance->setToolTip(tr("The paragraph talks about growth and readiness. It never mentions a record."));
    m_manager = new QLineEdit;
    m_manager->setPlaceholderText(tr("e.g. Ms. Lopez (leave blank for “Hiring Manager”)"));
    m_availability = new QLineEdit;
    m_availability->setPlaceholderText(tr("right away"));
    m_note = new QLineEdit;
    m_note->setPlaceholderText(tr("Optional personal sentence, e.g. why this company matters to them"));
    fa->addRow(tr("Profile"), m_profile);
    fa->addRow(tr("Posting"), m_job);
    fa->addRow(tr("Tone"), m_tone);
    fb->addRow(tr("Addressed to"), m_manager);
    fb->addRow(tr("Can start"), m_availability);
    fb->addRow(tr("Growth paragraph"), m_fairChance);
    grid->addLayout(fa, 1);
    grid->addLayout(fb, 1);
    options->body()->addLayout(grid);
    auto* noteRow = new QHBoxLayout;
    noteRow->addWidget(ui::label(tr("Personal touch"), "Muted"));
    noteRow->addWidget(m_note, 1);
    auto* gen = ui::button(tr("Write letter"), "magic", true);
    noteRow->addWidget(gen);
    options->body()->addLayout(noteRow);
    rl->addWidget(options);

    auto* editorCard = new Card;
    auto* head = new QHBoxLayout;
    m_meta = ui::label(tr("No letter open"), "Muted");
    head->addWidget(m_meta, 1);
    auto* copy = ui::flatButton("clipboard", tr("Copy to clipboard"));
    auto* exportBtn = ui::button(tr("Export"), "download");
    auto* menu = new QMenu(exportBtn);
    menu->addAction(tr("PDF"), this, [this] { exportAs("pdf"); });
    menu->addAction(tr("Word document (.docx)"), this, [this] { exportAs("docx"); });
    menu->addAction(tr("Plain text (.txt)"), this, [this] { exportAs("txt"); });
    exportBtn->setMenu(menu);
    exportBtn->setProperty("hasMenu", true);
    m_save = ui::button(tr("Save"), "save", true);
    head->addWidget(copy);
    head->addWidget(exportBtn);
    head->addWidget(m_save);
    editorCard->body()->addLayout(head);
    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(tr("Choose a profile and posting above, then click “Write letter”."));
    QFont serif("Georgia");
    serif.setPointSizeF(11);
    m_editor->setFont(serif);
    editorCard->body()->addWidget(m_editor, 1);
    rl->addWidget(editorCard, 1);
    split->addWidget(right);
    split->setStretchFactor(1, 1);

    connect(gen, &QPushButton::clicked, this, &LettersPage::generate);
    connect(m_save, &QPushButton::clicked, this, [this] { saveLetter(); });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { setDirty(true); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_editor->toPlainText());
        m_ctx->toast(tr("Letter copied."), AppContext::ToastKind::Success);
    });
    connect(newBtn, &QPushButton::clicked, this, [this] { resolveUnsaved([this] {
        ++m_session;
        m_loading = true;
        m_letterId.clear();
        m_editor->clear();
        m_meta->setText(tr("New letter — not saved yet"));
        m_list->clearSelection();
        m_loading = false;
        setDirty(false);
    }); });
    connect(del, &QPushButton::clicked, this, [this] {
        if (m_letterId.isEmpty()) return;
        if (QMessageBox::question(this, tr("Delete letter"), tr("Delete this letter?")) != QMessageBox::Yes) return;
        m_ctx->bridge()->call("letter.delete", {{"letter_id", m_letterId}}, [this](const QJsonValue&, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Deleting letter"), e);
            m_loading = true;
            m_letterId.clear();
            m_editor->clear();
            m_meta->setText(tr("No letter open"));
            m_loading = false;
            setDirty(false);
            refreshList();
        }, this);
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty() || id == m_letterId) return;
        resolveUnsaved([this, id] { openLetter(id); });
    });
    connect(ctx, &AppContext::profilesChanged, this, [this] { m_ctx->fillProfileCombo(m_profile); });
    connect(ctx, &AppContext::jobsChanged, this, [this] { m_ctx->fillJobCombo(m_job, {}, true); });
    connect(Theme::instance(), &Theme::changed, this, [this] {
        if (m_ctx->bridge()->isReady()) refreshList();  // re-tint list icons
    });
    setDirty(false);
}

void LettersPage::activate(const QVariantMap& args) {
    m_ctx->fillProfileCombo(m_profile, args.value("profile_id").toString());
    m_ctx->fillJobCombo(m_job, args.value("job_id").toString(), true);
    refreshList();
}

bool LettersPage::canClose() {
    if (!m_dirty) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved letter"), tr("Save the current letter first?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Discard) {
        revert();
        return true;
    }
    // Save: keep the window open until the engine confirms, then close again (nothing is dirty by then).
    if (answer == QMessageBox::Save) saveLetter([this] { window()->close(); });
    return false;
}

void LettersPage::setDirty(bool dirty) {
    if (m_loading) return;
    m_dirty = dirty && !m_editor->toPlainText().trimmed().isEmpty();
    m_save->setEnabled(m_dirty);
}

void LettersPage::resolveUnsaved(std::function<void()> next) {
    if (!m_dirty) return next();
    const auto answer = QMessageBox::question(this, tr("Unsaved letter"), tr("Save the current letter first?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return saveLetter(std::move(next));
    if (answer == QMessageBox::Discard) {
        revert();
        next();
    }
}

void LettersPage::revert() {
    if (m_letterId.isEmpty()) {
        m_loading = true;
        m_editor->clear();
        m_meta->setText(tr("New letter — not saved yet"));
        m_loading = false;
    } else {
        openLetter(m_letterId);
    }
    m_dirty = false;
    m_save->setEnabled(false);
}

void LettersPage::generate() {
    const QString pid = m_profile->currentData().toString();
    if (pid.isEmpty()) {
        m_ctx->toast(tr("Create a profile first."), AppContext::ToastKind::Warning);
        return;
    }
    resolveUnsaved([this] { runGenerate(); });
}

void LettersPage::runGenerate() {
    const QString pid = m_profile->currentData().toString();
    if (pid.isEmpty()) return;
    QJsonObject params{{"profile_id", pid},
                       {"job_id", m_job->currentData().toString()},
                       {"tone", m_tone->currentData().toString()},
                       {"hiring_manager", m_manager->text().trimmed()},
                       {"availability", m_availability->text().trimmed().isEmpty() ? tr("right away") : m_availability->text().trimmed()},
                       {"personal_note", m_note->text().trimmed()}};
    const QString fc = m_fairChance->currentData().toString();
    if (fc != "auto") params.insert("fair_chance_line", fc == "include");
    m_ctx->bridge()->call("letter.generate", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Writing letter"), e);
        const QJsonObject out = r.toObject();
        m_loading = true;
        ++m_session;
        m_letterId = out.value("letter").toObject().value("id").toString();
        m_editor->setPlainText(out.value("text").toString());
        QString meta = tr("%1 words · %2 examples from experience").arg(out.value("word_count").toInt()).arg(out.value("evidence_count").toInt());
        if (!out.value("match_score").isNull()) meta += tr(" · keyword match %1%").arg(out.value("match_score").toInt());
        m_meta->setText(meta);
        m_loading = false;
        setDirty(false);
        refreshList();
        if (out.value("evidence_count").toInt() == 0)
            m_ctx->toast(tr("Add experience bullets to the profile for a stronger letter."), AppContext::ToastKind::Warning);
    }, this);
}

void LettersPage::saveLetter(std::function<void()> then) {
    const QString text = m_editor->toPlainText();
    QJsonObject params{{"letter_id", m_letterId}, {"text", text}};
    if (m_letterId.isEmpty()) {
        // A hand-written letter: the engine creates it with this text in the same call.
        const QString pid = m_profile->currentData().toString();
        if (pid.isEmpty()) return m_ctx->toast(tr("Pick a profile for this letter first."), AppContext::ToastKind::Warning);
        params.insert("profile_id", pid);
        params.insert("job_id", m_job->currentData().toString());
    }
    const int session = m_session;
    m_ctx->bridge()->call("letter.save", params, [this, text, session, then](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Saving letter"), e);
        if (session == m_session) {  // still the same letter on screen
            if (m_letterId.isEmpty()) m_letterId = r.toObject().value("id").toString();
            if (m_editor->toPlainText() == text) {  // nothing typed since the save was sent
                m_dirty = false;
                m_save->setEnabled(false);
            }
        }
        m_ctx->toast(tr("Letter saved."), AppContext::ToastKind::Success);
        refreshList();
        if (then) then();
    }, this);
}

void LettersPage::refreshList() {
    m_ctx->bridge()->call("letter.list", [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        QSignalBlocker block(m_list);
        m_list->clear();
        for (const QJsonValue& v : r.toArray()) {
            const QJsonObject l = v.toObject();
            const QDateTime when = QDateTime::fromString(l.value("updated_at").toString(), Qt::ISODate).toLocalTime();
            auto* item = new QListWidgetItem(l.value("title").toString() + "\n" + QLocale().toString(when, QLocale::ShortFormat), m_list);
            item->setData(Qt::UserRole, l.value("id").toString());
            item->setIcon(Theme::instance()->icon("envelope-paper"));
            if (l.value("id").toString() == m_letterId) m_list->setCurrentItem(item);
        }
        if (m_letterId.isEmpty() && !m_dirty && m_editor->toPlainText().isEmpty() && !r.toArray().isEmpty())
            openLetter(r.toArray().first().toObject().value("id").toString());
        if (r.toArray().isEmpty()) {
            auto* hint = new QListWidgetItem(tr("No letters yet."), m_list);
            hint->setFlags(Qt::NoItemFlags);
        }
    }, this);
}

void LettersPage::openLetter(const QString& id) {
    m_ctx->bridge()->call("letter.get", {{"letter_id", id}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Opening letter"), e);
        const QJsonObject l = r.toObject();
        ++m_session;
        m_loading = true;
        m_letterId = l.value("id").toString();
        m_editor->setPlainText(l.value("text").toString());
        m_meta->setText(l.value("title").toString());
        m_ctx->fillProfileCombo(m_profile, l.value("profile_id").toString());
        m_ctx->fillJobCombo(m_job, l.value("job_id").toString(), true);
        m_loading = false;
        setDirty(false);
    }, this);
}

void LettersPage::exportAs(const QString& format) {
    const QString text = m_editor->toPlainText();
    if (text.trimmed().isEmpty()) return;
    QString dir = QSettings().value("ui/lastExportDir").toString();
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString base = m_profile->currentText().section(" — ", 0, 0) + " " + tr("Cover Letter");
    base.remove(QRegularExpression(R"([\\/:*?"<>|])"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export cover letter"), QDir(dir).filePath(base + "." + format),
                                                      format == "pdf" ? tr("PDF (*.pdf)") : format == "docx" ? tr("Word (*.docx)") : tr("Text (*.txt)"));
    if (path.isEmpty()) return;
    QSettings().setValue("ui/lastExportDir", QFileInfo(path).absolutePath());
    if (format == "pdf") {
        if (!ResumePage::writePdf(path, text, true))
            return m_ctx->toast(tr("Couldn't write %1 — check the folder and that the file isn't open.")
                                    .arg(QDir::toNativeSeparators(path)), AppContext::ToastKind::Error);
        m_ctx->toast(tr("Saved %1").arg(QDir::toNativeSeparators(path)), AppContext::ToastKind::Success);
        return;
    }
    // The engine exports what is saved, so save edits first.
    auto doExport = [this, format, path] {
        m_ctx->bridge()->call("letter.export", {{"letter_id", m_letterId}, {"format", format}, {"path", path}},
                              [this](const QJsonValue& r, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Export"), e);
            m_ctx->toast(tr("Saved %1").arg(QDir::toNativeSeparators(r.toString())), AppContext::ToastKind::Success);
        }, this);
    };
    if (m_letterId.isEmpty()) {
        m_ctx->toast(tr("Save the letter first, then export."), AppContext::ToastKind::Warning);
        return;
    }
    if (m_dirty) {
        m_ctx->bridge()->call("letter.save", {{"letter_id", m_letterId}, {"text", text}}, [this, doExport](const QJsonValue&, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Saving letter"), e);
            m_dirty = false;
            m_save->setEnabled(false);
            doExport();
        }, this);
    } else {
        doExport();
    }
}
