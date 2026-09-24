#pragma once

// Master/detail editor for one list section of a profile (Experience,
// Education, Certifications, ...). Left: entries. Right: a form built from
// the field specs. Emits changed() on every edit.

#include <QJsonArray>
#include <QWidget>

class QFormLayout;
class QListWidget;
class QPushButton;

struct FieldSpec {
    QString key;
    QString label;
    QString placeholder;
    bool multiline = false;   // bullets / details
};

class SectionEditor : public QWidget {
    Q_OBJECT
public:
    SectionEditor(const QString& section, const QString& titleField, const QString& subtitleField,
                  QList<FieldSpec> fields, const QString& hint, QWidget* parent = nullptr);

    void setEntries(const QJsonArray& entries);
    QJsonArray entries() const { return m_entries; }
    void selectEntryById(const QString& id);

signals:
    void changed();

private:
    void rebuildList(int select);
    void showEntry(int row);
    void storeField(const QString& key, const QJsonValue& value);
    QString entryLabel(const QJsonObject& e) const;

    QString m_section;
    QString m_titleField;
    QString m_subtitleField;
    QList<FieldSpec> m_fields;
    QJsonArray m_entries;
    QListWidget* m_list;
    QWidget* m_form;
    QHash<QString, QWidget*> m_editors;
    QPushButton* m_remove;
    QPushButton* m_up;
    QPushButton* m_down;
    bool m_loading = false;
};
