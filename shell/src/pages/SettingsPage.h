#pragma once

#include "ui/Widgets.h"

class AppContext;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;

class SettingsPage : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    void refreshEngine();

    AppContext* m_ctx;
    QRadioButton* m_light;
    QRadioButton* m_dark;
    QRadioButton* m_system;
    QLineEdit* m_org;
    QLabel* m_engineState;
    QLabel* m_engineDetails;
    QLineEdit* m_python;
    QLineEdit* m_dataDir;
    QPlainTextEdit* m_logs;
};
