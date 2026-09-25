#pragma once

#include "ui/Widgets.h"

class AppContext;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

class LettersPage : public Page {
    Q_OBJECT
public:
    explicit LettersPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;
    bool canClose() override;

private:
    void generate();
    void saveLetter();
    void refreshList();
    void openLetter(const QString& id);
    void exportAs(const QString& format);
    void setDirty(bool dirty);
    bool confirmDiscard();

    AppContext* m_ctx;
    QListWidget* m_list;
    QComboBox* m_profile;
    QComboBox* m_job;
    QComboBox* m_tone;
    QComboBox* m_fairChance;
    QLineEdit* m_manager;
    QLineEdit* m_availability;
    QLineEdit* m_note;
    QPlainTextEdit* m_editor;
    QLabel* m_meta;
    QPushButton* m_save;
    QString m_letterId;
    bool m_dirty = false;
    bool m_loading = false;
};
