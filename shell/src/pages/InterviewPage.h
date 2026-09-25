#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "ui/Widgets.h"

class AppContext;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QPlainTextEdit;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class RecordCoachTab;

// Interview practice: S.T.A.R.S answers (Situation, Task, Action, Result,
// Skills) with spaCy coaching, and workplace-assessment practice.
class InterviewPage : public Page {
    Q_OBJECT
public:
    explicit InterviewPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    struct PartEditor {
        QString key;
        QPlainTextEdit* edit = nullptr;
        QLabel* badge = nullptr;
        QVBoxLayout* feedback = nullptr;
    };
    struct AssessmentRow {
        QString id;
        QButtonGroup* group = nullptr;
        QLabel* coaching = nullptr;
        QLabel* fit = nullptr;
    };

    QWidget* buildStarsTab();
    QWidget* buildAssessmentTab();
    QWidget* buildSavedTab();
    void loadBanks();
    void selectQuestion(int row);
    void loadStoryIdeas();
    void coach(bool save);
    void showCoaching(const QJsonObject& result);
    void clearFeedback();
    void updateCounter();
    void buildAssessmentItems(const QJsonObject& bank);
    void updateAssessmentProgress();
    void scoreAssessment(bool save);
    void refreshSaved();
    void openSaved(int row);
    QString questionId() const;

    AppContext* m_ctx;
    QComboBox* m_profile;
    QComboBox* m_job;
    QTabWidget* m_tabs;
    RecordCoachTab* m_recordTab;
    QWidget* m_savedTab;
    QScrollArea* m_starsScroll = nullptr;
    QScrollArea* m_assessScroll = nullptr;

    // STARS
    QJsonArray m_questions;
    QListWidget* m_questionList;
    QLabel* m_questionTitle;
    QLabel* m_lookingFor;
    QLabel* m_tip;
    QLineEdit* m_customQuestion;
    Card* m_ideasCard;
    QVBoxLayout* m_ideas;
    QList<PartEditor> m_parts;
    QLabel* m_counter;
    Card* m_resultCard;
    ScoreRing* m_score;
    QLabel* m_grade;
    QVBoxLayout* m_notes;
    QTextBrowser* m_polished;

    // Assessment
    QJsonObject m_assessment;
    QVBoxLayout* m_items;
    QList<AssessmentRow> m_rows;
    QCheckBox* m_liveCoaching;
    QLabel* m_progress;
    Card* m_assessResult;
    QLabel* m_consistency;
    QVBoxLayout* m_traits;
    QVBoxLayout* m_flags;

    // Saved
    QTableWidget* m_saved;
    QJsonArray m_savedRows;
    QString m_openRecord;  // saved practice to open once the list loads
};
