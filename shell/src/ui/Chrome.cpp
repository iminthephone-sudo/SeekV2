#include "ui/Chrome.h"

#include <QButtonGroup>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSettings>
#include <QVBoxLayout>
#include <QWindow>

#include "core/Theme.h"

// -- TitleBarButton -------------------------------------------------------------------
TitleBarButton::TitleBarButton(Kind kind, const QString& icon, QWidget* parent)
    : QAbstractButton(parent), m_kind(kind), m_iconName(icon) {
    setFixedSize(46, 40);
    setAttribute(Qt::WA_Hover);
    connect(Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void TitleBarButton::paintEvent(QPaintEvent*) {
    const auto& t = Theme::instance()->p();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fg = t.text;
    if (m_kind == Kind::Close && (m_hover || isDown())) {
        p.fillRect(rect(), isDown() ? t.closeHover.lighter(115) : t.closeHover);
        fg = Qt::white;
    } else if (m_hover || isDown()) {
        p.fillRect(rect(), isDown() ? t.maskPressed : t.mask);
    }
    const int s = m_kind == Kind::Plain ? 18 : 14;
    p.drawPixmap((width() - s) / 2, (height() - s) / 2, Theme::instance()->pixmap(m_iconName, s, fg));
}

// -- TitleBar ----------------------------------------------------------------------------
TitleBar::TitleBar(QWidget* window) : QWidget(window), m_window(window) {
    setObjectName("TitleBar");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(44);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(4, 0, 0, 0);
    row->setSpacing(6);

    auto* menu = new TitleBarButton(TitleBarButton::Kind::Plain, "list", this);
    menu->setToolTip(tr("Expand or collapse navigation"));
    connect(menu, &QAbstractButton::clicked, this, &TitleBar::menuClicked);
    auto* logo = new QLabel(this);
    auto refreshLogo = [logo] { logo->setPixmap(Theme::instance()->appIcon().pixmap(22, 22)); };
    refreshLogo();
    auto* title = new QLabel(QStringLiteral("SEEK"), this);
    title->setObjectName("AppTitle");
    m_subtitle = new QLabel(this);
    m_subtitle->setObjectName("AppSubtitle");

    m_theme = new TitleBarButton(TitleBarButton::Kind::Plain, "sun", this);
    m_theme->setToolTip(tr("Switch light / dark theme"));
    auto syncTheme = [this] { m_theme->setIconName(Theme::instance()->isDark() ? "sun" : "moon"); };
    syncTheme();
    connect(Theme::instance(), &Theme::changed, this, syncTheme);
    connect(m_theme, &QAbstractButton::clicked, Theme::instance(), &Theme::toggle);

    m_min = new TitleBarButton(TitleBarButton::Kind::Minimize, "dash-lg", this);
    m_max = new TitleBarButton(TitleBarButton::Kind::Maximize, "square", this);
    m_close = new TitleBarButton(TitleBarButton::Kind::Close, "x-lg", this);
    connect(m_min, &QAbstractButton::clicked, m_window, &QWidget::showMinimized);
    connect(m_max, &QAbstractButton::clicked, this, [this] {
        m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
    });
    connect(m_close, &QAbstractButton::clicked, m_window, &QWidget::close);
    m_window->installEventFilter(this);

    row->addWidget(menu);
    row->addSpacing(4);
    row->addWidget(logo);
    row->addWidget(title);
    row->addWidget(m_subtitle);
    row->addStretch(1);
    row->addWidget(m_theme);
    row->addSpacing(8);
    row->addWidget(m_min);
    row->addWidget(m_max);
    row->addWidget(m_close);
}

void TitleBar::setSubtitle(const QString& text) { m_subtitle->setText(text.isEmpty() ? QString() : "·  " + text); }

void TitleBar::setNativeFrame(bool native) {
    m_min->setVisible(!native);
    m_max->setVisible(!native);
    m_close->setVisible(!native);
}

void TitleBar::syncMaxIcon() { m_max->setIconName(m_window->isMaximized() ? "fullscreen" : "square"); }

bool TitleBar::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_window && e->type() == QEvent::WindowStateChange) syncMaxIcon();
    return QWidget::eventFilter(obj, e);
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_window->windowHandle() && m_close->isVisible())
        m_window->windowHandle()->startSystemMove();
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_close->isVisible())
        m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
    QWidget::mouseDoubleClickEvent(e);
}

// -- NavButton ---------------------------------------------------------------------------
NavButton::NavButton(const QString& icon, const QString& text, QWidget* parent) : QAbstractButton(parent), m_icon(icon) {
    setText(text);
    setToolTip(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(40);
    setAttribute(Qt::WA_Hover);
    connect(Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void NavButton::paintEvent(QPaintEvent*) {
    const auto& t = Theme::instance()->p();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bg = QRectF(rect()).adjusted(4, 2, -4, -2);
    if (isChecked() || m_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? t.mask : (isDown() ? t.maskPressed : t.mask));
        p.drawRoundedRect(bg, 5, 5);
    }
    if (isChecked()) {
        p.setBrush(t.accent);
        p.drawRoundedRect(QRectF(bg.left(), bg.center().y() - 8, 3, 16), 1.5, 1.5);
    }
    p.drawPixmap(int(bg.left()) + 14, (height() - 18) / 2, Theme::instance()->pixmap(m_icon, 18, t.text));
    if (m_expanded) {
        p.setPen(t.text);
        p.drawText(QRectF(bg.left() + 46, 0, bg.width() - 50, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

// -- Sidebar -----------------------------------------------------------------------------
Sidebar::Sidebar(QWidget* parent) : QWidget(parent), m_anim(new QPropertyAnimation(this, "minimumWidth", this)) {
    setObjectName("Sidebar");
    setAttribute(Qt::WA_StyledBackground);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 6, 4, 10);
    col->setSpacing(2);
    m_top = new QVBoxLayout;
    m_top->setSpacing(2);
    m_bottom = new QVBoxLayout;
    m_bottom->setSpacing(2);
    col->addLayout(m_top);
    col->addStretch(1);
    col->addLayout(m_bottom);
    m_expanded = QSettings().value("ui/sidebarExpanded", false).toBool();
    const int w = m_expanded ? 220 : 64;
    setMinimumWidth(w);
    setMaximumWidth(w);
    m_anim->setDuration(160);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::valueChanged, this, [this](const QVariant& v) { setMaximumWidth(v.toInt()); });
}

NavButton* Sidebar::addPage(const QString& key, const QString& icon, const QString& text, bool bottom) {
    auto* btn = new NavButton(icon, text, this);
    btn->setExpanded(m_expanded);
    btn->setAutoExclusive(true);
    (bottom ? m_bottom : m_top)->addWidget(btn);
    m_buttons.insert(key, btn);
    connect(btn, &QAbstractButton::clicked, this, [this, key] { emit pageSelected(key); });
    return btn;
}

void Sidebar::select(const QString& key) {
    if (auto* b = m_buttons.value(key)) b->setChecked(true);
}

void Sidebar::toggleExpanded() {
    m_expanded = !m_expanded;
    QSettings().setValue("ui/sidebarExpanded", m_expanded);
    for (NavButton* b : std::as_const(m_buttons)) b->setExpanded(m_expanded);
    m_anim->stop();
    m_anim->setStartValue(width());
    m_anim->setEndValue(m_expanded ? 220 : 64);
    m_anim->start();
}
