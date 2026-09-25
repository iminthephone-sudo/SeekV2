#include "pages/OptimizePage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

OptimizePage::OptimizePage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);
    col->addWidget(ui::pageHeader(tr("Keyword optimizer"),
                                  tr("See how well a profile covers what a posting asks for, then tailor it — honestly. "
                                     "Only add skills the participant really has.")));

    auto* pick = new Card;
    auto* row = new QHBoxLayout;
    m_profile = new QComboBox;
    m_job = new QComboBox;
    m_profile->setMinimumWidth(240);
    m_job->setMinimumWidth(280);
    auto* go = ui::button(tr("Analyze match"), "bullseye", true);
    row->addWidget(ui::label(tr("Profile"), "Muted"));
    row->addWidget(m_profile, 1);
    row->addSpacing(8);
    row->addWidget(ui::label(tr("Posting"), "Muted"));
    row->addWidget(m_job, 1);
    row->addWidget(go);
    pick->body()->addLayout(row);
    col->addWidget(pick);

    m_stack = new QStackedWidget;
    auto* empty = ui::label(tr("Pick a profile and a saved posting, then click “Analyze match”.\n"
                               "No postings yet? Import one on the Job postings page."), "Muted");
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* canvas = new QWidget;
    auto* grid = new QHBoxLayout(canvas);
    grid->setContentsMargins(0, 0, 6, 0);
    grid->setSpacing(14);
    scroll->setWidget(canvas);
    m_stack->addWidget(scroll);
    col->addWidget(m_stack, 1);

    // Left column: score, tips, matched keywords, bullet ranking.
    auto* left = new QVBoxLayout;
    left->setSpacing(14);
    auto* scoreCard = new Card;
    auto* scoreRow = new QHBoxLayout;
    m_score = new ScoreRing;
    m_score->setFixedSize(140, 140);
    scoreRow->addWidget(m_score);
    auto* scoreText = new QVBoxLayout;
    m_grade = ui::label(QString(), "SectionTitle");
    m_required = ui::label(QString(), "Muted");
    scoreText->addWidget(m_grade);
    scoreText->addWidget(m_required);
    m_tips = new QVBoxLayout;
    m_tips->setSpacing(4);
    scoreText->addLayout(m_tips);
    scoreText->addStretch(1);
    scoreRow->addLayout(scoreText, 1);
    scoreCard->body()->addLayout(scoreRow);
    left->addWidget(scoreCard);

    auto* matchedCard = new Card;
    matchedCard->body()->addWidget(ui::label(tr("Already covered"), "CardTitle"));
    auto* matchedHost = new QWidget;
    m_matched = new FlowLayout(matchedHost);
    matchedCard->body()->addWidget(matchedHost);
    left->addWidget(matchedCard);

    auto* bulletCard = new Card;
    bulletCard->body()->addWidget(ui::label(tr("Most relevant bullets"), "CardTitle"));
    bulletCard->body()->addWidget(ui::label(tr("Tailoring moves these to the top of each job."), "Muted"));
    m_bullets = new QVBoxLayout;
    m_bullets->setSpacing(6);
    bulletCard->body()->addLayout(m_bullets);
    left->addWidget(bulletCard);
    left->addStretch(1);
    grid->addLayout(left, 3);

    // Right column: the tailoring form.
    auto* right = new QVBoxLayout;
    right->setSpacing(14);
    auto* addCard = new Card;
    addCard->body()->addWidget(ui::label(tr("Shown in experience, not listed as skills"), "CardTitle"));
    addCard->body()->addWidget(ui::label(tr("Their bullets already prove these. Adding them to Skills is an easy win."), "Muted"));
    m_addList = new QVBoxLayout;
    addCard->body()->addLayout(m_addList);
    right->addWidget(addCard);

    auto* confirmCard = new Card;
    confirmCard->body()->addWidget(ui::label(tr("Missing from the profile"), "CardTitle"));
    confirmCard->body()->addWidget(ui::label(tr("Tick only what the participant genuinely has (from work, programs or "
                                                "everyday life). Anything left unticked is a training goal."), "Muted"));
    m_confirmList = new QVBoxLayout;
    confirmCard->body()->addLayout(m_confirmList);
    right->addWidget(confirmCard);

    m_wordingCard = new Card;
    m_wordingCard->body()->addWidget(ui::label(tr("Wording to consider"), "CardTitle"));
    m_wordingCard->body()->addWidget(ui::label(tr("Phrases from the posting that don't appear in the profile. If they "
                                                  "describe real experience, use the same words in a bullet or the summary."), "Muted"));
    auto* wordingHost = new QWidget;
    m_wording = new FlowLayout(wordingHost);
    m_wordingCard->body()->addWidget(wordingHost);
    right->addWidget(m_wordingCard);

    auto* applyCard = new Card;
    applyCard->body()->addWidget(ui::label(tr("Tailor"), "CardTitle"));
    m_useHeadline = new QCheckBox(tr("Use this headline:"));
    m_headline = new QLineEdit;
    m_reorder = new QCheckBox(tr("Put the most relevant skills and bullets first"));
    m_reorder->setChecked(true);
    m_asCopy = new QRadioButton(tr("Save as a new tailored profile (recommended)"));
    auto* inPlace = new QRadioButton(tr("Update the selected profile"));
    m_asCopy->setChecked(true);
    m_apply = ui::button(tr("Apply"), "magic", true);
    applyCard->body()->addWidget(m_useHeadline);
    applyCard->body()->addWidget(m_headline);
    applyCard->body()->addWidget(m_reorder);
    applyCard->body()->addWidget(m_asCopy);
    applyCard->body()->addWidget(inPlace);
    auto* applyRow = new QHBoxLayout;
    applyRow->addStretch(1);
    applyRow->addWidget(m_apply);
    applyCard->body()->addLayout(applyRow);
    right->addWidget(applyCard);
    right->addStretch(1);
    grid->addLayout(right, 2);

    connect(go, &QPushButton::clicked, this, &OptimizePage::runMatch);
    connect(m_apply, &QPushButton::clicked, this, &OptimizePage::apply);
    connect(m_useHeadline, &QCheckBox::toggled, m_headline, &QWidget::setEnabled);
    connect(ctx, &AppContext::profilesChanged, this, [this] {
        m_ctx->fillProfileCombo(m_profile, m_selectAfterRefresh);
        m_selectAfterRefresh.clear();
    });
    connect(ctx, &AppContext::jobsChanged, this, [this] { m_ctx->fillJobCombo(m_job); });
    // Only a user's own pick invalidates the result on screen.
    connect(m_profile, &QComboBox::activated, this, [this] { m_stack->setCurrentIndex(0); });
    connect(m_job, &QComboBox::activated, this, [this] { m_stack->setCurrentIndex(0); });
}

void OptimizePage::activate(const QVariantMap& args) {
    m_ctx->fillProfileCombo(m_profile, args.value("profile_id").toString());
    m_ctx->fillJobCombo(m_job, args.value("job_id").toString());
    if (args.value("run").toBool()) runMatch();
}

void OptimizePage::runMatch() {
    const QString pid = m_profile->currentData().toString();
    const QString jid = m_job->currentData().toString();
    if (pid.isEmpty() || jid.isEmpty()) {
        m_ctx->toast(tr("Choose a profile and a saved posting first."), AppContext::ToastKind::Warning);
        return;
    }
    m_ctx->bridge()->call("optimize.match", {{"profile_id", pid}, {"job_id", jid}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Matching"), e);
        showResult(r.toObject());
    }, this);
}

void OptimizePage::showResult(const QJsonObject& result) {
    m_result = result;
    m_stack->setCurrentIndex(1);
    m_score->setScore(result.value("score").toInt(), tr("keyword match"));
    m_grade->setText(result.value("grade").toString());
    m_required->setText(tr("%1 of %2 stated requirements covered")
                            .arg(result.value("required_covered").toInt())
                            .arg(result.value("required_total").toInt()));
    ui::clearLayout(m_tips);
    for (const QJsonValue& t : result.value("tips").toArray()) m_tips->addWidget(ui::label("• " + t.toString()));

    ui::clearLayout(m_matched);
    for (const QJsonValue& v : result.value("matched").toArray()) {
        const QJsonObject k = v.toObject();
        m_matched->addWidget(ui::chip(k.value("term").toString(), "matched",
                                      k.value("listed").toBool() ? tr("Listed in skills") : tr("Found in experience")));
    }
    if (result.value("matched").toArray().isEmpty()) m_matched->addWidget(ui::label(tr("Nothing yet."), "Muted"));

    m_skillChecks.clear();
    ui::clearLayout(m_addList);
    for (const QJsonValue& v : result.value("add_to_skills").toArray()) {
        auto* cb = new QCheckBox(v.toString());
        cb->setChecked(true);
        m_addList->addWidget(cb);
        m_skillChecks << cb;
    }
    if (m_skillChecks.isEmpty()) m_addList->addWidget(ui::label(tr("None — skills list is in good shape."), "Muted"));

    ui::clearLayout(m_confirmList);
    ui::clearLayout(m_wording);
    for (const QJsonValue& v : result.value("missing").toArray()) {
        const QJsonObject k = v.toObject();
        if (k.value("kind").toString() != "skill") {
            m_wording->addWidget(ui::chip(k.value("term").toString(), k.value("required").toBool() ? "required" : "suggest",
                                          k.value("advice").toString()));
            continue;
        }
        auto* cb = new QCheckBox(k.value("term").toString() + (k.value("required").toBool() ? tr("  (required)") : QString()));
        cb->setProperty("skill", k.value("term").toString());
        cb->setToolTip(k.value("advice").toString());
        m_confirmList->addWidget(cb);
        m_skillChecks << cb;
    }
    if (m_confirmList->count() == 0) m_confirmList->addWidget(ui::label(tr("No missing skills — great fit."), "Muted"));
    m_wordingCard->setVisible(m_wording->count() > 0);

    ui::clearLayout(m_bullets);
    int shown = 0;
    for (const QJsonValue& v : result.value("bullet_ranking").toArray()) {
        const QJsonObject b = v.toObject();
        if (shown++ >= 6) break;
        auto* row = new QHBoxLayout;
        const int hits = b.value("relevance").toInt();
        row->addWidget(ui::badge(QString::number(hits), hits >= 2 ? "good" : hits == 1 ? "accent" : QString()), 0, Qt::AlignTop);
        auto* text = ui::label(b.value("text").toString());
        QStringList h;
        for (const QJsonValue& x : b.value("hits").toArray()) h << x.toString();
        if (!h.isEmpty()) text->setToolTip(tr("Matches: %1").arg(h.join(", ")));
        row->addWidget(text, 1);
        m_bullets->addLayout(row);
    }

    m_headline->setText(result.value("suggested_headline").toString());
    m_useHeadline->setChecked(!result.value("title_match").toBool());
    m_headline->setEnabled(m_useHeadline->isChecked());
}

void OptimizePage::apply() {
    QJsonArray skills;
    for (QCheckBox* cb : std::as_const(m_skillChecks)) {
        if (!cb->isChecked()) continue;
        const QString s = cb->property("skill").toString();
        skills.append(s.isEmpty() ? cb->text() : s);
    }
    QJsonObject params{{"profile_id", m_profile->currentData().toString()},
                       {"job_id", m_job->currentData().toString()},
                       {"add_skills", skills},
                       {"reorder", m_reorder->isChecked()},
                       {"as_copy", m_asCopy->isChecked()}};
    if (m_useHeadline->isChecked()) params.insert("headline", m_headline->text().trimmed());
    m_apply->setEnabled(false);
    m_ctx->bridge()->call("optimize.apply", params, [this, before = m_result.value("score").toInt()](const QJsonValue& r, const BridgeError& e) {
        m_apply->setEnabled(true);
        if (e.isError()) return m_ctx->reportError(tr("Tailoring"), e);
        const QJsonObject out = r.toObject();
        const QJsonObject profile = out.value("profile").toObject();
        m_selectAfterRefresh = profile.value("id").toString();
        m_ctx->refreshProfiles();
        showResult(out.value("match").toObject());
        m_ctx->toast(tr("Saved “%1”: match went from %2% to %3%.")
                         .arg(profile.value("name").toString())
                         .arg(before)
                         .arg(out.value("match").toObject().value("score").toInt()),
                     AppContext::ToastKind::Success);
    }, this);
}
