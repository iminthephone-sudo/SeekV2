#include "pages/WritingHelp.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "ui/Widgets.h"

namespace {

QScrollArea* scrolled(QWidget* inner) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

QLabel* wrapped(const QString& text, const char* style = nullptr) {
    auto* l = ui::label(text, style);
    l->setWordWrap(true);
    return l;
}

}  // namespace

// -- Summary ----------------------------------------------------------------------------------------
SummaryHelpDialog::SummaryHelpDialog(AppContext* ctx, const QString& profileId, const QJsonObject& profileData,
                                     QWidget* parent)
    : QDialog(parent), m_ctx(ctx), m_profileId(profileId), m_data(profileData) {
    setWindowTitle(tr("Write the summary with spaCy"));
    resize(760, 640);
    auto* col = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    top->addWidget(ui::label(tr("Aim at a posting"), "Muted"));
    m_job = new QComboBox;
    m_ctx->fillJobCombo(m_job, {}, true, tr("No specific posting"));
    top->addWidget(m_job, 1);
    col->addLayout(top);

    auto* inner = new QWidget;
    auto* il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 8, 0);
    il->addWidget(ui::label(tr("Your current summary"), "CardTitle"));
    m_review = new QVBoxLayout;
    il->addLayout(m_review);
    il->addWidget(ui::label(tr("Drafts"), "CardTitle"));
    il->addWidget(wrapped(tr("Built only from what's already in the profile. Pick one, then edit it so it sounds like "
                             "the participant."), "Muted"));
    m_drafts = new QVBoxLayout;
    m_drafts->setSpacing(10);
    il->addLayout(m_drafts);
    il->addStretch(1);
    col->addWidget(scrolled(inner), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addWidget(buttons);

    connect(m_job, &QComboBox::currentIndexChanged, this, &SummaryHelpDialog::reload);
    reload();
}

void SummaryHelpDialog::reload() {
    const QJsonObject params{{"profile_id", m_profileId}, {"job_id", m_job->currentData().toString()},
                             {"text", m_data.value("summary").toString()}, {"data", m_data}};
    m_ctx->bridge()->call("assist.summary", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Writing help"), e);
        const QJsonObject res = r.toObject();
        ui::clearLayout(m_review);
        const QJsonArray review = res.value("review").toArray();
        if (review.isEmpty()) m_review->addWidget(wrapped("✓ " + tr("Looks good — clear, skill-first and the right length.")));
        for (const QJsonValue& v : review) {
            auto* row = new QHBoxLayout;
            row->addWidget(ui::badge(tr("Check"), "warn"), 0, Qt::AlignTop);
            row->addWidget(wrapped(v.toObject().value("message").toString()), 1);
            m_review->addLayout(row);
        }
        ui::clearLayout(m_drafts);
        for (const QJsonValue& v : res.value("drafts").toArray()) {
            const QJsonObject d = v.toObject();
            auto* card = new Card;
            auto* head = new QHBoxLayout;
            head->addWidget(ui::label(d.value("label").toString(), "CardTitle"), 1);
            head->addWidget(ui::label(tr("%1 words").arg(d.value("words").toString()), "Muted"));
            auto* use = ui::button(tr("Use this"), "check2", true);
            head->addWidget(use);
            card->body()->addLayout(head);
            auto* text = wrapped(d.value("text").toString());
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            card->body()->addWidget(text);
            connect(use, &QPushButton::clicked, this, [this, t = d.value("text").toString()] {
                emit chosen(t);
                accept();
            });
            m_drafts->addWidget(card);
        }
    }, this);
}

// -- Duties -----------------------------------------------------------------------------------------
DutiesHelpDialog::DutiesHelpDialog(AppContext* ctx, const QString& title, const QStringList& bullets, bool current,
                                   QWidget* parent)
    : QDialog(parent), m_ctx(ctx), m_title(title), m_bullets(bullets), m_current(current) {
    setWindowTitle(tr("Job duties — %1").arg(title.isEmpty() ? tr("untitled job") : title));
    resize(860, 700);
    auto* col = new QVBoxLayout(this);
    col->addWidget(wrapped(current ? tr("Current job: duties are written in the present tense (“Pick and pack…”).")
                                   : tr("Past job: duties are written in the past tense (“Picked and packed…”)."),
                           "Muted"));
    auto* top = new QHBoxLayout;
    top->addWidget(ui::label(tr("Also suggest duties from a posting"), "Muted"));
    m_job = new QComboBox;
    m_ctx->fillJobCombo(m_job, {}, true, tr("No posting"));
    top->addWidget(m_job, 1);
    col->addLayout(top);

    auto* inner = new QWidget;
    auto* il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 8, 0);
    il->addWidget(ui::label(tr("Your bullets"), "CardTitle"));
    il->addWidget(wrapped(tr("Ticked rewrites replace the original. You can edit any rewrite before applying."), "Muted"));
    m_mine = new QVBoxLayout;
    m_mine->setSpacing(10);
    il->addLayout(m_mine);
    il->addWidget(ui::label(tr("Suggested duties"), "CardTitle"));
    il->addWidget(wrapped(tr("Tick only what the participant really did. Replace every [#] with a real number, or "
                             "delete it."), "Muted"));
    m_suggested = new QVBoxLayout;
    m_suggested->setSpacing(6);
    il->addLayout(m_suggested);
    il->addStretch(1);
    col->addWidget(scrolled(inner), 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_apply = buttons->addButton(tr("Apply to bullets"), QDialogButtonBox::AcceptRole);
    m_apply->setProperty("accent", true);
    m_apply->setEnabled(false);  // until the rewrites arrive: applying an empty list would wipe the bullets
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit applied(result());
        accept();
    });
    col->addWidget(buttons);
    connect(m_job, &QComboBox::currentIndexChanged, this, &DutiesHelpDialog::reload);
    reload();
}

void DutiesHelpDialog::reload() {
    QJsonArray bullets;
    for (const QString& b : std::as_const(m_bullets)) bullets.append(b);
    const QJsonObject params{{"title", m_title}, {"bullets", bullets}, {"current", m_current},
                             {"job_id", m_job->currentData().toString()}};
    m_apply->setEnabled(false);
    m_ctx->bridge()->call("assist.duties", params, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Writing help"), e);
        m_apply->setEnabled(true);
        const QJsonObject res = r.toObject();
        const auto& t = Theme::instance()->p();
        ui::clearLayout(m_mine);
        m_rows.clear();
        const QJsonArray mine = res.value("bullets").toArray();
        if (mine.isEmpty()) m_mine->addWidget(wrapped(tr("No bullets yet — pick some suggestions below."), "Muted"));
        for (const QJsonValue& v : mine) {
            const QJsonObject b = v.toObject();
            Row row;
            row.original = b.value("original").toString();
            const QString rewrite = b.value("rewrite").toString();
            const bool differs = rewrite != row.original.trimmed();
            auto* card = new Card;
            card->body()->addWidget(wrapped(tr("Now: %1").arg(row.original), "Muted"));
            auto* line = new QHBoxLayout;
            row.use = new QCheckBox(tr("Use"));
            row.use->setChecked(differs);
            row.text = new QLineEdit(rewrite);
            line->addWidget(row.use);
            line->addWidget(row.text, 1);
            card->body()->addLayout(line);
            QStringList notes;
            for (const QJsonValue& c : b.value("changes").toArray()) notes << c.toString();
            if (!notes.isEmpty()) card->body()->addWidget(wrapped("• " + notes.join("\n• "), "Muted"));
            for (const QJsonValue& w : b.value("warnings").toArray()) {
                auto* l = wrapped("⚠ " + w.toString());
                l->setStyleSheet(QStringLiteral("color:%1;").arg(t.warning.name()));
                card->body()->addWidget(l);
            }
            connect(row.text, &QLineEdit::textEdited, row.use, [use = row.use] { use->setChecked(true); });
            m_rows << row;
            m_mine->addWidget(card);
        }
        ui::clearLayout(m_suggested);
        m_suggestions.clear();
        const QJsonArray sugg = res.value("suggestions").toArray();
        if (sugg.isEmpty())
            m_suggested->addWidget(wrapped(tr("No typical duties found for this job title. Try a common title such "
                                              "as “Line Cook” or pick a posting above."), "Muted"));
        QString lastSource;
        for (const QJsonValue& v : sugg) {
            const QJsonObject s = v.toObject();
            if (s.value("source").toString() != lastSource) {
                lastSource = s.value("source").toString();
                m_suggested->addWidget(ui::label(lastSource.startsWith("Posting:") ? lastSource
                                                                                   : tr("Typical for %1").arg(lastSource),
                                                 "Muted"));
            }
            Row row;
            auto* line = new QHBoxLayout;
            row.use = new QCheckBox;
            row.text = new QLineEdit(s.value("text").toString());
            line->addWidget(row.use);
            line->addWidget(row.text, 1);
            connect(row.text, &QLineEdit::textEdited, row.use, [use = row.use] { use->setChecked(true); });
            m_suggestions << row;
            m_suggested->addLayout(line);
        }
    }, this);
}

QStringList DutiesHelpDialog::result() const {
    if (m_rows.isEmpty() && !m_bullets.isEmpty()) return m_bullets;  // never replace bullets with nothing
    QStringList out;
    for (const Row& r : m_rows) {
        const QString text = (r.use->isChecked() ? r.text->text() : r.original).trimmed();
        if (!text.isEmpty()) out << text;
    }
    for (const Row& r : m_suggestions)
        if (r.use->isChecked() && !r.text->text().trimmed().isEmpty()) out << r.text->text().trimmed();
    return out;
}
