#pragma once

#include <QJsonObject>

#include "ui/Widgets.h"

class AppContext;
class FlowLayout;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QStackedWidget;

class OptimizePage : public Page {
    Q_OBJECT
public:
    explicit OptimizePage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    void runMatch();
    void showResult(const QJsonObject& result);
    void apply();

    AppContext* m_ctx;
    QComboBox* m_profile;
    QComboBox* m_job;
    QStackedWidget* m_stack;
    ScoreRing* m_score;
    QLabel* m_grade;
    QLabel* m_required;
    QVBoxLayout* m_tips;
    FlowLayout* m_matched;
    QVBoxLayout* m_addList;
    QVBoxLayout* m_confirmList;
    Card* m_wordingCard;
    FlowLayout* m_wording;
    QVBoxLayout* m_bullets;
    QCheckBox* m_useHeadline;
    QLineEdit* m_headline;
    QCheckBox* m_reorder;
    QRadioButton* m_asCopy;
    QPushButton* m_apply;
    QList<QCheckBox*> m_skillChecks;
    QJsonObject m_result;
    QString m_selectAfterRefresh;  // tailored profile to select once the list reloads
};
