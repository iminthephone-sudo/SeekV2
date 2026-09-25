#pragma once

// Interview page tab: practise answering questions about a criminal record and
// get coaching (answer honestly, own it, show change, turn to the job).

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>

#include "ui/Widgets.h"

class AppContext;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;
class QVBoxLayout;

class RecordCoachTab : public QWidget {
    Q_OBJECT
public:
    RecordCoachTab(AppContext* ctx, std::function<QString()> profileId, QWidget* parent = nullptr);
    void loadBank();
    void refreshProof();
    // Reopen a saved practice record (type "record").
    void openRecord(const QJsonObject& record);

signals:
    void saved();

private:
    void selectQuestion(int row);
    QString questionId() const;
    void coach(bool save);
    void showCoaching(const QJsonObject& result);
    void updateCounter();

    AppContext* m_ctx;
    std::function<QString()> m_profileId;
    QJsonArray m_questions;
    QListWidget* m_list;
    QLabel* m_title;
    QLineEdit* m_custom;
    QLabel* m_lookingFor;
    QLabel* m_tip;
    QLabel* m_example;
    QPushButton* m_showExample;
    QLabel* m_legal;
    QPlainTextEdit* m_answer;
    QLabel* m_counter;
    Card* m_proofCard;
    QVBoxLayout* m_proof;
    Card* m_resultCard;
    ScoreRing* m_score;
    QLabel* m_grade;
    QLabel* m_next;
    QVBoxLayout* m_elements;
    QVBoxLayout* m_issues;
    QVBoxLayout* m_outline;
    QScrollArea* m_scroll;
};
