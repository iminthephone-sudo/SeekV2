#pragma once

// Fluent building blocks in the spirit of QWidget-FancyUI: cards, chips,
// flow layout, score ring, hero banner and an InfoBar-style toast.

#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

class QVBoxLayout;
class QTimer;

// Base class for every page: gets a chance to react when navigated to.
class Page : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual void activate(const QVariantMap& args) { Q_UNUSED(args); }
    // Return false to keep the app open (e.g. the user cancelled a "save changes?" prompt).
    virtual bool canClose() { return true; }
};

class Card : public QFrame {
    Q_OBJECT
public:
    explicit Card(QWidget* parent = nullptr, bool clickable = false);
    QVBoxLayout* body() const { return m_layout; }
signals:
    void clicked();
protected:
    void mouseReleaseEvent(QMouseEvent* e) override;
private:
    QVBoxLayout* m_layout;
    bool m_clickable;
};

// Wrapping layout for chips (adapted from Qt's Flow Layout example).
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = 6, int vSpacing = 6);
    ~FlowLayout() override;
    void addItem(QLayoutItem* item) override;
    int count() const override { return int(m_items.size()); }
    QLayoutItem* itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& rect) override;
    void clear();
private:
    int doLayout(const QRect& rect, bool testOnly) const;
    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

class ScoreRing : public QWidget {
    Q_OBJECT
public:
    explicit ScoreRing(QWidget* parent = nullptr);
    void setScore(int score, const QString& caption = {});
    QSize sizeHint() const override { return {120, 120}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int m_score = -1;
    QString m_caption;
};

class HeroBanner : public QWidget {
    Q_OBJECT
public:
    explicit HeroBanner(QWidget* parent = nullptr);
    void setTexts(const QString& eyebrow, const QString& title, const QString& subtitle);
    QVBoxLayout* overlay() const { return m_overlay; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_eyebrow, m_title, m_subtitle;
    QVBoxLayout* m_overlay;
};

// Translucent "glass" card placed on top of the hero art, like FancyUI's home page.
class GlassCard : public QWidget {
    Q_OBJECT
public:
    GlassCard(const QString& icon, const QString& title, const QString& body, QWidget* parent = nullptr);
    QSize sizeHint() const override { return {300, 150}; }
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent*) override { emit clicked(); }
private:
    QString m_icon, m_title, m_body;
    bool m_hover = false;
};

class InfoBar : public QFrame {
    Q_OBJECT
public:
    enum class Kind { Info, Success, Warning, Error };
    explicit InfoBar(QWidget* parent);
    void showMessage(const QString& message, Kind kind);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QLabel* m_icon;
    QLabel* m_text;
    QTimer* m_timer;
    Kind m_kind = Kind::Info;
};

namespace ui {
QPushButton* button(const QString& text, const QString& icon = {}, bool accent = false, QWidget* parent = nullptr);
QPushButton* flatButton(const QString& icon, const QString& tooltip, QWidget* parent = nullptr);
QLabel* label(const QString& text, const char* objectName = nullptr, QWidget* parent = nullptr);
QLabel* chip(const QString& text, const QString& kind = {}, const QString& tooltip = {});
QLabel* badge(const QString& text, const QString& kind = {});
QWidget* pageHeader(const QString& title, const QString& subtitle, QWidget* trailing = nullptr);
void clearLayout(QLayout* layout);
void repolish(QWidget* w);
QString elide(const QString& text, int max);
}  // namespace ui
