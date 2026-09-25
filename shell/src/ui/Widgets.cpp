#include "ui/Widgets.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "core/Theme.h"

// -- Card ---------------------------------------------------------------------------
Card::Card(QWidget* parent, bool clickable) : QFrame(parent), m_layout(new QVBoxLayout(this)), m_clickable(clickable) {
    setObjectName("Card");
    setProperty("clickable", clickable);
    if (clickable) setCursor(Qt::PointingHandCursor);
    m_layout->setContentsMargins(18, 16, 18, 16);
    m_layout->setSpacing(8);
}

void Card::mouseReleaseEvent(QMouseEvent* e) {
    if (m_clickable && e->button() == Qt::LeftButton && rect().contains(e->pos())) emit clicked();
    QFrame::mouseReleaseEvent(e);
}

// -- FlowLayout -------------------------------------------------------------------
FlowLayout::FlowLayout(QWidget* parent, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() { clear(); }

void FlowLayout::addItem(QLayoutItem* item) { m_items.append(item); }

QLayoutItem* FlowLayout::takeAt(int index) {
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

void FlowLayout::clear() {
    while (QLayoutItem* item = takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : m_items) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x(), y = area.y(), lineHeight = 0;
    for (QLayoutItem* item : m_items) {
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_hSpace;
        if (nextX - m_hSpace > area.right() + 1 && lineHeight > 0) {
            x = area.x();
            y += lineHeight + m_vSpace;
            nextX = x + hint.width() + m_hSpace;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

// -- ScoreRing ----------------------------------------------------------------------
ScoreRing::ScoreRing(QWidget* parent) : QWidget(parent) {
    setMinimumSize(110, 110);
    connect(Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ScoreRing::setScore(int score, const QString& caption) {
    m_score = score;
    m_caption = caption;
    update();
}

void ScoreRing::paintEvent(QPaintEvent*) {
    const auto& t = Theme::instance()->p();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int side = qMin(width(), height()) - 12;
    const QRectF r((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    QPen track(t.border, 10, Qt::SolidLine, Qt::RoundCap);
    p.setPen(track);
    p.drawEllipse(r);
    if (m_score >= 0) {
        QColor c = m_score >= 75 ? t.success : m_score >= 50 ? t.accent : m_score >= 30 ? t.warning : t.danger;
        QPen arc(c, 10, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arc);
        p.drawArc(r, 90 * 16, -int(360.0 * 16 * qBound(0, m_score, 100) / 100.0));
    }
    p.setPen(t.text);
    QFont f = font();
    f.setPixelSize(qMax(12, int(side / 4.6)));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QString text = m_score >= 0 ? QString::number(m_score) + "%" : QStringLiteral("—");
    const qreal cy = r.center().y();
    const qreal numberH = side * 0.30;
    const qreal shift = m_caption.isEmpty() ? 0 : side * 0.07;
    p.drawText(QRectF(r.left(), cy - numberH / 2 - shift, r.width(), numberH), Qt::AlignCenter, text);
    if (!m_caption.isEmpty()) {
        f.setPixelSize(qMax(9, int(side / 11.0)));
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(t.textMuted);
        p.drawText(QRectF(r.left() + 14, cy + numberH / 2 - shift, r.width() - 28, side * 0.16),
                   Qt::AlignHCenter | Qt::AlignTop, m_caption);
    }
}

// -- HeroBanner ---------------------------------------------------------------------
HeroBanner::HeroBanner(QWidget* parent) : QWidget(parent), m_overlay(new QVBoxLayout(this)) {
    setMinimumHeight(360);
    m_overlay->setContentsMargins(36, 170, 36, 26);
    m_overlay->addStretch();
    connect(Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void HeroBanner::setTexts(const QString& eyebrow, const QString& title, const QString& subtitle) {
    m_eyebrow = eyebrow;
    m_title = title;
    m_subtitle = subtitle;
    update();
}

void HeroBanner::paintEvent(QPaintEvent*) {
    const bool dark = Theme::instance()->isDark();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    QPainterPath clip;
    clip.addRoundedRect(r, 8, 8);
    p.setClipPath(clip);

    // Deep night-blue base, like the Windows 11 "bloom" artwork used by FancyUI.
    QLinearGradient base(r.topLeft(), r.bottomRight());
    base.setColorAt(0, dark ? QColor("#050B1F") : QColor("#0A1A3F"));
    base.setColorAt(0.55, dark ? QColor("#0B1D4A") : QColor("#123A86"));
    base.setColorAt(1, dark ? QColor("#10306E") : QColor("#2B6BD6"));
    p.fillRect(r, base);

    // Flowing ribbons.
    const qreal w = r.width(), h = r.height();
    struct Ribbon { qreal y0, y1, c1, c2, thick; QColor a, b; };
    const QList<Ribbon> ribbons = {
        {0.95, 0.10, 0.85, 0.05, 0.20, QColor(40, 110, 255, 170), QColor(120, 200, 255, 60)},
        {1.05, 0.25, 0.95, 0.20, 0.16, QColor(30, 80, 220, 150), QColor(90, 170, 255, 80)},
        {1.10, 0.40, 1.00, 0.35, 0.12, QColor(80, 160, 255, 120), QColor(180, 230, 255, 70)},
        {0.80, -0.05, 0.70, 0.00, 0.10, QColor(120, 90, 255, 90), QColor(90, 200, 255, 50)},
    };
    for (const Ribbon& rb : ribbons) {
        QPainterPath path;
        path.moveTo(w * 0.30, h * rb.y0);
        path.cubicTo(w * 0.55, h * rb.c1, w * 0.62, h * rb.c2, w * 1.02, h * rb.y1);
        path.lineTo(w * 1.02, h * (rb.y1 + rb.thick));
        path.cubicTo(w * 0.66, h * (rb.c2 + rb.thick * 1.6), w * 0.58, h * (rb.c1 + rb.thick),
                     w * 0.38, h * (rb.y0 + rb.thick));
        path.closeSubpath();
        QLinearGradient g(QPointF(w * 0.3, h), QPointF(w, 0));
        g.setColorAt(0, rb.a);
        g.setColorAt(1, rb.b);
        p.fillPath(path, g);
    }
    QRadialGradient glow(QPointF(w * 0.78, h * 0.35), w * 0.35);
    glow.setColorAt(0, QColor(140, 200, 255, 70));
    glow.setColorAt(1, QColor(140, 200, 255, 0));
    p.fillRect(r, glow);

    // Fade into the page at the bottom.
    QLinearGradient fade(QPointF(0, h * 0.6), QPointF(0, h));
    QColor page = Theme::instance()->p().content;
    page.setAlpha(0);
    fade.setColorAt(0, page);
    page.setAlpha(dark ? 200 : 120);
    fade.setColorAt(1, page);
    p.fillRect(r, fade);

    p.setClipping(false);
    p.setPen(QColor(255, 255, 255, 210));
    QFont f = font();
    f.setPointSizeF(13);
    p.setFont(f);
    p.drawText(QRectF(40, 34, w - 80, 30), Qt::AlignLeft | Qt::AlignVCenter, m_eyebrow);
    f.setPointSizeF(40);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(38, 62, w - 80, 70), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    f.setPointSizeF(12.5);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(QRectF(40, 130, qMin(w - 80, 620.0), 40), Qt::AlignLeft | Qt::TextWordWrap, m_subtitle);
}

// -- GlassCard ----------------------------------------------------------------------
GlassCard::GlassCard(const QString& icon, const QString& title, const QString& body, QWidget* parent)
    : QWidget(parent), m_icon(icon), m_title(title), m_body(body) {
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(240, 140);
    setMaximumWidth(340);
    setAttribute(Qt::WA_Hover);
}

void GlassCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, QColor(255, 255, 255, m_hover ? 70 : 52));
    g.setColorAt(1, QColor(255, 255, 255, m_hover ? 40 : 24));
    p.setBrush(g);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawRoundedRect(r, 8, 8);
    p.drawPixmap(20, 18, Theme::instance()->pixmap(m_icon, 34, Qt::white));
    QFont f = font();
    f.setPointSizeF(12.5);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(20, 62, r.width() - 40, 26), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    f.setPointSizeF(10);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 225));
    p.drawText(QRectF(20, 90, r.width() - 48, r.height() - 96), Qt::AlignLeft | Qt::TextWordWrap, m_body);
    p.drawPixmap(int(r.width()) - 34, int(r.height()) - 32, Theme::instance()->pixmap("box-arrow-up-right", 16, Qt::white));
}

// -- InfoBar (toast) -----------------------------------------------------------------
InfoBar::InfoBar(QWidget* parent) : QFrame(parent), m_icon(new QLabel(this)), m_text(new QLabel(this)), m_timer(new QTimer(this)) {
    setObjectName("InfoBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 10, 10, 10);
    lay->setSpacing(10);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* close = ui::flatButton("x-lg", tr("Dismiss"), this);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    lay->addWidget(m_icon, 0, Qt::AlignTop);
    lay->addWidget(m_text, 1);
    lay->addWidget(close, 0, Qt::AlignTop);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void InfoBar::showMessage(const QString& message, Kind kind) {
    m_kind = kind;
    const auto& t = Theme::instance()->p();
    const QColor c = kind == Kind::Success ? t.success : kind == Kind::Warning ? t.warning
                   : kind == Kind::Error ? t.danger : t.accent;
    const QString icon = kind == Kind::Success ? "check-circle" : kind == Kind::Info ? "info-circle" : "exclamation-triangle";
    m_icon->setPixmap(Theme::instance()->pixmap(icon, 18, c));
    m_text->setText(message);
    const int w = qMin(460, parentWidget()->width() - 40);
    setFixedWidth(w);
    adjustSize();
    move(parentWidget()->width() - w - 20, 52);
    raise();
    show();
    m_timer->start(kind == Kind::Error ? 9000 : 4000);
}

void InfoBar::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    const auto& t = Theme::instance()->p();
    QColor tint = m_kind == Kind::Success ? t.success : m_kind == Kind::Warning ? t.warning
                : m_kind == Kind::Error ? t.danger : t.accent;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    tint.setAlpha(30);
    p.setPen(Qt::NoPen);
    p.setBrush(tint);
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
}

// -- helpers ---------------------------------------------------------------------------
namespace ui {

QPushButton* button(const QString& text, const QString& icon, bool accent, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    if (accent) b->setProperty("accent", true);
    if (!icon.isEmpty()) {
        auto refresh = [b, icon, accent] {
            const auto& t = Theme::instance()->p();
            b->setIcon(Theme::instance()->icon(icon, accent ? t.accentText : t.text));
        };
        refresh();
        QObject::connect(Theme::instance(), &Theme::changed, b, refresh);
    }
    return b;
}

QPushButton* flatButton(const QString& icon, const QString& tooltip, QWidget* parent) {
    auto* b = new QPushButton(parent);
    b->setProperty("flat", true);
    b->setToolTip(tooltip);
    b->setCursor(Qt::PointingHandCursor);
    b->setIconSize(QSize(16, 16));
    auto refresh = [b, icon] { b->setIcon(Theme::instance()->icon(icon)); };
    refresh();
    QObject::connect(Theme::instance(), &Theme::changed, b, refresh);
    return b;
}

QLabel* label(const QString& text, const char* objectName, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    if (objectName) l->setObjectName(objectName);
    l->setWordWrap(true);
    return l;
}

QLabel* chip(const QString& text, const QString& kind, const QString& tooltip) {
    auto* l = new QLabel(text);
    l->setObjectName("Chip");
    if (!kind.isEmpty()) l->setProperty("kind", kind);
    if (!tooltip.isEmpty()) l->setToolTip(tooltip);
    return l;
}

QLabel* badge(const QString& text, const QString& kind) {
    auto* l = new QLabel(text);
    l->setObjectName("Badge");
    if (!kind.isEmpty()) l->setProperty("kind", kind);
    l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return l;
}

QWidget* pageHeader(const QString& title, const QString& subtitle, QWidget* trailing) {
    auto* w = new QWidget;
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 4);
    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    col->addWidget(label(title, "PageTitle"));
    if (!subtitle.isEmpty()) col->addWidget(label(subtitle, "PageSubtitle"));
    row->addLayout(col, 1);
    if (trailing) row->addWidget(trailing, 0, Qt::AlignBottom);
    return w;
}

void clearLayout(QLayout* layout) {
    if (!layout) return;
    if (auto* flow = dynamic_cast<FlowLayout*>(layout)) {
        flow->clear();
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        if (QLayout* child = item->layout()) clearLayout(child);
        delete item;
    }
}

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QString elide(const QString& text, int max) {
    return text.size() <= max ? text : text.left(max - 1).trimmed() + QChar(0x2026);
}

}  // namespace ui
