#pragma once

#include "ui/Widgets.h"

class AppContext;
class QGridLayout;

class HomePage : public Page {
    Q_OBJECT
public:
    explicit HomePage(AppContext* ctx, QWidget* parent = nullptr);
    void activate(const QVariantMap& args) override;

private:
    void refresh();
    void refreshStatus();
    Card* featureCard(const QString& icon, const QString& title, const QString& body, const QString& page);

    AppContext* m_ctx;
    HeroBanner* m_hero;
    QLabel* m_engineState;
    QLabel* m_engineDetail;
    QLabel* m_profiles;
    QLabel* m_jobs;
    QLabel* m_letters;
    QLabel* m_active;
    QVBoxLayout* m_recent;
};
