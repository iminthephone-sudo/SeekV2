#pragma once

#include <QJsonObject>

#include "ui/Widgets.h"

class AppContext;
class FlowLayout;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
class QTextBrowser;

class JobsPage : public Page {
    Q_OBJECT
public:
    explicit JobsPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    void fetch();
    void importInBrowser(const QString& url);
    void imported(const QJsonObject& job);
    void offerPaste();
    void analyzePaste();
    void rebuildList();
    void showJob(const QString& id);
    void populate(const QJsonObject& job);
    void updateJob(const QJsonObject& changes);

    AppContext* m_ctx;
    QLineEdit* m_url;
    QPushButton* m_fetch;
    QProgressBar* m_progress;
    QLabel* m_progressText;
    QString m_fetchRequest;
    Card* m_pasteCard;
    QLineEdit* m_pasteTitle;
    QLineEdit* m_pasteCompany;
    QLineEdit* m_pasteLocation;
    QPlainTextEdit* m_pasteText;

    QListWidget* m_list;
    QStackedWidget* m_detailStack;
    QLabel* m_title;
    QLabel* m_meta;
    QComboBox* m_status;
    QHBoxLayout* m_signals;
    FlowLayout* m_keywords;
    QTextBrowser* m_description;
    QPlainTextEdit* m_notes;
    QPushButton* m_openLink;
    QJsonObject m_job;
};
