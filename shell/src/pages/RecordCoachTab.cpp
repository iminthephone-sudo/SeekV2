#include "pages/RecordCoachTab.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

RecordCoachTab::RecordCoachTab(AppContext* ctx, std::function<QString()> profileId, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_profileId(std::move(profileId)) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split);

    auto* listCard = new Card;
    listCard->setMinimumWidth(250);
    listCard->setMaximumWidth(340);
    listCard->body()->addWidget(ui::label(tr("Questions about a record"), "CardTitle"));
    m_list = new QListWidget;
    m_list->setWordWrap(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listCard->body()->addWidget(m_list, 1);
    split->addWidget(listCard);

    auto* inner = new QWidget;
    auto* rl = new QVBoxLayout(inner);
    rl->setContentsMargins(0, 0, 8, 0);
    rl->setSpacing(12);

    auto* qCard = new Card;
    m_title = ui::label(tr("Pick a question on the left."), "SectionTitle");
    m_title->setWordWrap(true);
    m_custom = new QLineEdit;
    m_custom->setPlaceholderText(tr("Type the question you expect, e.g. “Tell me about your background.”"));
    m_custom->hide();
    m_lookingFor = ui::label(QString());
    m_lookingFor->setWordWrap(true);
    m_tip = ui::label(QString(), "Muted");
    m_tip->setWordWrap(true);
    auto* exampleRow = new QHBoxLayout;
    m_showExample = ui::button(tr("Show a model answer"), "chat-quote");
    exampleRow->addWidget(m_showExample);
    exampleRow->addStretch(1);
    m_example = ui::label(QString());
    m_example->setWordWrap(true);
    m_example->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_example->hide();
    for (QWidget* w : {static_cast<QWidget*>(m_title), static_cast<QWidget*>(m_custom), static_cast<QWidget*>(m_lookingFor),
                       static_cast<QWidget*>(m_tip)})
        qCard->body()->addWidget(w);
    qCard->body()->addLayout(exampleRow);
    qCard->body()->addWidget(m_example);
    rl->addWidget(qCard);

    auto* legalCard = new Card;
    auto* legalRow = new QHBoxLayout;
    legalRow->addWidget(ui::badge(tr("Know your rights"), "warn"), 0, Qt::AlignTop);
    m_legal = ui::label(QString(), "Muted");
    m_legal->setWordWrap(true);
    legalRow->addWidget(m_legal, 1);
    legalCard->body()->addLayout(legalRow);
    rl->addWidget(legalCard);

    auto* answerCard = new Card;
    answerCard->body()->addWidget(ui::label(tr("Your answer"), "CardTitle"));
    answerCard->body()->addWidget(ui::label(tr("Write it the way you'd say it out loud. Practice stays private to this "
                                               "profile unless you save it."), "Muted"));
    m_answer = new QPlainTextEdit;
    m_answer->setMinimumHeight(170);
    m_answer->setPlaceholderText(tr("Yes. In 2019 I was convicted of … I made a bad decision and I take responsibility "
                                    "for it. Since then I've … I'm ready to …"));
    answerCard->body()->addWidget(m_answer);
    auto* actions = new QHBoxLayout;
    m_counter = ui::label(QString(), "Muted");
    auto* clear = ui::button(tr("Clear"));
    auto* save = ui::button(tr("Coach && save"), "save");
    auto* coachBtn = ui::button(tr("Coach me"), "magic", true);
    actions->addWidget(m_counter, 1);
    actions->addWidget(clear);
    actions->addWidget(save);
    actions->addWidget(coachBtn);
    answerCard->body()->addLayout(actions);
    rl->addWidget(answerCard);

    m_proofCard = new Card;
    m_proofCard->body()->addWidget(ui::label(tr("Proof of change from this profile"), "CardTitle"));
    m_proofCard->body()->addWidget(ui::label(tr("Specific things you've done since are the strongest part of the "
                                                "answer. “Add” puts the sentence at the end of your answer."), "Muted"));
    m_proof = new QVBoxLayout;
    m_proof->setSpacing(6);
    m_proofCard->body()->addLayout(m_proof);
    m_proofCard->hide();
    rl->addWidget(m_proofCard);

    m_resultCard = new Card;
    auto* resRow = new QHBoxLayout;
    m_score = new ScoreRing;
    m_score->setFixedSize(130, 130);
    resRow->addWidget(m_score, 0, Qt::AlignTop);
    auto* resText = new QVBoxLayout;
    m_grade = ui::label(QString(), "SectionTitle");
    m_next = ui::label(QString(), "CardTitle");
    m_next->setWordWrap(true);
    resText->addWidget(m_grade);
    resText->addWidget(m_next);
    m_elements = new QVBoxLayout;
    m_elements->setSpacing(6);
    resText->addLayout(m_elements);
    resText->addStretch(1);
    resRow->addLayout(resText, 1);
    m_resultCard->body()->addLayout(resRow);
    m_issues = new QVBoxLayout;
    m_issues->setSpacing(6);
    m_resultCard->body()->addLayout(m_issues);
    m_resultCard->body()->addWidget(ui::label(tr("Your answer, in the right order"), "CardTitle"));
    m_resultCard->body()->addWidget(ui::label(tr("Your own sentences sorted into the moves this question calls for. "
                                                 "Say them in this order."), "Muted"));
    m_outline = new QVBoxLayout;
    m_outline->setSpacing(6);
    m_resultCard->body()->addLayout(m_outline);
    m_resultCard->hide();
    rl->addWidget(m_resultCard);
    rl->addStretch(1);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidget(inner);
    split->addWidget(m_scroll);
    split->setStretchFactor(1, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, &RecordCoachTab::selectQuestion);
    connect(m_answer, &QPlainTextEdit::textChanged, this, &RecordCoachTab::updateCounter);
    connect(coachBtn, &QPushButton::clicked, this, [this] { coach(false); });
    connect(save, &QPushButton::clicked, this, [this] { coach(true); });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_answer->clear();
        m_resultCard->hide();
    });
    connect(m_showExample, &QPushButton::clicked, this, [this] {
        m_example->setVisible(!m_example->isVisible());
        m_showExample->setText(m_example->isVisible() ? tr("Hide the model answer") : tr("Show a model answer"));
    });
    updateCounter();
}

void RecordCoachTab::loadBank() {
    if (!m_questions.isEmpty()) return;  // loaded already (the engine may have restarted; keep the answer)
    m_ctx->bridge()->call("interview.record_questions", [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->reportError(tr("Loading record questions"), e);
        const QJsonObject bank = r.toObject();
        m_questions = bank.value("questions").toArray();
        m_legal->setText(bank.value("legal_note").toString());
        QSignalBlocker block(m_list);
        m_list->clear();
        for (const QJsonValue& v : std::as_const(m_questions)) {
            const QJsonObject q = v.toObject();
            auto* item = new QListWidgetItem(q.value("category").toString().toUpper() + "\n" + q.value("question").toString(),
                                             m_list);
            item->setData(Qt::UserRole, q.value("id").toString());
        }
        auto* custom = new QListWidgetItem(tr("YOUR OWN QUESTION\nPractise the way an employer asked you"), m_list);
        custom->setData(Qt::UserRole, QStringLiteral("custom"));
        block.unblock();
        m_list->setCurrentRow(0);
    }, this);
}

QString RecordCoachTab::questionId() const {
    auto* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void RecordCoachTab::selectQuestion(int row) {
    m_resultCard->hide();
    const bool custom = questionId() == "custom";
    m_custom->setVisible(custom);
    m_example->hide();
    m_showExample->setText(tr("Show a model answer"));
    if (custom) {
        m_title->setText(tr("Your own question"));
        m_lookingFor->setText(tr("Any question about your record works with the same four moves: answer honestly, "
                                 "own it, show change, turn to the job."));
        m_tip->clear();
        m_showExample->hide();
        m_custom->setFocus();
    } else if (row >= 0 && row < m_questions.size()) {
        const QJsonObject q = m_questions.at(row).toObject();
        m_title->setText("“" + q.value("question").toString() + "”");
        m_lookingFor->setText(tr("<b>What they're looking for:</b> %1").arg(q.value("looking_for").toString().toHtmlEscaped()));
        m_tip->setText(tr("Tip: %1").arg(q.value("tip").toString()));
        m_example->setText(q.value("example").toString());
        m_showExample->show();
    }
}

void RecordCoachTab::refreshProof() {
    const QString pid = m_profileId();
    if (pid.isEmpty()) {
        m_proofCard->hide();
        return;
    }
    m_ctx->bridge()->call("interview.record_proof", {{"profile_id", pid}}, [this](const QJsonValue& r, const BridgeError& e) {
        ui::clearLayout(m_proof);
        const QJsonArray points = e.isError() ? QJsonArray() : r.toArray();
        m_proofCard->setVisible(!points.isEmpty());
        for (const QJsonValue& v : points) {
            const QJsonObject p = v.toObject();
            auto* row = new QHBoxLayout;
            row->addWidget(ui::badge(p.value("kind").toString()), 0, Qt::AlignTop);
            auto* text = ui::label(p.value("sentence").toString());
            text->setWordWrap(true);
            row->addWidget(text, 1);
            auto* add = ui::button(tr("Add"), "plus-lg");
            connect(add, &QPushButton::clicked, this, [this, sentence = p.value("sentence").toString()] {
                QString current = m_answer->toPlainText().trimmed();
                m_answer->setPlainText(current.isEmpty() ? sentence : current + " " + sentence);
                m_answer->setFocus();
            });
            row->addWidget(add, 0, Qt::AlignTop);
            m_proof->addLayout(row);
        }
    }, this);
}

void RecordCoachTab::updateCounter() {
    const int words = m_answer->toPlainText().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    const int seconds = qRound(words / 140.0 * 60);
    m_counter->setText(tr("%1 words · about %2 s spoken · aim for 30–90 seconds").arg(words).arg(seconds));
}

void RecordCoachTab::coach(bool save) {
    const QString qid = questionId();
    if (qid.isEmpty()) return;
    const QJsonObject params{{"question_id", qid}, {"answer", m_answer->toPlainText().trimmed()},
                             {"custom_question", m_custom->text().trimmed()}, {"profile_id", m_profileId()},
                             {"save", save}};
    m_ctx->bridge()->call("interview.record_coach", params, [this, save](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return m_ctx->toast(e.message, AppContext::ToastKind::Warning);
        showCoaching(r.toObject());
        if (save) {
            m_ctx->toast(tr("Practice saved."), AppContext::ToastKind::Success);
            emit saved();
        }
    }, this);
}

void RecordCoachTab::showCoaching(const QJsonObject& result) {
    const auto& t = Theme::instance()->p();
    m_score->setScore(result.value("score").toInt(), tr("answer score"));
    m_grade->setText(result.value("grade").toString());
    m_next->setText(result.value("next_step").toString());

    ui::clearLayout(m_elements);
    for (const QJsonValue& v : result.value("elements").toArray()) {
        const QJsonObject el = v.toObject();
        const int s = el.value("score").toInt();
        auto* row = new QHBoxLayout;
        auto* badge = ui::badge(s >= 100 ? "✓" : s > 0 ? "~" : "✗", s >= 100 ? "good" : s > 0 ? "warn" : "bad");
        badge->setMinimumWidth(24);
        badge->setAlignment(Qt::AlignCenter);
        row->addWidget(badge, 0, Qt::AlignTop);
        QString text = "<b>" + el.value("label").toString().toHtmlEscaped() + "</b>";
        if (!el.value("feedback").toString().isEmpty()) text += " — " + el.value("feedback").toString().toHtmlEscaped();
        auto* label = ui::label(text);
        label->setWordWrap(true);
        row->addWidget(label, 1);
        m_elements->addLayout(row);
    }

    ui::clearLayout(m_issues);
    for (const QJsonValue& v : result.value("issues").toArray()) {
        const QJsonObject is = v.toObject();
        auto* row = new QHBoxLayout;
        row->addWidget(ui::badge(tr("Check"), "warn"), 0, Qt::AlignTop);
        QString text = is.value("message").toString().toHtmlEscaped();
        if (!is.value("found").toString().isEmpty())
            text += QStringLiteral(" <span style='color:%1;'>(%2)</span>")
                        .arg(t.textMuted.name(), is.value("found").toString().toHtmlEscaped());
        auto* label = ui::label(text);
        label->setWordWrap(true);
        row->addWidget(label, 1);
        m_issues->addLayout(row);
    }

    ui::clearLayout(m_outline);
    int step = 1;
    for (const QJsonValue& v : result.value("outline").toArray()) {
        const QJsonObject o = v.toObject();
        QStringList sentences;
        for (const QJsonValue& s : o.value("sentences").toArray()) sentences << s.toString().toHtmlEscaped();
        QString body = o.value("missing").toBool()
                           ? QStringLiteral("<span style='color:%1;'>%2 %3</span>")
                                 .arg(t.warning.name(), tr("Missing."), o.value("hint").toString().toHtmlEscaped())
                           : sentences.join(' ');
        auto* label = ui::label(QStringLiteral("<b>%1. %2</b><br>%3").arg(step++).arg(o.value("label").toString().toHtmlEscaped(), body));
        label->setWordWrap(true);
        m_outline->addWidget(label);
    }
    m_resultCard->show();
    QTimer::singleShot(0, this, [this] { m_scroll->ensureWidgetVisible(m_resultCard, 0, 0); });
}

void RecordCoachTab::openRecord(const QJsonObject& record) {
    const QString qid = record.value("question_id").toString();
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == qid) m_list->setCurrentRow(i);
    if (qid == "custom") m_custom->setText(record.value("question").toString());
    m_answer->setPlainText(record.value("answer").toString());
    coach(false);
}
