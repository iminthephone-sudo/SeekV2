#pragma once

#include "ui/Widgets.h"

class AppContext;
class QComboBox;
class QTableWidget;

class HistoryPage : public Page {
    Q_OBJECT
public:
    explicit HistoryPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    void refresh();
    AppContext* m_ctx;
    QComboBox* m_kind;
    QTableWidget* m_table;
};
