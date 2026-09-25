#include "pages/InterviewPage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

namespace {

QScrollArea* scrollOf(QWidget* inner) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

// Horizontal trait bar: label on the left, filled track, percentage on the right.
class TraitBar : public QWidget {
public:
    TraitBar(const QString& label, const QString& about, int score, QWidget* parent = nullptr)
        : QWidget(parent), m_label(label), m_score(score) {
        setToolTip(about);
        setMinimumHeight(30);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = Theme::instance()->p();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int labelW = 170, pctW = 48;
        p.setPen(t.text);
        p.drawText(QRect(0, 0, labelW, height()), Qt::AlignVCenter | Qt::AlignLeft, m_label);
        const QRectF track(labelW, height() / 2.0 - 4, width() - labelW - pctW, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(t.border);
        p.drawRoundedRect(track, 4, 4);
        const QColor c = m_score >= 75 ? t.success : m_score >= 50 ? t.accent : t.warning;
        p.setBrush(c);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * qBound(0, m_score, 100) / 100.0, 8), 4, 4);
        p.setPen(t.textMuted);
        p.drawText(QRect(width() - pctW, 0, pctW, height()), Qt::AlignVCenter | Qt::AlignRight, QString::number(m_score) + "%");
    }
private:
    QString m_label;
    int m_score;
};

const char* kPartLetters[] = {"S", "T", "A", "R", "S"};

}  // namespace

InterviewPage::InterviewPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);

    auto* tools = new QWidget;
    auto* tl = new QHBoxLayout(tools);
    tl->setContentsMargins(0, 0, 0, 0);
    m_profile = new QComboBox;
    m_profile->setMinimumWidth(220);
    m_profile->setToolTip(tr("Stories are suggested from this profile, and practice is saved to it."));
    m_job = new QComboBox;
    m_job->setMinimumWidth(220);
    m_job->setToolTip(tr("Optional: coach the Skills step against a saved posting."));
    tl->addWidget(ui::label(tr("Profile"), "Muted"));
    tl->addWidget(m_profile);
    tl->addWidget(ui::label(tr("Posting"), "Muted"));
    tl->addWidget(m_job);
    col->addWidget(ui::pageHeader(tr("Interview practice"),
                                  tr("Build strong S.T.A.R.S answers and practise the workplace assessments many "
                                     "employers use. Coaching is instant and private."), tools));

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildStarsTab(), tr("S.T.A.R.S practice"));
    m_tabs->addTab(buildAssessmentTab(), tr("Workplace assessment"));
    m_tabs->addTab(buildSavedTab(), tr("Saved practice"));
    auto tabIcons = [this] {
        const QStringList icons = {"stars", "check-circle", "journal"};
        for (int i = 0; i < icons.size(); ++i) m_tabs->setTabIcon(i, Theme::instance()->icon(icons.at(i)));
    };
    tabIcons();
    connect(Theme::instance(), &Theme::changed, this, tabIcons);
    col->addWidget(m_tabs, 1);

    connect(ctx, &AppContext::profilesChanged, this, [this] { m_ctx->fillProfileCombo(m_profile); });
    connect(ctx, &AppContext::jobsChanged, this, [this] { m_ctx->fillJobCombo(m_job, {}, true); });
    connect(m_profile, &QComboBox::currentIndexChanged, this, [this] {
        loadStoryIdeas();
        refreshSaved();
    });
    connect(ctx->bridge(), &SeekBridge::ready, this, &InterviewPage::loadBanks);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == 2) refreshSaved();
    });
}

// -- S.T.A.R.S tab ------------------------------------------------------------------------------
QWidget* InterviewPage::buildStarsTab() {
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);

    auto* listCard = new Card;
    listCard->setMinimumWidth(250);
    listCard->setMaximumWidth(340);
    listCard->body()->addWidget(ui::label(tr("Questions"), "CardTitle"));
    m_questionList = new QListWidget;
    m_questionList->setWordWrap(true);
    m_questionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listCard->body()->addWidget(m_questionList, 1);
    split->addWidget(listCard);

    auto* inner = new QWidget;
    auto* rl = new QVBoxLayout(inner);
    rl->setContentsMargins(0, 0, 8, 0);
    rl->setSpacing(12);

    auto* qCard = new Card;
    m_questionTitle = ui::label(tr("Pick a question on the left."), "SectionTitle");
    m_customQuestion = new QLineEdit;
    m_customQuestion->setPlaceholderText(tr("Type the interview question you want to practise"));
    m_customQuestion->hide();
    m_lookingFor = ui::label(QString());
    m_tip = ui::label(QString(), "Muted");
    qCard->body()->addWidget(m_questionTitle);
    qCard->body()->addWidget(m_customQuestion);
    qCard->body()->addWidget(m_lookingFor);
    qCard->body()->addWidget(m_tip);
    rl->addWidget(qCard);

    m_ideasCard = new Card;
    m_ideasCard->body()->addWidget(ui::label(tr("Story ideas from this profile"), "CardTitle"));
    m_ideasCard->body()->addWidget(ui::label(tr("Pick a real moment to build the answer around. “Use” fills in the empty "
                                                "boxes as a starting point; then make it your own."), "Muted"));
    m_ideas = new QVBoxLayout;
    m_ideas->setSpacing(6);
    m_ideasCard->body()->addLayout(m_ideas);
    m_ideasCard->hide();
    rl->addWidget(m_ideasCard);

    const QList<QPair<QString, QString>> parts = {
        {"situation", tr("Situation")}, {"task", tr("Task")}, {"action", tr("Action")},
        {"result", tr("Result")}, {"skills", tr("Skills")}};
    const QStringList hints = {
        tr("Where were you and what was going on? One or two sentences."),
        tr("What did you need to do, or what was the goal or problem?"),
        tr("What did YOU do, step by step? This is the heart of the answer."),
        tr("What happened because of it? Use numbers if you can."),
        tr("What skills does this story prove, and how would they help in this job?")};
    for (int i = 0; i < parts.size(); ++i) {
        PartEditor pe;
        pe.key = parts[i].first;
        auto* card = new Card;
        card->body()->setContentsMargins(14, 12, 14, 12);
        auto* head = new QHBoxLayout;
        auto* letter = ui::badge(QString::fromLatin1(kPartLetters[i]), "accent");
        letter->setMinimumWidth(26);
        letter->setAlignment(Qt::AlignCenter);
        head->addWidget(letter);
        head->addWidget(ui::label(parts[i].second, "CardTitle"));
        head->addWidget(ui::label("— " + hints[i], "Muted"), 1);
        pe.badge = ui::badge(QString());
        pe.badge->hide();
        head->addWidget(pe.badge);
        card->body()->addLayout(head);
        pe.edit = new QPlainTextEdit;
        pe.edit->setFixedHeight(pe.key == "action" ? 110 : 72);
        card->body()->addWidget(pe.edit);
        pe.feedback = new QVBoxLayout;
        pe.feedback->setSpacing(2);
        card->body()->addLayout(pe.feedback);
        connect(pe.edit, &QPlainTextEdit::textChanged, this, &InterviewPage::updateCounter);
        m_parts << pe;
        rl->addWidget(card);
    }

    auto* actions = new QHBoxLayout;
    m_counter = ui::label(QString(), "Muted");
    auto* clear = ui::button(tr("Clear"));
    auto* save = ui::button(tr("Coach && save"), "save");
    auto* coachBtn = ui::button(tr("Coach me"), "magic", true);
    actions->addWidget(m_counter, 1);
    actions->addWidget(clear);
    actions->addWidget(save);
    actions->addWidget(coachBtn);
    rl->addLayout(actions);

    m_resultCard = new Card;
    auto* resRow = new QHBoxLayout;
    m_score = new ScoreRing;
    m_score->setFixedSize(130, 130);
    resRow->addWidget(m_score, 0, Qt::AlignTop);
    auto* resText = new QVBoxLayout;
    m_grade = ui::label(QString(), "SectionTitle");
    resText->addWidget(m_grade);
    m_notes = new QVBoxLayout;
    m_notes->setSpacing(4);
    resText->addLayout(m_notes);
    resText->addStretch(1);
    resRow->addLayout(resText, 1);
    m_resultCard->body()->addLayout(resRow);
    auto* polishedHead = new QHBoxLayout;
    polishedHead->addWidget(ui::label(tr("Your answer, put together"), "CardTitle"), 1);
    auto* copy = ui::flatButton("clipboard", tr("Copy answer"));
    polishedHead->addWidget(copy);
    m_resultCard->body()->addLayout(polishedHead);
    m_resultCard->body()->addWidget(ui::label(tr("Read it out loud a few times. It should feel like you talking, not a script."),
                                              "Muted"));
    m_polished = new QTextBrowser;
    m_polished->setMinimumHeight(120);
    m_resultCard->body()->addWidget(m_polished);
    m_resultCard->hide();
    rl->addWidget(m_resultCard);
    rl->addStretch(1);

    m_starsScroll = scrollOf(inner);
    split->addWidget(m_starsScroll);
    split->setStretchFactor(1, 1);

    connect(m_questionList, &QListWidget::currentRowChanged, this, &InterviewPage::selectQuestion);
    connect(m_customQuestion, &QLineEdit::editingFinished, this, &InterviewPage::loadStoryIdeas);
    connect(coachBtn, &QPushButton::clicked, this, [this] { coach(false); });
    connect(save, &QPushButton::clicked, this, [this] { coach(true); });
    connect(clear, &QPushButton::clicked, this, [this] {
        for (const PartEditor& pe : std::as_const(m_parts)) pe.edit->clear();
        clearFeedback();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_polished->toPlainText());
        m_ctx->toast(tr("Answer copied."), AppContext::ToastKind::Success);
    });
    updateCounter();
    return split;
}

void InterviewPage::loadBanks() {
    m_ctx->bridge()->call("interview.stars_questions", [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Loading questions"), e);
        m_questions = r.toObject().value("questions").toArray();
        QSignalBlocker block(m_questionList);
        m_questionList->clear();
        for (const QJsonValue& v : std::as_const(m_questions)) {
            const QJsonObject q = v.toObject();
            auto* item = new QListWidgetItem(q.value("category").toString().toUpper() + "\n" + q.value("question").toString(),
                                             m_questionList);
            item->setData(Qt::UserRole, q.value("id").toString());
        }
        auto* custom = new QListWidgetItem(tr("YOUR OWN QUESTION\nPractise any question you expect to be asked"), m_questionList);
        custom->setData(Qt::UserRole, QStringLiteral("custom"));
        block.unblock();
        m_questionList->setCurrentRow(0);
    }, this);
    m_ctx->bridge()->call("interview.assessment_items", [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Loading assessment"), e);
        buildAssessmentItems(r.toObject());
    }, this);
}

QString InterviewPage::questionId() const {
    auto* item = m_questionList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void InterviewPage::selectQuestion(int row) {
    clearFeedback();
    const QString id = questionId();
    const bool custom = id == "custom";
    m_customQuestion->setVisible(custom);
    if (custom) {
        m_questionTitle->setText(tr("Your own question"));
        m_lookingFor->setText(tr("Behavioral questions (“Tell me about a time…”) all work with S.T.A.R.S."));
        m_tip->clear();
        m_customQuestion->setFocus();
    } else if (row >= 0 && row < m_questions.size()) {
        const QJsonObject q = m_questions.at(row).toObject();
        m_questionTitle->setText("“" + q.value("question").toString() + "”");
        m_lookingFor->setText(tr("<b>What they're looking for:</b> %1").arg(q.value("looking_for").toString().toHtmlEscaped()));
        m_tip->setText(tr("Tip: %1").arg(q.value("tip").toString()));
    }
    loadStoryIdeas();
}

void InterviewPage::loadStoryIdeas() {
    const QString pid = m_profile->currentData().toString();
    const QString qid = questionId();
    if (pid.isEmpty() || qid.isEmpty() || (qid == "custom" && m_customQuestion->text().trimmed().isEmpty())) {
        m_ideasCard->hide();
        return;
    }
    m_ctx->bridge()->call("interview.story_ideas",
                          {{"profile_id", pid}, {"question_id", qid}, {"custom_question", m_customQuestion->text().trimmed()}},
                          [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        ui::clearLayout(m_ideas);
        const QJsonArray ideas = r.toArray();
        m_ideasCard->setVisible(!ideas.isEmpty());
        for (const QJsonValue& v : ideas) {
            const QJsonObject idea = v.toObject();
            auto* row = new QHBoxLayout;
            QString where = idea.value("role").toString();
            if (!idea.value("org").toString().isEmpty()) where += " · " + idea.value("org").toString();
            auto* text = ui::label(idea.value("text").toString() + (where.isEmpty() ? QString() : "\n" + where));
            row->addWidget(text, 1);
            auto* use = ui::button(tr("Use"), "pencil");
            connect(use, &QPushButton::clicked, this, [this, starter = idea.value("starter").toObject()] {
                // Only fill empty boxes: never overwrite what the participant wrote.
                for (const PartEditor& pe : std::as_const(m_parts)) {
                    const QString s = starter.value(pe.key).toString();
                    if (!s.isEmpty() && pe.edit->toPlainText().trimmed().isEmpty()) pe.edit->setPlainText(s);
                }
                m_ctx->toast(tr("Starter added. Now tell the rest in your own words — especially the Task and Result."));
            });
            row->addWidget(use, 0, Qt::AlignTop);
            m_ideas->addLayout(row);
        }
    }, this);
}

void InterviewPage::updateCounter() {
    int words = 0;
    for (const PartEditor& pe : std::as_const(m_parts))
        words += pe.edit->toPlainText().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    const int seconds = qRound(words / 140.0 * 60);
    m_counter->setText(tr("%1 words · about %2 min %3 s spoken · aim for 1–2 minutes")
                           .arg(words).arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')));
}

void InterviewPage::clearFeedback() {
    for (PartEditor& pe : m_parts) {
        pe.badge->hide();
        ui::clearLayout(pe.feedback);
    }
    m_resultCard->hide();
}

void InterviewPage::coach(bool save) {
    const QString qid = questionId();
    if (qid.isEmpty()) return;
    QJsonObject answers;
    for (const PartEditor& pe : std::as_const(m_parts)) answers.insert(pe.key, pe.edit->toPlainText().trimmed());
    const QJsonObject params{{"question_id", qid}, {"answers", answers},
                             {"custom_question", m_customQuestion->text().trimmed()},
                             {"job_id", m_job->currentData().toString()},
                             {"profile_id", m_profile->currentData().toString()}, {"save", save}};
    m_ctx->bridge()->call("interview.stars_coach", params, [this, save](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->toast(e.message, AppContext::ToastKind::Warning);
        showCoaching(r.toObject());
        if (save) {
            m_ctx->toast(tr("Practice saved."), AppContext::ToastKind::Success);
            refreshSaved();
        }
    }, this);
}

void InterviewPage::showCoaching(const QJsonObject& result) {
    const auto& t = Theme::instance()->p();
    const QJsonObject parts = result.value("parts").toObject();
    for (PartEditor& pe : m_parts) {
        const QJsonObject part = parts.value(pe.key).toObject();
        const int s = part.value("score").toInt();
        pe.badge->setText(QString::number(s));
        pe.badge->setProperty("kind", s >= 80 ? "good" : s >= 50 ? "warn" : "bad");
        ui::repolish(pe.badge);
        pe.badge->show();
        ui::clearLayout(pe.feedback);
        for (const QJsonValue& g : part.value("good").toArray()) {
            auto* l = ui::label("✓ " + g.toString());
            l->setStyleSheet(QStringLiteral("color:%1;").arg(t.success.name()));
            pe.feedback->addWidget(l);
        }
        for (const QJsonValue& f : part.value("feedback").toArray()) pe.feedback->addWidget(ui::label("→ " + f.toString()));
    }
    m_score->setScore(result.value("score").toInt(), tr("answer score"));
    m_grade->setText(result.value("grade").toString());
    ui::clearLayout(m_notes);
    const int secs = result.value("speaking_seconds").toInt();
    m_notes->addWidget(ui::label(tr("%1 words · about %2 min %3 s spoken").arg(result.value("word_count").toInt())
                                     .arg(secs / 60).arg(secs % 60, 2, 10, QChar('0')), "Muted"));
    for (const QJsonValue& n : result.value("notes").toArray()) m_notes->addWidget(ui::label("• " + n.toString()));
    if (result.value("score").toInt() < 85) m_notes->addWidget(ui::label(result.value("next_step").toString(), "CardTitle"));
    m_polished->setPlainText(result.value("polished").toString());
    m_resultCard->show();
    QTimer::singleShot(0, this, [this] { m_starsScroll->ensureWidgetVisible(m_resultCard, 0, 0); });
}

// -- Assessment tab -------------------------------------------------------------------------------
QWidget* InterviewPage::buildAssessmentTab() {
    auto* inner = new QWidget;
    auto* col = new QVBoxLayout(inner);
    col->setContentsMargins(0, 8, 8, 0);
    col->setSpacing(12);

    auto* intro = new Card;
    intro->body()->addWidget(ui::label(tr("Practice workplace assessment"), "CardTitle"));
    intro->body()->addWidget(ui::label(
        tr("Many retail, warehouse, food-service and call-center applications include agree/disagree statements like "
           "these. There are no trick answers here: this is practice, so answer the way you really work. SEEK "
           "explains what employers look for and points out answers that contradict each other or sound too good to "
           "be true."), "Muted"));
    auto* opts = new QHBoxLayout;
    m_liveCoaching = new QCheckBox(tr("Coach me as I answer"));
    m_liveCoaching->setChecked(true);
    connect(m_liveCoaching, &QCheckBox::toggled, this, [this](bool on) {
        for (AssessmentRow& row : m_rows) row.coaching->setVisible(on && row.group->checkedId() > 0);
    });
    m_progress = ui::label(QString(), "Muted");
    opts->addWidget(m_liveCoaching);
    opts->addStretch(1);
    opts->addWidget(m_progress);
    intro->body()->addLayout(opts);
    col->addWidget(intro);

    m_items = new QVBoxLayout;
    m_items->setSpacing(8);
    col->addLayout(m_items);

    auto* actions = new QHBoxLayout;
    auto* reset = ui::button(tr("Start over"), "arrow-repeat");
    auto* save = ui::button(tr("Results && save"), "save");
    auto* score = ui::button(tr("See my results"), "check-circle", true);
    actions->addStretch(1);
    actions->addWidget(reset);
    actions->addWidget(save);
    actions->addWidget(score);
    col->addLayout(actions);

    m_assessResult = new Card;
    auto* head = new QHBoxLayout;
    head->addWidget(ui::label(tr("Your results"), "SectionTitle"), 1);
    m_consistency = ui::badge(QString());
    head->addWidget(m_consistency);
    m_assessResult->body()->addLayout(head);
    m_assessResult->body()->addWidget(ui::label(tr("How your answers read to an employer, by area. Hover a bar for what "
                                                   "it means."), "Muted"));
    m_traits = new QVBoxLayout;
    m_traits->setSpacing(2);
    m_assessResult->body()->addLayout(m_traits);
    m_flags = new QVBoxLayout;
    m_flags->setSpacing(6);
    m_assessResult->body()->addLayout(m_flags);
    m_assessResult->hide();
    col->addWidget(m_assessResult);
    col->addStretch(1);

    connect(score, &QPushButton::clicked, this, [this] { scoreAssessment(false); });
    connect(save, &QPushButton::clicked, this, [this] { scoreAssessment(true); });
    connect(reset, &QPushButton::clicked, this, [this] {
        for (AssessmentRow& row : m_rows) {
            row.group->setExclusive(false);
            for (QAbstractButton* b : row.group->buttons()) b->setChecked(false);
            row.group->setExclusive(true);
            row.coaching->hide();
            row.fit->hide();
        }
        m_assessResult->hide();
        updateAssessmentProgress();
    });
    m_assessScroll = scrollOf(inner);
    return m_assessScroll;
}

void InterviewPage::buildAssessmentItems(const QJsonObject& bank) {
    m_assessment = bank;
    ui::clearLayout(m_items);
    m_rows.clear();
    const QJsonArray scale = bank.value("scale").toArray();
    int n = 0;
    for (const QJsonValue& v : bank.value("items").toArray()) {
        const QJsonObject item = v.toObject();
        AssessmentRow row;
        row.id = item.value("id").toString();
        auto* card = new Card;
        card->body()->setContentsMargins(14, 10, 14, 10);
        auto* head = new QHBoxLayout;
        head->addWidget(ui::label(QStringLiteral("%1. %2").arg(++n).arg(item.value("text").toString()), "CardTitle"), 1);
        row.fit = ui::badge(QString());
        row.fit->hide();
        head->addWidget(row.fit, 0, Qt::AlignTop);
        card->body()->addLayout(head);
        auto* choices = new QHBoxLayout;
        row.group = new QButtonGroup(card);
        for (int i = 0; i < scale.size(); ++i) {
            auto* rb = new QRadioButton(scale.at(i).toString());
            row.group->addButton(rb, i + 1);
            choices->addWidget(rb);
        }
        choices->addStretch(1);
        card->body()->addLayout(choices);
        row.coaching = ui::label(tr("Coaching: %1").arg(item.value("coaching").toString()), "Muted");
        row.coaching->hide();
        card->body()->addWidget(row.coaching);
        QLabel* coaching = row.coaching;
        connect(row.group, &QButtonGroup::idClicked, this, [this, coaching] {
            coaching->setVisible(m_liveCoaching->isChecked());
            updateAssessmentProgress();
        });
        m_items->addWidget(card);
        m_rows << row;
    }
    updateAssessmentProgress();
}

void InterviewPage::updateAssessmentProgress() {
    int answered = 0;
    for (const AssessmentRow& row : std::as_const(m_rows)) answered += row.group->checkedId() > 0;
    m_progress->setText(tr("%1 of %2 answered").arg(answered).arg(m_rows.size()));
}

void InterviewPage::scoreAssessment(bool save) {
    QJsonObject answers;
    for (const AssessmentRow& row : std::as_const(m_rows))
        if (row.group->checkedId() > 0) answers.insert(row.id, row.group->checkedId());
    const QJsonObject params{{"answers", answers}, {"profile_id", m_profile->currentData().toString()}, {"save", save}};
    m_ctx->bridge()->call("interview.assessment_score", params, [this, save](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->toast(e.message, AppContext::ToastKind::Warning);
        const QJsonObject res = r.toObject();
        const bool consistent = res.value("consistency").toString() == "Consistent";
        m_consistency->setText(res.value("consistency").toString());
        m_consistency->setProperty("kind", consistent ? "good" : "warn");
        ui::repolish(m_consistency);

        QHash<QString, QJsonObject> byId;
        for (const QJsonValue& v : res.value("items").toArray()) byId.insert(jstr(v, "id"), v.toObject());
        for (AssessmentRow& row : m_rows) {
            if (!byId.contains(row.id)) {
                row.fit->hide();
                continue;
            }
            const QString fit = byId.value(row.id).value("fit").toString();
            row.fit->setText(fit == "strong" ? tr("Strong") : fit == "ok" ? tr("Okay") : tr("Rethink"));
            row.fit->setProperty("kind", fit == "strong" ? "good" : fit == "ok" ? "accent" : "bad");
            ui::repolish(row.fit);
            row.fit->show();
            row.coaching->show();  // after scoring, always show the "why"
        }

        ui::clearLayout(m_traits);
        for (const QJsonValue& v : res.value("traits").toArray()) {
            const QJsonObject tr_ = v.toObject();
            m_traits->addWidget(new TraitBar(tr_.value("label").toString(), tr_.value("about").toString(),
                                             tr_.value("score").toInt()));
        }
        ui::clearLayout(m_flags);
        for (const QJsonValue& v : res.value("flags").toArray()) {
            auto* row = new QHBoxLayout;
            row->addWidget(ui::badge(tr("Check"), "warn"), 0, Qt::AlignTop);
            row->addWidget(ui::label(jstr(v, "message")), 1);
            m_flags->addLayout(row);
        }
        for (const QJsonValue& tip : res.value("tips").toArray()) m_flags->addWidget(ui::label("• " + tip.toString()));
        m_assessResult->show();
        QTimer::singleShot(0, this, [this] { m_assessScroll->ensureWidgetVisible(m_assessResult, 0, 0); });
        if (save) {
            m_ctx->toast(tr("Assessment practice saved."), AppContext::ToastKind::Success);
            refreshSaved();
        }
    }, this);
}

// -- Saved tab ------------------------------------------------------------------------------------
QWidget* InterviewPage::buildSavedTab() {
    auto* card = new Card;
    auto* head = new QHBoxLayout;
    head->addWidget(ui::label(tr("Saved practice for this profile. Open a S.T.A.R.S answer to keep improving it."), "Muted"), 1);
    auto* open = ui::button(tr("Open"), "pencil");
    auto* del = ui::flatButton("trash", tr("Delete"));
    head->addWidget(open);
    head->addWidget(del);
    card->body()->addLayout(head);
    m_saved = new QTableWidget(0, 4);
    m_saved->setHorizontalHeaderLabels({tr("When"), tr("Type"), tr("Question / summary"), tr("Score")});
    m_saved->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_saved->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_saved->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_saved->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_saved->verticalHeader()->hide();
    m_saved->setShowGrid(false);
    m_saved->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_saved->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_saved->setSelectionMode(QAbstractItemView::SingleSelection);
    card->body()->addWidget(m_saved, 1);

    auto openRow = [this] { openSaved(m_saved->currentRow()); };
    connect(open, &QPushButton::clicked, this, openRow);
    connect(m_saved, &QTableWidget::cellDoubleClicked, this, openRow);
    connect(del, &QPushButton::clicked, this, [this] {
        const int row = m_saved->currentRow();
        if (row < 0 || row >= m_savedRows.size()) return;
        if (QMessageBox::question(this, tr("Delete practice"), tr("Delete this saved practice?")) != QMessageBox::Yes) return;
        m_ctx->bridge()->call("interview.delete", {{"record_id", jstr(m_savedRows.at(row), "id")}},
                              [this](const QJsonValue&, const BridgeError& e) {
            if (e.isError()) return m_ctx->reportError(tr("Deleting practice"), e);
            refreshSaved();
        }, this);
    });
    return card;
}

void InterviewPage::openSaved(int row) {
    if (row < 0 || row >= m_savedRows.size()) return;
    const QJsonObject rec = m_savedRows.at(row).toObject();
    if (rec.value("type").toString() == "stars") {
        const QString qid = rec.value("question_id").toString();
        for (int i = 0; i < m_questionList->count(); ++i)
            if (m_questionList->item(i)->data(Qt::UserRole).toString() == qid) m_questionList->setCurrentRow(i);
        if (qid == "custom") m_customQuestion->setText(rec.value("question").toString());
        const QJsonObject answers = rec.value("answers").toObject();
        for (const PartEditor& pe : std::as_const(m_parts)) pe.edit->setPlainText(answers.value(pe.key).toString());
        m_tabs->setCurrentIndex(0);
        coach(false);
    } else {
        const QJsonObject answers = rec.value("answers").toObject();
        for (AssessmentRow& r : m_rows) {
            const int v = answers.value(r.id).toInt();
            if (auto* b = r.group->button(v)) b->setChecked(true);
        }
        m_tabs->setCurrentIndex(1);
        updateAssessmentProgress();
        scoreAssessment(false);
    }
}

void InterviewPage::refreshSaved() {
    m_ctx->bridge()->call("interview.saved", {{"profile_id", m_profile->currentData().toString()}},
                          [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        m_savedRows = r.toArray();
        m_saved->setRowCount(m_savedRows.size());
        for (int i = 0; i < m_savedRows.size(); ++i) {
            const QJsonObject rec = m_savedRows.at(i).toObject();
            const bool stars = rec.value("type").toString() == "stars";
            const QDateTime when = QDateTime::fromString(rec.value("created_at").toString(), Qt::ISODate).toLocalTime();
            m_saved->setItem(i, 0, new QTableWidgetItem(QLocale().toString(when, QLocale::ShortFormat)));
            m_saved->setItem(i, 1, new QTableWidgetItem(stars ? tr("S.T.A.R.S") : tr("Assessment")));
            QString summary = stars ? rec.value("question").toString()
                                    : tr("%1 statements · %2 flag(s)").arg(rec.value("answers").toObject().size())
                                          .arg(rec.value("flags").toInt());
            m_saved->setItem(i, 2, new QTableWidgetItem(summary));
            m_saved->setItem(i, 3, new QTableWidgetItem(stars ? QString::number(rec.value("score").toInt()) + "%" : QString()));
        }
        if (!m_openRecord.isEmpty()) {
            for (int i = 0; i < m_savedRows.size(); ++i) {
                if (m_openRecord == "latest" || jstr(m_savedRows.at(i), "id") == m_openRecord) {
                    openSaved(i);
                    break;
                }
            }
            m_openRecord.clear();
        }
    }, this);
}

void InterviewPage::activate(const QVariantMap& args) {
    m_ctx->fillProfileCombo(m_profile, args.value("profile_id").toString());
    m_ctx->fillJobCombo(m_job, args.value("job_id").toString(), true);
    if (m_questions.isEmpty() && m_ctx->bridge()->isReady()) loadBanks();
    if (args.value("tab").toString() == "assessment") m_tabs->setCurrentIndex(1);
    // record_id reopens a saved practice; "latest" reopens the most recent one.
    m_openRecord = args.value("record_id").toString();
    if (m_openRecord.isEmpty() && args.value("latest").toBool()) m_openRecord = QStringLiteral("latest");
    refreshSaved();
}
