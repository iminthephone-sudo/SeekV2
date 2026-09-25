#pragma once

// Window chrome modelled on the FancyUI demo: a custom title bar (menu,
// app mark, title, theme toggle, min/max/close) and a collapsible icon
// sidebar with an accent indicator on the selected item.

#include <QAbstractButton>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

class TitleBarButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Kind { Minimize, Maximize, Close, Plain };
    TitleBarButton(Kind kind, const QString& icon, QWidget* parent = nullptr);
    void setIconName(const QString& name) { m_iconName = name; update(); }
    QSize sizeHint() const override { return {46, 40}; }
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
private:
    Kind m_kind;
    QString m_iconName;
    bool m_hover = false;
};

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);
    void setSubtitle(const QString& text);
    void setNativeFrame(bool native);
signals:
    void menuClicked();
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;
private:
    void syncMaxIcon();
    QWidget* m_window;
    QLabel* m_subtitle;
    TitleBarButton* m_theme;
    TitleBarButton* m_min;
    TitleBarButton* m_max;
    TitleBarButton* m_close;
};

class NavButton : public QAbstractButton {
    Q_OBJECT
public:
    NavButton(const QString& icon, const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {48, 40}; }
    void setExpanded(bool expanded) { m_expanded = expanded; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
private:
    QString m_icon;
    bool m_hover = false;
    bool m_expanded = false;
};

class Sidebar : public QWidget {
    Q_OBJECT
public:
    explicit Sidebar(QWidget* parent = nullptr);
    NavButton* addPage(const QString& key, const QString& icon, const QString& text, bool bottom = false);
    void select(const QString& key);
    void toggleExpanded();
signals:
    void pageSelected(const QString& key);
private:
    QVBoxLayout* m_top;
    QVBoxLayout* m_bottom;
    QHash<QString, NavButton*> m_buttons;
    QPropertyAnimation* m_anim;
    bool m_expanded = false;
};
