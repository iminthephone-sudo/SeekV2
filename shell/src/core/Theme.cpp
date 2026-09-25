#include "core/Theme.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>
#include <QSvgRenderer>

namespace {

ThemePalette lightPalette() {
    ThemePalette t;
    t.window = QColor("#F3F3F3");
    t.content = QColor("#F9F9F9");
    t.card = QColor("#FFFFFF");
    t.cardHover = QColor("#F6F6F6");
    t.border = QColor("#E5E5E5");
    t.text = QColor("#1B1B1B");
    t.textMuted = QColor("#5F5F5F");
    t.accent = QColor("#005FB8");
    t.accentHover = QColor("#1A6FC0");
    t.accentText = QColor("#FFFFFF");
    t.input = QColor("#FBFBFB");
    t.inputFocus = QColor("#FFFFFF");
    t.inputBorder = QColor("#D9D9D9");
    t.mask = QColor(0, 0, 0, 10);
    t.maskPressed = QColor(0, 0, 0, 16);
    t.selection = QColor(0, 95, 184, 40);
    t.success = QColor("#0F7B0F");
    t.warning = QColor("#9D5D00");
    t.danger = QColor("#C42B1C");
    t.closeHover = QColor("#C42B1C");
    return t;
}

ThemePalette darkPalette() {
    ThemePalette t;
    t.window = QColor("#202020");
    t.content = QColor("#272727");
    t.card = QColor("#2D2D2D");
    t.cardHover = QColor("#323232");
    t.border = QColor("#3A3A3A");
    t.text = QColor("#FFFFFF");
    t.textMuted = QColor("#CFCFCF");
    t.accent = QColor("#60CDFF");
    t.accentHover = QColor("#7AD5FF");
    t.accentText = QColor("#000000");
    t.input = QColor("#2D2D2D");
    t.inputFocus = QColor("#1F1F1F");
    t.inputBorder = QColor("#424242");
    t.mask = QColor(255, 255, 255, 15);
    t.maskPressed = QColor(255, 255, 255, 10);
    t.selection = QColor(96, 205, 255, 50);
    t.success = QColor("#6CCB5F");
    t.warning = QColor("#FCE100");
    t.danger = QColor("#FF99A4");
    t.closeHover = QColor("#C42B1C");
    return t;
}

}  // namespace

Theme* Theme::instance() {
    static Theme* theme = new Theme(qApp);
    return theme;
}

Theme::Theme(QObject* parent) : QObject(parent) {
    const QString saved = QSettings().value("ui/theme", "system").toString();
    m_mode = saved == "dark" ? Mode::Dark : saved == "light" ? Mode::Light : Mode::System;
    rebuild();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_mode == Mode::System) apply();
    });
#endif
}

QString Theme::css(const QColor& c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

void Theme::rebuild() {
    bool dark = m_mode == Mode::Dark;
    if (m_mode == Mode::System) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
        dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
    }
    m_dark = dark;
    m_palette = dark ? darkPalette() : lightPalette();
    m_iconCache.clear();
}

void Theme::setMode(Mode mode) {
    m_mode = mode;
    QSettings().setValue("ui/theme", mode == Mode::Dark ? "dark" : mode == Mode::Light ? "light" : "system");
    apply();
}

void Theme::toggle() { setMode(m_dark ? Mode::Light : Mode::Dark); }

void Theme::apply() {
    rebuild();
    QPalette pal;
    const auto& t = m_palette;
    pal.setColor(QPalette::Window, t.window);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Base, t.input);
    pal.setColor(QPalette::AlternateBase, t.card);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.card);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, t.accentText);
    pal.setColor(QPalette::PlaceholderText, t.textMuted);
    pal.setColor(QPalette::ToolTipBase, t.card);
    pal.setColor(QPalette::ToolTipText, t.text);
    pal.setColor(QPalette::Link, t.accent);
    qApp->setPalette(pal);
    qApp->setStyleSheet(styleSheet());
    emit changed();
}

QString Theme::styleSheet() const {
    const auto& t = m_palette;
    QString qss = QStringLiteral(R"(
* { font-family: "Segoe UI Variable Text", "Segoe UI", "Inter", "Noto Sans", sans-serif; font-size: 10pt; color: @text; }
QToolTip { background: @card; color: @text; border: 1px solid @border; padding: 4px 8px; border-radius: 4px; }

#MainRoot, #TitleBar, #Sidebar { background: @window; }
#ContentArea { background: @content; border-top: 1px solid @border; border-left: 1px solid @border; border-top-left-radius: 8px; }
#ContentArea > QStackedWidget, QStackedWidget#Pages > QWidget { background: transparent; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }

#AppTitle { font-size: 10pt; font-weight: 600; }
#AppSubtitle { color: @muted; font-size: 9pt; }
#PageTitle { font-size: 20pt; font-weight: 600; }
#PageSubtitle { color: @muted; font-size: 10pt; }
#SectionTitle { font-size: 12pt; font-weight: 600; }
#Muted, QLabel[muted="true"] { color: @muted; }
#CardTitle { font-size: 11pt; font-weight: 600; }
#BigNumber { font-size: 22pt; font-weight: 600; }

QFrame#Card { background: @card; border: 1px solid @border; border-radius: 8px; }
QFrame#Card[clickable="true"]:hover { background: @cardHover; }
QFrame#InfoBar { border-radius: 6px; border: 1px solid @border; background: @card; }

QPushButton { background: @card; border: 1px solid @inputBorder; border-bottom-color: @bottomLine; border-radius: 5px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { background: @cardHover; }
QPushButton:pressed { background: @input; color: @muted; }
QPushButton:disabled { color: @muted; background: @input; }
QPushButton[accent="true"] { background: @accent; color: @accentText; border: 1px solid @accent; font-weight: 600; }
QPushButton[accent="true"]:hover { background: @accentHover; border-color: @accentHover; }
QPushButton[accent="true"]:disabled { background: @border; color: @muted; border-color: @border; }
QPushButton[flat="true"], QToolButton { background: transparent; border: none; border-radius: 5px; padding: 4px 8px; }
QPushButton[flat="true"]:hover, QToolButton:hover { background: @mask; }
QPushButton[flat="true"]:pressed, QToolButton:pressed { background: @maskPressed; }
QPushButton::menu-indicator { subcontrol-position: right center; right: 8px; }
QPushButton[hasMenu="true"] { padding-right: 26px; }

QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
    background: @input; border: 1px solid @inputBorder; border-bottom: 1px solid @indicator; border-radius: 5px;
    padding: 5px 8px; selection-background-color: @accent; selection-color: @accentText; }
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QComboBox:hover { background: @cardHover; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus { background: @inputFocus; border-bottom: 2px solid @accent; }
QTextBrowser { background: @card; border: 1px solid @border; border-radius: 8px; }
QTextBrowser#Paper { background: #FFFFFF; color: #111827; border: 1px solid @border; border-radius: 4px; padding: 28px; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: @card; border: 1px solid @border; selection-background-color: @selection; selection-color: @text; outline: none; padding: 4px; }

QListWidget, QTreeWidget, QTableWidget { background: transparent; border: none; outline: none; }
QListWidget QScrollBar:horizontal, QTreeWidget QScrollBar:horizontal { height: 0; }
QListWidget::item, QTreeWidget::item { padding: 8px 8px; border-radius: 5px; margin: 1px 2px; }
QListWidget::item:hover, QTreeWidget::item:hover { background: @mask; }
QListWidget::item:selected, QTreeWidget::item:selected { background: @selection; color: @text; }
QTableWidget { gridline-color: @border; }
QTableWidget::item { padding: 6px; }
QTableWidget::item:selected { background: @selection; color: @text; }
QHeaderView::section { background: transparent; border: none; border-bottom: 1px solid @border; padding: 6px; font-weight: 600; color: @muted; }

QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; border: none; padding: 7px 14px; margin-right: 2px; border-radius: 5px; color: @muted; }
QTabBar::tab:hover { background: @mask; color: @text; }
QTabBar::tab:selected { color: @text; background: @card; border: 1px solid @border; font-weight: 600; }

QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid @muted; background: @input; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@checkIcon); }
QRadioButton::indicator:checked { border: 1px solid @accent;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 @accentText, stop:0.42 @accentText, stop:0.5 @accent, stop:1 @accent); }

QProgressBar { background: @border; border: none; border-radius: 2px; max-height: 4px; min-height: 4px; }
QProgressBar::chunk { background: @accent; border-radius: 2px; }

QSplitter::handle { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 3px; min-height: 32px; margin: 0 2px; }
QScrollBar::handle:vertical:hover { margin: 0; border-radius: 4px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @scroll; border-radius: 3px; min-width: 32px; margin: 2px 0; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }

QMenu { background: @card; border: 1px solid @border; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: @mask; }

QLabel#Chip { border-radius: 11px; padding: 3px 10px; background: @mask; border: 1px solid @border; }
QLabel#Chip[kind="matched"] { background: @successBg; border-color: @success; }
QLabel#Chip[kind="missing"] { background: @dangerBg; border-color: @danger; }
QLabel#Chip[kind="required"] { border-color: @accent; font-weight: 600; }
QLabel#Chip[kind="suggest"] { border-style: dashed; border-color: @accent; }
QLabel#Badge { border-radius: 4px; padding: 2px 8px; font-size: 9pt; font-weight: 600; background: @mask; }
QLabel#Badge[kind="good"] { background: @successBg; color: @success; }
QLabel#Badge[kind="warn"] { background: @warningBg; color: @warning; }
QLabel#Badge[kind="bad"] { background: @dangerBg; color: @danger; }
QLabel#Badge[kind="accent"] { background: @selection; color: @accent; }
)");
    auto withAlpha = [](QColor c, int a) { c.setAlpha(a); return c; };
    const QColor bottomLine = m_dark ? QColor("#2C2C2C") : QColor("#C8C8C8");
    const QColor indicator = m_dark ? QColor("#9A9A9A") : QColor("#868686");
    const QColor scroll = m_dark ? QColor(159, 159, 159, 140) : QColor(138, 138, 138, 140);
    const QList<QPair<QString, QColor>> tokens = {
        {"@successBg", withAlpha(t.success, 36)}, {"@warningBg", withAlpha(t.warning, 36)},
        {"@dangerBg", withAlpha(t.danger, 36)},   {"@accentHover", t.accentHover},
        {"@accentText", t.accentText},           {"@accent", t.accent},
        {"@window", t.window},                   {"@content", t.content},
        {"@cardHover", t.cardHover},             {"@card", t.card},
        {"@border", t.border},                   {"@text", t.text},
        {"@muted", t.textMuted},                 {"@inputFocus", t.inputFocus},
        {"@inputBorder", t.inputBorder},         {"@input", t.input},
        {"@maskPressed", t.maskPressed},         {"@mask", t.mask},
        {"@selection", t.selection},             {"@success", t.success},
        {"@warning", t.warning},                 {"@danger", t.danger},
        {"@bottomLine", bottomLine},             {"@indicator", indicator},
        {"@scroll", scroll},
    };
    // Longer token names first (list order) so "@accentText" isn't clobbered by "@accent".
    for (const auto& [token, color] : tokens) qss.replace(token, css(color));
    qss.replace("@checkIcon", m_dark ? ":/icons/check2.svg" : ":/icons/check2-white.svg");
    return qss;
}

QPixmap Theme::pixmap(const QString& name, int size, const QColor& color) const {
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray svg = file.readAll();
    const QColor c = color.isValid() ? color : m_palette.text;
    svg.replace("#000000", c.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(svg);
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size * dpr, size * dpr));
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon Theme::icon(const QString& name, const QColor& color) const {
    const QColor c = color.isValid() ? color : m_palette.text;
    const QString key = name + c.name(QColor::HexArgb);
    auto it = m_iconCache.find(key);
    if (it != m_iconCache.end()) return *it;
    QIcon icon;
    for (int size : {16, 20, 24, 32}) icon.addPixmap(pixmap(name, size, c));
    QColor disabled = c;
    disabled.setAlpha(110);
    icon.addPixmap(pixmap(name, 16, disabled), QIcon::Disabled);
    m_iconCache.insert(key, icon);
    return icon;
}

QIcon Theme::appIcon() const {
    // Rounded accent tile with a magnifier: "seek".
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, size, size);
        g.setColorAt(0, QColor("#0B7CBD"));
        g.setColorAt(1, QColor("#3F2FB5"));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.22, size * 0.22);
        QPen pen(Qt::white, qMax(1.5, size * 0.09), Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal r = size * 0.22;
        const QPointF c(size * 0.44, size * 0.44);
        p.drawEllipse(c, r, r);
        p.drawLine(c + QPointF(r * 0.72, r * 0.72), QPointF(size * 0.76, size * 0.76));
        icon.addPixmap(pm);
    }
    return icon;
}
