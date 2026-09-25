#pragma once

// spaCy writing help on the Profiles page: summary drafts and job-duty rewrites.
// Both dialogs only suggest; nothing changes until staff pick "Use" / "Apply".

#include <QDialog>
#include <QJsonObject>
#include <QList>

class AppContext;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

class SummaryHelpDialog : public QDialog {
    Q_OBJECT
public:
    // profileData: the editor's current (possibly unsaved) values, laid over the saved profile.
    SummaryHelpDialog(AppContext* ctx, const QString& profileId, const QJsonObject& profileData, QWidget* parent = nullptr);

signals:
    void chosen(const QString& summary);

private:
    void reload();

    AppContext* m_ctx;
    QString m_profileId;
    QJsonObject m_data;
    QComboBox* m_job;
    QVBoxLayout* m_review;
    QVBoxLayout* m_drafts;
};

class DutiesHelpDialog : public QDialog {
    Q_OBJECT
public:
    DutiesHelpDialog(AppContext* ctx, const QString& title, const QStringList& bullets, bool current,
                     QWidget* parent = nullptr);

signals:
    void applied(const QStringList& bullets);

private:
    struct Row {
        QCheckBox* use = nullptr;
        QLineEdit* text = nullptr;
        QString original;
    };
    void reload();
    QStringList result() const;

    AppContext* m_ctx;
    QString m_title;
    QStringList m_bullets;
    bool m_current;
    QComboBox* m_job;
    QVBoxLayout* m_mine;
    QVBoxLayout* m_suggested;
    QList<Row> m_rows;
    QList<Row> m_suggestions;
    QPushButton* m_apply = nullptr;
};
