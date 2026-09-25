#pragma once

#include <QHash>
#include <QWidget>

class AppContext;
class InfoBar;
class Page;
class QStackedWidget;
class Sidebar;
class TitleBar;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(AppContext* ctx, QWidget* parent = nullptr);
    void navigate(const QString& key, const QVariantMap& args = {});

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void addPage(const QString& key, const QString& icon, const QString& text, Page* page, bool bottom = false);
    Qt::Edges edgesAt(const QPoint& globalPos) const;

    AppContext* m_ctx;
    TitleBar* m_titleBar;
    Sidebar* m_sidebar;
    QStackedWidget* m_stack;
    InfoBar* m_toast;
    QHash<QString, Page*> m_pages;
    bool m_nativeFrame = false;
};
