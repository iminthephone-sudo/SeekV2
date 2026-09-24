#include "ui/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include "core/AppContext.h"
#include "core/Theme.h"
#include "pages/HistoryPage.h"
#include "pages/HomePage.h"
#include "pages/JobsPage.h"
#include "pages/LettersPage.h"
#include "pages/OptimizePage.h"
#include "pages/ProfilesPage.h"
#include "pages/ResumePage.h"
#include "pages/SettingsPage.h"
#include "ui/Chrome.h"
#include "ui/Widgets.h"

namespace {
constexpr int kResizeMargin = 6;
}

MainWindow::MainWindow(AppContext* ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx) {
    setObjectName("MainRoot");
    setAttribute(Qt::WA_StyledBackground);
    setWindowTitle(QStringLiteral("SEEK"));
    setWindowIcon(Theme::instance()->appIcon());
    // SEEK_NATIVE_FRAME=1 (or the Settings toggle) falls back to the OS title bar,
    // useful on window managers that don't support client-side moves.
    m_nativeFrame = qEnvironmentVariableIntValue("SEEK_NATIVE_FRAME") == 1 ||
                    QSettings().value("ui/nativeFrame", false).toBool();
    if (!m_nativeFrame) setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setMinimumSize(980, 640);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_titleBar = new TitleBar(this);
    m_titleBar->setNativeFrame(m_nativeFrame);
    m_titleBar->setSubtitle(ctx->organization());
    root->addWidget(m_titleBar);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_sidebar = new Sidebar(this);
    body->addWidget(m_sidebar);
    auto* content = new QWidget(this);
    content->setObjectName("ContentArea");
    content->setAttribute(Qt::WA_StyledBackground);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(content);
    m_stack->setObjectName("Pages");
    contentLayout->addWidget(m_stack);
    body->addWidget(content, 1);
    root->addLayout(body, 1);

    addPage("home", "house", tr("Home"), new HomePage(ctx));
    addPage("profiles", "people", tr("Profiles"), new ProfilesPage(ctx));
    addPage("resume", "file-earmark-person", tr("Resume builder"), new ResumePage(ctx));
    addPage("jobs", "briefcase", tr("Job postings"), new JobsPage(ctx));
    addPage("optimize", "bullseye", tr("Keyword optimizer"), new OptimizePage(ctx));
    addPage("letters", "envelope-paper", tr("Cover letters"), new LettersPage(ctx));
    addPage("history", "clock-history", tr("Activity"), new HistoryPage(ctx));
    addPage("settings", "gear", tr("Settings"), new SettingsPage(ctx), true);

    m_toast = new InfoBar(this);

    connect(m_titleBar, &TitleBar::menuClicked, m_sidebar, &Sidebar::toggleExpanded);
    connect(m_sidebar, &Sidebar::pageSelected, this, [this](const QString& key) { navigate(key); });
    connect(ctx, &AppContext::navigateRequested, this, &MainWindow::navigate);
    connect(ctx, &AppContext::organizationChanged, m_titleBar, &TitleBar::setSubtitle);
    connect(ctx, &AppContext::toastRequested, this, [this](const QString& msg, AppContext::ToastKind kind) {
        m_toast->showMessage(msg, static_cast<InfoBar::Kind>(kind));
    });

    if (!m_nativeFrame) qApp->installEventFilter(this);

    QSettings s;
    if (!restoreGeometry(s.value("ui/geometry").toByteArray())) {
        const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1400, 900);
        resize(qMin(1320, avail.width() - 80), qMin(860, avail.height() - 80));
        move(avail.center() - rect().center());
    }
    navigate("home");
}

void MainWindow::addPage(const QString& key, const QString& icon, const QString& text, Page* page, bool bottom) {
    m_pages.insert(key, page);
    m_stack->addWidget(page);
    m_sidebar->addPage(key, icon, text, bottom);
}

void MainWindow::navigate(const QString& key, const QVariantMap& args) {
    Page* page = m_pages.value(key);
    if (!page) return;
    m_sidebar->select(key);
    m_stack->setCurrentWidget(page);
    page->activate(args);
}

Qt::Edges MainWindow::edgesAt(const QPoint& globalPos) const {
    if (isMaximized() || isFullScreen()) return {};
    const QPoint p = mapFromGlobal(globalPos);
    Qt::Edges edges;
    if (p.x() >= 0 && p.x() < kResizeMargin) edges |= Qt::LeftEdge;
    if (p.x() <= width() && p.x() > width() - kResizeMargin) edges |= Qt::RightEdge;
    if (p.y() >= 0 && p.y() < kResizeMargin) edges |= Qt::TopEdge;
    if (p.y() <= height() && p.y() > height() - kResizeMargin) edges |= Qt::BottomEdge;
    return edges;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    // Frameless resize: watch mouse events anywhere inside this window.
    auto* w = qobject_cast<QWidget*>(obj);
    if (w && w->window() == this && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonPress ||
                                     e->type() == QEvent::HoverMove)) {
        const QPoint global = QCursor::pos();
        const Qt::Edges edges = edgesAt(global);
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (edges && me->button() == Qt::LeftButton && windowHandle()) {
                windowHandle()->startSystemResize(edges);
                return true;
            }
        } else if (!(QApplication::mouseButtons() & Qt::LeftButton)) {
            Qt::CursorShape shape = Qt::ArrowCursor;
            if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) shape = Qt::SizeFDiagCursor;
            else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) shape = Qt::SizeBDiagCursor;
            else if (edges & (Qt::LeftEdge | Qt::RightEdge)) shape = Qt::SizeHorCursor;
            else if (edges & (Qt::TopEdge | Qt::BottomEdge)) shape = Qt::SizeVerCursor;
            if (shape != Qt::ArrowCursor) {
                if (!QApplication::overrideCursor() || QApplication::overrideCursor()->shape() != shape) {
                    if (QApplication::overrideCursor()) QApplication::restoreOverrideCursor();
                    QApplication::setOverrideCursor(shape);
                }
            } else if (QApplication::overrideCursor()) {
                QApplication::restoreOverrideCursor();
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (m_toast && m_toast->isVisible()) m_toast->move(width() - m_toast->width() - 20, 52);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings().setValue("ui/geometry", saveGeometry());
    // Pages with unsaved edits get a chance to object.
    for (Page* page : std::as_const(m_pages)) {
        if (!page->canClose()) {
            e->ignore();
            return;
        }
    }
    e->accept();
}
