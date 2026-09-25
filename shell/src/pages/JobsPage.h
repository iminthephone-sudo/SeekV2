#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "ui/Widgets.h"

class AppContext;
class QCheckBox;
class QTableWidget;
class FlowLayout;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
class QTextBrowser;
class QTimer;

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
    void runSearch(int page);
    void searchInBrowser(const QString& source, const QString& url, bool visible);
    void addResults(const QJsonArray& rows);
    void finishSearchStep();
    void importResult(int row);
    void flushNotes();
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

    Card* m_searchCard;
    QLineEdit* m_query;
    QLineEdit* m_where;
    QCheckBox* m_srcLinkedIn;
    QCheckBox* m_srcIndeed;
    QCheckBox* m_fairChance;
    QPushButton* m_searchBtn;
    QPushButton* m_more;
    QPushButton* m_finishCheck;
    QLabel* m_searchStatus;
    QTableWidget* m_results;
    QJsonArray m_resultRows;
    QStringList m_searchNotes;
    QString m_checkSource;
    QString m_checkUrl;
    QString m_importingId;
    int m_page = 0;
    int m_pending = 0;

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
    QTimer* m_notesTimer;
    QString m_notesJobId;  // the posting the notes box belongs to
    QString m_notesSaved;
};
