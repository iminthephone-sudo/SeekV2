#pragma once

#include <QJsonObject>
#include <functional>

#include "ui/Widgets.h"

class AppContext;
class FlowLayout;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;
class SectionEditor;

class ProfilesPage : public Page {
    Q_OBJECT
public:
    explicit ProfilesPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;
    bool canClose() override;

private:
    void rebuildTree();
    void loadProfile(const QString& id);
    void populate(const QJsonObject& profile);
    QJsonObject collect() const;
    void save(std::function<void()> then = {});
    // Unsaved edits? Ask; Save runs ``next`` only after the save succeeded, Discard reverts first, Cancel skips.
    void resolveUnsaved(std::function<void()> next);
    void showNewProfileDialog();
    void showDuplicateDialog();
    void setDirty(bool dirty);
    void newProfile();
    void duplicateProfile();
    void deleteProfile();
    void suggestSkills();
    void addSkill(const QString& skill);
    QWidget* buildSitesTab();
    void refreshSites();
    void recordSiteStatus(const QString& site, const QString& status);

    AppContext* m_ctx;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    QWidget* m_editor;
    QLabel* m_editorTitle;
    QLabel* m_activeBadge;
    QPushButton* m_save;
    QPushButton* m_revert;
    QPushButton* m_setActive;
    QTabWidget* m_tabs;

    QHash<QString, QLineEdit*> m_fields;  // name, participant, contact.*, headline, target_roles
    QPlainTextEdit* m_summary;
    QPlainTextEdit* m_notes;
    QPlainTextEdit* m_skills;
    QComboBox* m_template;
    QCheckBox* m_includeRefs;
    QCheckBox* m_refsOnRequest;
    FlowLayout* m_suggestions;
    QHash<QString, SectionEditor*> m_sections;
    QHash<QString, QLabel*> m_siteStatus;
    QHash<QString, QPushButton*> m_siteSignOut;

    QJsonObject m_profile;
    QString m_currentId;
    bool m_dirty = false;
    bool m_loading = false;
    int m_editSerial = 0;  // bumped on every edit; a save reply only repopulates if nothing changed since
};
