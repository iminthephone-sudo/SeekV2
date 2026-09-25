#include "pages/SectionEditor.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "ui/Widgets.h"

SectionEditor::SectionEditor(const QString& section, const QString& titleField, const QString& subtitleField,
                             QList<FieldSpec> fields, const QString& hint, QWidget* parent)
    : QWidget(parent), m_section(section), m_titleField(titleField), m_subtitleField(subtitleField),
      m_fields(std::move(fields)) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 8, 0, 0);
    row->setSpacing(14);

    auto* left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(220);
    m_list->setMaximumWidth(300);
    left->addWidget(m_list, 1);
    auto* tools = new QHBoxLayout;
    auto* add = ui::button(tr("Add"), "plus-lg");
    m_remove = ui::flatButton("trash", tr("Remove entry"));
    m_up = ui::flatButton("arrow-up", tr("Move up"));
    m_down = ui::flatButton("arrow-down", tr("Move down"));
    tools->addWidget(add);
    tools->addStretch(1);
    tools->addWidget(m_up);
    tools->addWidget(m_down);
    tools->addWidget(m_remove);
    left->addLayout(tools);
    row->addLayout(left);

    auto* right = new QVBoxLayout;
    if (!hint.isEmpty()) right->addWidget(ui::label(hint, "Muted"));
    m_form = new QWidget(this);
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(0, 4, 0, 0);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setVerticalSpacing(10);
    for (const FieldSpec& f : m_fields) {
        QWidget* editor;
        if (f.multiline) {
            auto* edit = new QPlainTextEdit(m_form);
            edit->setPlaceholderText(f.placeholder);
            edit->setMinimumHeight(f.key == "bullets" ? 160 : 80);
            connect(edit, &QPlainTextEdit::textChanged, this, [this, f, edit] {
                if (f.key == "bullets") {
                    QJsonArray lines;
                    for (const QString& line : edit->toPlainText().split('\n')) {
                        const QString t = line.trimmed();
                        if (!t.isEmpty()) lines.append(t);
                    }
                    storeField(f.key, lines);
                } else {
                    storeField(f.key, edit->toPlainText());
                }
            });
            editor = edit;
        } else {
            auto* edit = new QLineEdit(m_form);
            edit->setPlaceholderText(f.placeholder);
            connect(edit, &QLineEdit::textEdited, this, [this, f](const QString& t) { storeField(f.key, t); });
            editor = edit;
        }
        m_editors.insert(f.key, editor);
        form->addRow(f.label, editor);
        if (f.multiline) {
            auto* tools = new QHBoxLayout;
            tools->setContentsMargins(0, 0, 0, 0);
            m_tools.insert(f.key, tools);
            form->addRow(QString(), tools);
        }
    }
    right->addWidget(m_form);
    right->addStretch(1);
    row->addLayout(right, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, &SectionEditor::showEntry);
    connect(add, &QPushButton::clicked, this, [this] {
        QJsonObject e{{"id", QStringLiteral("%1_%2").arg(m_section.left(3)).arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'))}};
        for (const FieldSpec& f : std::as_const(m_fields)) e.insert(f.key, f.key == "bullets" ? QJsonValue(QJsonArray()) : QJsonValue(QString()));
        m_entries.append(e);
        rebuildList(m_entries.size() - 1);
        emit changed();
        if (auto* first = m_editors.value(m_fields.first().key)) first->setFocus();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0) return;
        m_entries.removeAt(row);
        rebuildList(qMin(row, int(m_entries.size()) - 1));
        emit changed();
    });
    auto move = [this](int delta) {
        const int row = m_list->currentRow();
        const int to = row + delta;
        if (row < 0 || to < 0 || to >= m_entries.size()) return;
        const QJsonValue tmp = m_entries.at(row);
        m_entries[row] = m_entries.at(to);
        m_entries[to] = tmp;
        rebuildList(to);
        emit changed();
    };
    connect(m_up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(m_down, &QPushButton::clicked, this, [move] { move(1); });
    rebuildList(-1);
}

QString SectionEditor::entryLabel(const QJsonObject& e) const {
    QString title = e.value(m_titleField).toString();
    if (title.isEmpty()) title = tr("(untitled)");
    const QString sub = e.value(m_subtitleField).toString();
    return sub.isEmpty() ? title : title + "\n" + sub;
}

void SectionEditor::setEntries(const QJsonArray& entries) {
    m_entries = entries;
    rebuildList(entries.isEmpty() ? -1 : 0);
}

void SectionEditor::selectEntryById(const QString& id) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).toObject().value("id").toString() == id) m_list->setCurrentRow(i);
    }
}

void SectionEditor::rebuildList(int select) {
    QSignalBlocker block(m_list);
    m_list->clear();
    for (const QJsonValue& v : std::as_const(m_entries)) m_list->addItem(entryLabel(v.toObject()));
    block.unblock();
    m_list->setCurrentRow(select);
    showEntry(select);
}

void SectionEditor::showEntry(int row) {
    const bool valid = row >= 0 && row < m_entries.size();
    m_form->setEnabled(valid);
    m_remove->setEnabled(valid);
    m_up->setEnabled(valid && row > 0);
    m_down->setEnabled(valid && row < m_entries.size() - 1);
    m_loading = true;
    const QJsonObject e = valid ? m_entries.at(row).toObject() : QJsonObject();
    for (const FieldSpec& f : std::as_const(m_fields)) {
        QWidget* w = m_editors.value(f.key);
        if (auto* line = qobject_cast<QLineEdit*>(w)) {
            line->setText(e.value(f.key).toString());
        } else if (auto* text = qobject_cast<QPlainTextEdit*>(w)) {
            QSignalBlocker block(text);
            if (e.value(f.key).isArray()) {
                QStringList lines;
                for (const QJsonValue& b : e.value(f.key).toArray()) lines << b.toString();
                text->setPlainText(lines.join('\n'));
            } else {
                text->setPlainText(e.value(f.key).toString());
            }
        }
    }
    m_loading = false;
}

QJsonObject SectionEditor::currentEntry() const {
    const int row = m_list->currentRow();
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).toObject() : QJsonObject();
}

void SectionEditor::setCurrentField(const QString& key, const QJsonValue& value) {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_entries.size()) return;
    showEntry(row);  // make sure the form shows this entry before it's rewritten
    if (auto* text = qobject_cast<QPlainTextEdit*>(m_editors.value(key))) {
        QStringList lines;
        for (const QJsonValue& v : value.toArray()) lines << v.toString();
        text->setPlainText(value.isArray() ? lines.join('\n') : value.toString());  // textChanged stores it
    } else if (auto* line = qobject_cast<QLineEdit*>(m_editors.value(key))) {
        line->setText(value.toString());
        storeField(key, value);
    }
}

void SectionEditor::storeField(const QString& key, const QJsonValue& value) {
    if (m_loading) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_entries.size()) return;
    QJsonObject e = m_entries.at(row).toObject();
    e.insert(key, value);
    m_entries[row] = e;
    if (key == m_titleField || key == m_subtitleField) m_list->item(row)->setText(entryLabel(e));
    emit changed();
}
