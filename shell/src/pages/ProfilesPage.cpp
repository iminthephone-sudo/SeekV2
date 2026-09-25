#include "pages/ProfilesPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "core/WebSessions.h"
#include "ui/BrowserImportDialog.h"
#include "pages/SectionEditor.h"
#include "pages/WritingHelp.h"

namespace {

QString joinList(const QJsonValue& v, const QString& sep) {
    QStringList out;
    for (const QJsonValue& x : v.toArray()) out << x.toString();
    return out.join(sep);
}

QJsonArray splitList(const QString& text, QChar sep) {
    QJsonArray out;
    for (const QString& part : text.split(sep)) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

QWidget* scrollWrap(QWidget* inner) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

}  // namespace

ProfilesPage::ProfilesPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);

    auto* headerTools = new QWidget;
    auto* ht = new QHBoxLayout(headerTools);
    ht->setContentsMargins(0, 0, 0, 0);
    auto* newBtn = ui::button(tr("New profile"), "plus-lg", true);
    ht->addWidget(newBtn);
    col->addWidget(ui::pageHeader(tr("Profiles"),
                                  tr("Each participant can have several profiles aimed at different kinds of work. "
                                     "Everything is stored only on this computer."),
                                  headerTools));

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    col->addWidget(split, 1);

    // -- left: participants & profiles -------------------------------------------------
    auto* listCard = new Card;
    listCard->setMinimumWidth(250);
    listCard->setMaximumWidth(360);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search participants or profiles"));
    m_search->setClearButtonEnabled(true);
    listCard->body()->addWidget(m_search);
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->setRootIsDecorated(false);
    listCard->body()->addWidget(m_tree, 1);
    auto* listTools = new QHBoxLayout;
    auto* dup = ui::flatButton("copy", tr("Duplicate profile (e.g. to aim at a different kind of work)"));
    auto* del = ui::flatButton("trash", tr("Delete profile"));
    listTools->addWidget(ui::label(tr("Tip: duplicate a profile to tailor it."), "Muted"), 1);
    listTools->addWidget(dup);
    listTools->addWidget(del);
    listCard->body()->addLayout(listTools);
    split->addWidget(listCard);

    // -- right: editor ---------------------------------------------------------------------
    auto* editorCard = new Card;
    m_editor = editorCard;
    auto* top = new QHBoxLayout;
    m_editorTitle = ui::label(tr("Select or create a profile"), "SectionTitle");
    m_activeBadge = ui::badge(tr("Active"), "good");
    m_activeBadge->hide();
    m_setActive = ui::button(tr("Make active"), "check-circle");
    m_revert = ui::button(tr("Revert"));
    m_save = ui::button(tr("Save"), "save", true);
    m_save->setShortcut(QKeySequence::Save);
    top->addWidget(m_editorTitle, 1);
    top->addWidget(m_activeBadge);
    top->addWidget(m_setActive);
    top->addWidget(m_revert);
    top->addWidget(m_save);
    editorCard->body()->addLayout(top);

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    editorCard->body()->addWidget(m_tabs, 1);
    split->addWidget(editorCard);
    split->setStretchFactor(1, 1);

    // Basics
    auto* basics = new QWidget;
    auto* bgrid = new QHBoxLayout(basics);
    bgrid->setContentsMargins(0, 10, 8, 0);
    bgrid->setSpacing(24);
    auto* formA = new QFormLayout;
    auto* formB = new QFormLayout;
    for (QFormLayout* f : {formA, formB}) {
        f->setVerticalSpacing(10);
        f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    }
    auto line = [this](const QString& key, const QString& placeholder) {
        auto* e = new QLineEdit;
        e->setPlaceholderText(placeholder);
        connect(e, &QLineEdit::textEdited, this, [this] { setDirty(true); });
        m_fields.insert(key, e);
        return e;
    };
    formA->addRow(tr("Profile name"), line("name", tr("e.g. Warehouse focus")));
    formA->addRow(tr("Participant"), line("participant", tr("Who this profile belongs to")));
    formA->addRow(tr("Full name"), line("contact.full_name", tr("As it should appear on the resume")));
    formA->addRow(tr("Phone"), line("contact.phone", tr("(555) 123-4567")));
    formA->addRow(tr("Email"), line("contact.email", tr("name@example.org")));
    formA->addRow(tr("City"), line("contact.city", tr("City")));
    formA->addRow(tr("State"), line("contact.state", tr("State")));
    formA->addRow(tr("LinkedIn"), line("contact.linkedin", tr("optional")));
    formA->addRow(tr("Website"), line("contact.website", tr("optional")));
    formB->addRow(tr("Headline"), line("headline", tr("e.g. Forklift-Certified Warehouse Associate")));
    formB->addRow(tr("Target roles"), line("target_roles", tr("Comma separated: Line Cook, Prep Cook")));
    m_summary = new QPlainTextEdit;
    m_summary->setPlaceholderText(tr("2–3 sentences: strengths, experience, certifications, and the work wanted."));
    m_summary->setMinimumHeight(110);
    formB->addRow(tr("Summary"), m_summary);
    auto* summaryHelp = ui::button(tr("Write with spaCy"), "magic");
    summaryHelp->setToolTip(tr("Drafts a summary from this profile (and a posting), and reviews the current one."));
    auto* summaryTools = new QHBoxLayout;
    summaryTools->addWidget(summaryHelp);
    summaryTools->addStretch(1);
    formB->addRow(QString(), summaryTools);
    connect(summaryHelp, &QPushButton::clicked, this, [this] {
        if (m_currentId.isEmpty()) return;
        auto* dlg = new SummaryHelpDialog(m_ctx, m_currentId, collect(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &SummaryHelpDialog::chosen, this, [this](const QString& text) {
            m_summary->setPlainText(text);  // marks the profile dirty; staff still review and save
            m_summary->setFocus();
        });
        dlg->open();
    });
    m_template = new QComboBox;
    m_template->addItem(tr("Classic (serif, centered)"), "classic");
    m_template->addItem(tr("Modern (clean, left-aligned)"), "modern");
    m_template->addItem(tr("Compact (fits more on one page)"), "compact");
    formB->addRow(tr("Template"), m_template);
    m_includeRefs = new QCheckBox(tr("Print references on the resume"));
    m_refsOnRequest = new QCheckBox(tr("Otherwise show \"References available upon request\""));
    formB->addRow(QString(), m_includeRefs);
    formB->addRow(QString(), m_refsOnRequest);
    m_notes = new QPlainTextEdit;
    m_notes->setPlaceholderText(tr("Staff notes — never printed or exported."));
    m_notes->setMaximumHeight(90);
    formB->addRow(tr("Notes"), m_notes);
    bgrid->addLayout(formA, 1);
    bgrid->addLayout(formB, 1);
    m_tabs->addTab(scrollWrap(basics), tr("Basics"));
    connect(m_summary, &QPlainTextEdit::textChanged, this, [this] { setDirty(true); });
    connect(m_notes, &QPlainTextEdit::textChanged, this, [this] { setDirty(true); });
    connect(m_template, &QComboBox::currentIndexChanged, this, [this] { setDirty(true); });
    connect(m_includeRefs, &QCheckBox::toggled, this, [this] { setDirty(true); });
    connect(m_refsOnRequest, &QCheckBox::toggled, this, [this] { setDirty(true); });

    // Skills
    auto* skills = new QWidget;
    auto* sl = new QHBoxLayout(skills);
    sl->setContentsMargins(0, 10, 0, 0);
    sl->setSpacing(18);
    auto* sLeft = new QVBoxLayout;
    sLeft->addWidget(ui::label(tr("One skill per line. Put the strongest and most relevant first."), "Muted"));
    m_skills = new QPlainTextEdit;
    m_skills->setPlaceholderText(tr("Forklift\nCustomer service\nServSafe\nInventory\nTeamwork"));
    connect(m_skills, &QPlainTextEdit::textChanged, this, [this] { setDirty(true); });
    sLeft->addWidget(m_skills, 1);
    sl->addLayout(sLeft, 1);
    auto* sRight = new QVBoxLayout;
    auto* suggestBtn = ui::button(tr("Suggest skills from experience"), "magic");
    sRight->addWidget(suggestBtn);
    sRight->addWidget(ui::label(tr("spaCy reads the bullets, certifications and training and finds skills that are "
                                   "demonstrated but not listed yet. Click one to add it."), "Muted"));
    auto* chipHost = new QWidget;
    m_suggestions = new FlowLayout(chipHost);
    sRight->addWidget(chipHost);
    sRight->addStretch(1);
    sl->addLayout(sRight, 1);
    m_tabs->addTab(skills, tr("Skills"));
    connect(suggestBtn, &QPushButton::clicked, this, &ProfilesPage::suggestSkills);

    // List sections
    auto addSection = [this](const QString& key, const QString& tab, const QString& titleField,
                             const QString& subField, QList<FieldSpec> fields, const QString& hint) {
        auto* ed = new SectionEditor(key, titleField, subField, std::move(fields), hint);
        connect(ed, &SectionEditor::changed, this, [this] { setDirty(true); });
        m_sections.insert(key, ed);
        if (QHBoxLayout* tools = ed->fieldTools("bullets")) {
            auto* help = ui::button(tr("Improve duties with spaCy"), "magic");
            help->setToolTip(tr("Rewrites weak bullets and suggests typical duties for this job title."));
            tools->addWidget(help);
            tools->addStretch(1);
            connect(help, &QPushButton::clicked, this, [this, ed, titleField] {
                const QJsonObject entry = ed->currentEntry();
                const QString entryId = entry.value("id").toString();
                if (entry.isEmpty()) return m_ctx->toast(tr("Add or select an entry first."));
                QStringList bullets;
                for (const QJsonValue& b : entry.value("bullets").toArray()) bullets << b.toString();
                const QString end = entry.value("end").toString().trimmed().toLower();
                const bool current = end.isEmpty() || end == "present" || end == "current" || end == "now";
                auto* dlg = new DutiesHelpDialog(m_ctx, entry.value(titleField).toString(), bullets, current, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                connect(dlg, &DutiesHelpDialog::applied, this, [this, ed, entryId](const QStringList& lines) {
                    // Apply to the entry the dialog was opened for, even if the list moved meanwhile.
                    ed->selectEntryById(entryId);
                    if (ed->currentEntry().value("id").toString() != entryId)
                        return m_ctx->toast(tr("That entry is no longer here, so nothing was changed."),
                                            AppContext::ToastKind::Warning);
                    ed->setCurrentField("bullets", QJsonArray::fromStringList(lines));
                });
                dlg->open();
            });
        }
        m_tabs->addTab(ed, tab);
    };
    addSection("experience", tr("Experience"), "title", "employer",
               {{"title", tr("Job title"), tr("e.g. Line Cook")},
                {"employer", tr("Employer / program"), tr("Company, crew or program name")},
                {"location", tr("Location"), tr("City, State")},
                {"start", tr("Start"), tr("e.g. Jan 2021 or 2021")},
                {"end", tr("End"), tr("e.g. 2023 or Present")},
                {"bullets", tr("Bullets"), tr("One accomplishment per line. Start with a verb, add numbers:\n"
                                               "Prepared 400+ meals daily following food safety standards"), true}},
               tr("Paid jobs, program work, crew assignments and apprenticeships all count."));
    addSection("certifications", tr("Certifications"), "name", "issuer",
               {{"name", tr("Certification"), tr("e.g. Forklift Certification, ServSafe, OSHA 10")},
                {"issuer", tr("Issued by"), tr("Organization")},
                {"date", tr("Date"), tr("e.g. 2023")},
                {"expires", tr("Expires"), tr("optional")}},
               QString());
    addSection("training", tr("Training && programs"), "name", "provider",
               {{"name", tr("Program"), tr("e.g. Pre-Apprenticeship Construction Program")},
                {"provider", tr("Provider"), tr("School, nonprofit or agency")},
                {"hours", tr("Hours"), tr("optional")},
                {"date", tr("Completed"), tr("e.g. 2024")},
                {"details", tr("Details"), tr("What was learned or practiced"), true}},
               tr("Vocational, reentry, job-readiness and college programs. Great for filling gaps."));
    addSection("education", tr("Education"), "credential", "school",
               {{"credential", tr("Credential"), tr("e.g. GED, High School Diploma, Certificate")},
                {"field", tr("Field"), tr("optional")},
                {"school", tr("School"), tr("School or program")},
                {"location", tr("Location"), tr("optional")},
                {"start", tr("Start"), tr("optional")},
                {"end", tr("Completed"), tr("e.g. 2021")},
                {"details", tr("Details"), tr("Honors, coursework"), true}},
               QString());
    addSection("volunteer", tr("Volunteer"), "role", "organization",
               {{"role", tr("Role"), tr("e.g. Food Bank Volunteer")},
                {"organization", tr("Organization"), tr("Organization")},
                {"start", tr("Start"), QString()},
                {"end", tr("End"), QString()},
                {"bullets", tr("Bullets"), tr("One per line"), true}},
               QString());
    addSection("references", tr("References"), "name", "relationship",
               {{"name", tr("Name"), QString()},
                {"relationship", tr("Relationship"), tr("e.g. Program instructor, Former supervisor")},
                {"phone", tr("Phone"), QString()},
                {"email", tr("Email"), QString()}},
               tr("Always ask references first."));

    m_tabs->addTab(buildSitesTab(), tr("Job sites"));

    // -- wiring ------------------------------------------------------------------------------
    connect(newBtn, &QPushButton::clicked, this, &ProfilesPage::newProfile);
    connect(dup, &QPushButton::clicked, this, &ProfilesPage::duplicateProfile);
    connect(del, &QPushButton::clicked, this, &ProfilesPage::deleteProfile);
    connect(m_save, &QPushButton::clicked, this, [this] { save(); });
    connect(m_revert, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()) loadProfile(m_currentId);
    });
    connect(m_setActive, &QPushButton::clicked, this, [this] { m_ctx->setActiveProfile(m_currentId); });
    connect(m_search, &QLineEdit::textChanged, this, &ProfilesPage::rebuildTree);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        const QString id = item->data(0, Qt::UserRole).toString();
        if (id.isEmpty() || id == m_currentId) return;
        // Keep the open profile highlighted until it's replaced (and when the question is cancelled). Deferred:
        // rebuilding the tree inside its own currentItemChanged would leave the clicked row selected.
        QTimer::singleShot(0, this, &ProfilesPage::rebuildTree);
        resolveUnsaved([this, id] { loadProfile(id); });
    });
    connect(ctx, &AppContext::profilesChanged, this, &ProfilesPage::rebuildTree);
    connect(Theme::instance(), &Theme::changed, this, &ProfilesPage::rebuildTree);  // re-tint list icons
    connect(ctx, &AppContext::activeProfileChanged, this, [this] {
        m_activeBadge->setVisible(m_currentId == m_ctx->activeProfileId());
        m_setActive->setVisible(m_currentId != m_ctx->activeProfileId());
    });

    m_editor->setEnabled(false);
    setDirty(false);
}

void ProfilesPage::activate(const QVariantMap& args) {
    const QString id = args.value("profile_id").toString();
    if (!id.isEmpty() && id != m_currentId) resolveUnsaved([this, id] { loadProfile(id); });
    // Reload the open profile when coming back: the Optimizer (or another page) may have changed it, and a save
    // from a stale editor would silently undo that.
    else if (!m_currentId.isEmpty() && !m_dirty) loadProfile(m_currentId);
    else if (m_currentId.isEmpty() && !m_ctx->activeProfileId().isEmpty()) loadProfile(m_ctx->activeProfileId());
    if (args.value("new").toBool()) newProfile();
}

bool ProfilesPage::canClose() {
    if (!m_dirty) return true;
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"), tr("Save changes to “%1” first?").arg(m_fields.value("name")->text()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Discard) {
        setDirty(false);
        loadProfile(m_currentId);  // put the saved version back on screen
        return true;
    }
    // Save: keep the window open until the engine confirms, then close again (nothing is dirty by then).
    if (answer == QMessageBox::Save) save([this] { window()->close(); });
    return false;
}

void ProfilesPage::rebuildTree() {
    const QString filter = m_search->text().trimmed();
    QSignalBlocker block(m_tree);
    m_tree->clear();
    QMap<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* select = nullptr;
    for (const QJsonValue& v : m_ctx->profiles()) {
        const QJsonObject p = v.toObject();
        const QString who = p.value("participant").toString().isEmpty() ? tr("Unassigned") : p.value("participant").toString();
        const QString name = p.value("name").toString();
        if (!filter.isEmpty() && !who.contains(filter, Qt::CaseInsensitive) && !name.contains(filter, Qt::CaseInsensitive) &&
            !p.value("full_name").toString().contains(filter, Qt::CaseInsensitive))
            continue;
        QTreeWidgetItem* group = groups.value(who);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {who});
            group->setFlags(Qt::ItemIsEnabled);
            QFont f = group->font(0);
            f.setWeight(QFont::DemiBold);
            group->setFont(0, f);
            group->setIcon(0, Theme::instance()->icon("people"));
            groups.insert(who, group);
        }
        auto* item = new QTreeWidgetItem(group, {name});
        item->setData(0, Qt::UserRole, p.value("id").toString());
        const QString tip = tr("%1 jobs · %2 skills").arg(p.value("experience_count").toInt()).arg(p.value("skills_count").toInt());
        item->setToolTip(0, tip);
        item->setIcon(0, Theme::instance()->icon(p.value("active").toBool() ? "check-circle" : "person-vcard",
                                                 p.value("active").toBool() ? Theme::instance()->p().accent : QColor()));
        if (p.value("id").toString() == m_currentId) select = item;
    }
    m_tree->expandAll();
    if (select) m_tree->setCurrentItem(select);
    if (m_ctx->profiles().isEmpty()) {
        auto* hint = new QTreeWidgetItem(m_tree, {tr("No profiles yet — click “New profile”.")});
        hint->setFlags(Qt::NoItemFlags);
    }
}

void ProfilesPage::loadProfile(const QString& id) {
    m_ctx->bridge()->call("profile.get", {{"profile_id", id}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Opening profile"), e);
        populate(r.toObject());
    }, this);
}

void ProfilesPage::populate(const QJsonObject& p) {
    m_loading = true;
    m_profile = p;
    m_currentId = p.value("id").toString();
    const QJsonObject contact = p.value("contact").toObject();
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        const QString key = it.key();
        QString value;
        if (key.startsWith("contact.")) value = contact.value(key.mid(8)).toString();
        else if (key == "target_roles") value = joinList(p.value(key), ", ");
        else value = p.value(key).toString();
        it.value()->setText(value);
    }
    m_summary->setPlainText(p.value("summary").toString());
    m_notes->setPlainText(p.value("notes").toString());
    m_skills->setPlainText(joinList(p.value("skills"), "\n"));
    const QJsonObject opts = p.value("options").toObject();
    m_template->setCurrentIndex(qMax(0, m_template->findData(opts.value("template").toString("classic"))));
    m_includeRefs->setChecked(opts.value("include_references").toBool());
    m_refsOnRequest->setChecked(opts.value("references_on_request").toBool(true));
    for (auto it = m_sections.begin(); it != m_sections.end(); ++it) it.value()->setEntries(p.value(it.key()).toArray());
    refreshSites();
    ui::clearLayout(m_suggestions);
    const QString who = p.value("participant").toString();
    m_editorTitle->setText((who.isEmpty() ? QString() : who + " — ") + p.value("name").toString());
    m_activeBadge->setVisible(m_currentId == m_ctx->activeProfileId());
    m_setActive->setVisible(m_currentId != m_ctx->activeProfileId());
    m_editor->setEnabled(true);
    m_loading = false;
    setDirty(false);
    rebuildTree();
}

QJsonObject ProfilesPage::collect() const {
    QJsonObject changes;
    QJsonObject contact;
    for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
        const QString key = it.key();
        const QString value = it.value()->text().trimmed();
        if (key.startsWith("contact.")) contact.insert(key.mid(8), value);
        else if (key == "target_roles") changes.insert(key, splitList(value, ','));
        else changes.insert(key, value);
    }
    changes.insert("contact", contact);
    changes.insert("summary", m_summary->toPlainText().trimmed());
    changes.insert("notes", m_notes->toPlainText().trimmed());
    changes.insert("skills", splitList(m_skills->toPlainText(), '\n'));
    changes.insert("options", QJsonObject{{"template", m_template->currentData().toString()},
                                          {"include_references", m_includeRefs->isChecked()},
                                          {"references_on_request", m_refsOnRequest->isChecked()}});
    for (auto it = m_sections.constBegin(); it != m_sections.constEnd(); ++it) changes.insert(it.key(), it.value()->entries());
    return changes;
}

void ProfilesPage::save(std::function<void()> then) {
    if (m_currentId.isEmpty()) return;
    m_save->setEnabled(false);
    const int serial = m_editSerial;
    const QString id = m_currentId;
    m_ctx->bridge()->call("profile.update", {{"profile_id", id}, {"changes", collect()}},
                          [this, then, serial, id](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) {
            setDirty(true);
            return m_ctx->reportError(tr("Saving profile"), e);
        }
        if (id == m_currentId && serial == m_editSerial) {
            populate(r.toObject());
        } else if (id == m_currentId) {
            // Typed more while the save was queued: keep the editor (and its dirty state), refresh the rest.
            m_profile = r.toObject();
            m_save->setEnabled(true);
        }
        m_ctx->refreshProfiles();
        m_ctx->toast(tr("Profile saved."), AppContext::ToastKind::Success);
        if (then) then();
    }, this);
}

void ProfilesPage::resolveUnsaved(std::function<void()> next) {
    if (!m_dirty) return next();
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"), tr("Save changes to “%1” first?").arg(m_fields.value("name")->text()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return save(std::move(next));  // a failed save keeps the edits and stops here
    if (answer == QMessageBox::Discard) {
        setDirty(false);
        loadProfile(m_currentId);  // the discarded edits never linger on screen
        next();
    }
}

void ProfilesPage::setDirty(bool dirty) {
    if (m_loading) return;
    m_dirty = dirty;
    if (dirty) ++m_editSerial;
    m_save->setEnabled(dirty);
    m_revert->setEnabled(dirty);
    setProperty("dirty", dirty);
}

void ProfilesPage::newProfile() { resolveUnsaved([this] { showNewProfileDialog(); }); }

void ProfilesPage::showNewProfileDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New profile"));
    auto* form = new QFormLayout(&dlg);
    auto* participant = new QLineEdit;
    participant->setPlaceholderText(tr("e.g. Marcus R. (first name + initial is fine)"));
    auto* name = new QLineEdit(tr("General"));
    name->setPlaceholderText(tr("e.g. Warehouse focus"));
    auto* fullName = new QLineEdit;
    fullName->setPlaceholderText(tr("Name printed on the resume"));
    form->addRow(tr("Participant"), participant);
    form->addRow(tr("Profile name"), name);
    form->addRow(tr("Full name"), fullName);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QJsonObject params{{"name", name->text().trimmed().isEmpty() ? tr("General") : name->text().trimmed()},
                             {"participant", participant->text().trimmed()},
                             {"data", QJsonObject{{"contact", QJsonObject{{"full_name", fullName->text().trimmed()}}}}}};
    m_ctx->bridge()->call("profile.create", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Creating profile"), e);
        populate(r.toObject());
        m_ctx->refreshProfiles();
        m_ctx->toast(tr("Profile created. Fill in the Basics, then add experience."), AppContext::ToastKind::Success);
    }, this);
}

void ProfilesPage::duplicateProfile() {
    if (m_currentId.isEmpty()) return;
    resolveUnsaved([this] { showDuplicateDialog(); });
}

void ProfilesPage::showDuplicateDialog() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Duplicate profile"), tr("Name for the copy:"), QLineEdit::Normal,
                                               m_fields.value("name")->text() + tr(" (copy)"), &ok);
    if (!ok) return;
    m_ctx->bridge()->call("profile.duplicate", {{"profile_id", m_currentId}, {"name", name}},
                          [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Duplicating profile"), e);
        populate(r.toObject());
        m_ctx->refreshProfiles();
    }, this);
}

void ProfilesPage::deleteProfile() {
    if (m_currentId.isEmpty()) return;
    if (QMessageBox::warning(this, tr("Delete profile"),
                             tr("Delete “%1”? This can't be undone.").arg(m_fields.value("name")->text()),
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    m_ctx->bridge()->call("profile.delete", {{"profile_id", m_currentId}}, [this, id = m_currentId](const QJsonValue&, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Deleting profile"), e);
        WebSessions::instance()->forget(id);  // the participant's LinkedIn/Indeed sign-ins go with the profile
        m_currentId.clear();
        m_loading = true;
        // Don't leave the deleted participant's details on screen (disabled, but readable).
        for (QLineEdit* e : std::as_const(m_fields)) e->clear();
        for (QPlainTextEdit* e : {m_summary, m_notes, m_skills}) e->clear();
        for (SectionEditor* s : std::as_const(m_sections)) s->setEntries({});
        m_profile = {};
        refreshSites();
        m_editorTitle->setText(tr("Select or create a profile"));
        m_editor->setEnabled(false);
        m_loading = false;
        setDirty(false);
        m_ctx->refreshProfiles();
        m_ctx->toast(tr("Profile deleted."));
    }, this);
}

// -- Job sites -------------------------------------------------------------------------------------
QWidget* ProfilesPage::buildSitesTab() {
    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 10, 8, 0);
    col->setSpacing(12);
    auto* intro = ui::label(
        WebSessions::available()
            ? tr("Sign this participant in to LinkedIn and Indeed so SEEK can open postings and run job searches as them. "
                 "Sign-in happens on the site's own page; SEEK never sees or stores the password. The sign-in stays in "
                 "SEEK's browser for this profile only, on this computer — sign out when finished on a shared computer.")
            : tr("This copy of SEEK was built without the built-in browser (Qt WebEngine), so it can't sign in to job "
                 "sites. Rebuild with the Qt WebEngine component to turn this on."),
        "Muted");
    intro->setWordWrap(true);
    col->addWidget(intro);
    for (const WebSessions::Site& site : WebSessions::sites()) {
        auto* card = new Card;
        auto* row = new QHBoxLayout;
        row->addWidget(ui::label(site.name, "CardTitle"));
        auto* status = ui::badge(tr("Not signed in"));
        m_siteStatus.insert(site.key, status);
        row->addWidget(status);
        row->addStretch(1);
        auto* signIn = ui::button(tr("Sign in"), "box-arrow-up-right", true);
        auto* check = ui::button(tr("Check"), "arrow-repeat");
        check->setToolTip(tr("Ask %1 whether this profile is still signed in.").arg(site.name));
        auto* signOut = ui::button(tr("Sign out"), "x-lg");
        m_siteSignOut.insert(site.key, signOut);
        for (QPushButton* b : {signIn, check, signOut}) {
            b->setEnabled(WebSessions::available());
            row->addWidget(b);
        }
        card->body()->addLayout(row);
        col->addWidget(card);
        const QString key = site.key;
        connect(signIn, &QPushButton::clicked, this, [this, key] {
            if (m_currentId.isEmpty()) return;
            auto* dlg = new SiteSignInDialog(key, m_currentId, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &SiteSignInDialog::finishedWith, this, [this, key](const QString& status) {
                recordSiteStatus(key, status);
            });
            dlg->open();
        });
        connect(check, &QPushButton::clicked, this, [this, key, check] {
            if (m_currentId.isEmpty()) return;
            check->setEnabled(false);
            m_siteStatus.value(key)->setText(tr("Checking…"));
            WebSessions::instance()->check(m_currentId, key, this, [this, key, check, id = m_currentId](const QString& status) {
                check->setEnabled(true);
                if (id == m_currentId) recordSiteStatus(key, status);
                else refreshSites();
            });
        });
        connect(signOut, &QPushButton::clicked, this, [this, key] {
            if (m_currentId.isEmpty()) return;
            WebSessions::instance()->signOut(m_currentId, key);
            recordSiteStatus(key, QStringLiteral("signed_out"));
        });
    }
    col->addStretch(1);
    return page;
}

void ProfilesPage::refreshSites() {
    const QJsonObject accounts = m_profile.value("accounts").toObject();
    for (const WebSessions::Site& site : WebSessions::sites()) {
        const QJsonObject a = accounts.value(site.key).toObject();
        const QString status = a.value("status").toString();
        QLabel* badge = m_siteStatus.value(site.key);
        const QDateTime when = QDateTime::fromString(a.value("checked_at").toString(), Qt::ISODate).toLocalTime();
        const QString at = when.isValid() ? QLocale().toString(when.date(), QLocale::ShortFormat) : QString();
        if (status == "signed_in") {
            badge->setText(at.isEmpty() ? tr("Signed in") : tr("Signed in · checked %1").arg(at));
            badge->setProperty("kind", "good");
        } else if (status == "unknown") {
            badge->setText(tr("Couldn't tell — try Check"));
            badge->setProperty("kind", "warn");
        } else {
            badge->setText(tr("Not signed in"));
            badge->setProperty("kind", QString());
        }
        ui::repolish(badge);
        // Always available: a sign-in can exist even when SEEK never recorded it (e.g. the dialog was cancelled).
        m_siteSignOut.value(site.key)->setEnabled(WebSessions::available() && !m_currentId.isEmpty());
    }
}

void ProfilesPage::recordSiteStatus(const QString& site, const QString& status) {
    const QString id = m_currentId;
    m_ctx->bridge()->call("profile.set_account", {{"profile_id", id}, {"site", site}, {"status", status}},
                          [this, id, site, status](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Saving sign-in status"), e);
        if (id != m_currentId) return;
        m_profile.insert("accounts", r.toObject());  // sign-in status doesn't make the editor dirty
        refreshSites();
        const QString name = WebSessions::site(site).name;
        if (status == "signed_in") m_ctx->toast(tr("Signed in to %1.").arg(name), AppContext::ToastKind::Success);
        else if (status == "signed_out") m_ctx->toast(tr("Signed out of %1.").arg(name));
        else m_ctx->toast(tr("%1 didn't say whether this profile is signed in. Try Check again.").arg(name),
                          AppContext::ToastKind::Warning);
    }, this);
}

void ProfilesPage::suggestSkills() {
    if (m_currentId.isEmpty()) return;
    auto run = [this] {
        m_ctx->bridge()->call("resume.analyze", {{"profile_id", m_currentId}}, [this](const QJsonValue& r, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Suggesting skills"), e);
            ui::clearLayout(m_suggestions);
            const QJsonArray suggested = r.toObject().value("suggested_skills").toArray();
            if (suggested.isEmpty()) {
                m_suggestions->addWidget(ui::label(tr("No new skills found — nice, the list is complete."), "Muted"));
                return;
            }
            for (const QJsonValue& v : suggested) {
                auto* b = new QPushButton("+ " + v.toString());
                b->setCursor(Qt::PointingHandCursor);
                connect(b, &QPushButton::clicked, this, [this, b, skill = v.toString()] {
                    addSkill(skill);
                    b->deleteLater();
                });
                m_suggestions->addWidget(b);
            }
        }, this);
    };
    // Analyse what's on screen, not what was last saved.
    if (m_dirty) save(run);
    else run();
}

void ProfilesPage::addSkill(const QString& skill) {
    QString text = m_skills->toPlainText().trimmed();
    m_skills->setPlainText(text.isEmpty() ? skill : text + "\n" + skill);
    setDirty(true);
}
