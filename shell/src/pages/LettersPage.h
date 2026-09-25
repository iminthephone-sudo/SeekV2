#pragma once

#include <functional>

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
    void runGenerate();
    // Saves the editor; ``then`` runs only after the engine confirmed the save.
    void saveLetter(std::function<void()> then = {});
    void refreshList();
    void openLetter(const QString& id);
    void exportAs(const QString& format);
    void setDirty(bool dirty);
    // Unsaved letter? Ask; Save runs ``next`` after the save succeeds, Discard reverts first, Cancel skips it.
    void resolveUnsaved(std::function<void()> next);
    void revert();

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
    int m_session = 0;  // bumped when another letter (or a new one) is opened; stale save replies are ignored
};
