#include "pages/SettingsPage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

SettingsPage::SettingsPage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* canvas = new QWidget;
    scroll->setWidget(canvas);
    auto* col = new QVBoxLayout(canvas);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(14);
    col->addWidget(ui::pageHeader(tr("Settings"), QString()));

    // Appearance
    auto* look = new Card;
    look->body()->addWidget(ui::label(tr("Appearance"), "CardTitle"));
    auto* themeRow = new QHBoxLayout;
    m_light = new QRadioButton(tr("Light"));
    m_dark = new QRadioButton(tr("Dark"));
    m_system = new QRadioButton(tr("Use system setting"));
    auto* group = new QButtonGroup(this);
    for (QRadioButton* r : {m_light, m_dark, m_system}) {
        group->addButton(r);
        themeRow->addWidget(r);
    }
    themeRow->addStretch(1);
    look->body()->addLayout(themeRow);
    auto* native = new QCheckBox(tr("Use the operating system's window frame (takes effect after restart)"));
    native->setChecked(QSettings().value("ui/nativeFrame", false).toBool());
    look->body()->addWidget(native);
    col->addWidget(look);

    // Organization
    auto* org = new Card;
    org->body()->addWidget(ui::label(tr("Organization"), "CardTitle"));
    org->body()->addWidget(ui::label(tr("Shown on the home screen and title bar."), "Muted"));
    m_org = new QLineEdit(ctx->organization());
    org->body()->addWidget(m_org);
    col->addWidget(org);

    // Engine
    auto* engine = new Card;
    engine->body()->addWidget(ui::label(tr("Engine (Python + spaCy)"), "CardTitle"));
    m_engineState = ui::label(QString());
    m_engineDetails = ui::label(QString(), "Muted");
    m_engineDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    engine->body()->addWidget(m_engineState);
    engine->body()->addWidget(m_engineDetails);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_python = new QLineEdit(QSettings().value("engine/python").toString());
    m_python->setPlaceholderText(tr("Automatic (python-backend/.venv, then python on PATH)"));
    auto* browsePy = ui::flatButton("search", tr("Choose Python interpreter"));
    auto* pyRow = new QHBoxLayout;
    pyRow->addWidget(m_python, 1);
    pyRow->addWidget(browsePy);
    form->addRow(tr("Python"), pyRow);
    m_dataDir = new QLineEdit(QSettings().value("engine/dataDir").toString());
    m_dataDir->setPlaceholderText(tr("Automatic (per-user app data folder)"));
    auto* browseData = ui::flatButton("search", tr("Choose data folder"));
    auto* dataRow = new QHBoxLayout;
    dataRow->addWidget(m_dataDir, 1);
    dataRow->addWidget(browseData);
    form->addRow(tr("Data folder"), dataRow);
    engine->body()->addLayout(form);
    auto* engineButtons = new QHBoxLayout;
    auto* restart = ui::button(tr("Apply && restart engine"), "arrow-repeat", true);
    auto* openData = ui::button(tr("Open data folder"), "box-arrow-up-right");
    engineButtons->addWidget(restart);
    engineButtons->addWidget(openData);
    engineButtons->addStretch(1);
    engine->body()->addLayout(engineButtons);
    col->addWidget(engine);

    // Logs
    auto* logs = new Card;
    logs->body()->addWidget(ui::label(tr("Logs"), "CardTitle"));
    logs->body()->addWidget(ui::label(tr("Engine diagnostics (stderr). Include these when reporting a problem."), "Muted"));
    m_logs = new QPlainTextEdit;
    m_logs->setReadOnly(true);
    m_logs->setMinimumHeight(180);
    m_logs->setFont(QFont("Consolas, Menlo, monospace"));
    m_logs->setMaximumBlockCount(2000);
    logs->body()->addWidget(m_logs);
    col->addWidget(logs);

    // About
    auto* about = new Card;
    about->body()->addWidget(ui::label(tr("About SEEK"), "CardTitle"));
    about->body()->addWidget(ui::label(
        tr("SEEK %1 — job-readiness toolkit for justice outreach. Qt %2 shell with a Python/spaCy engine pack behind a "
           "single bridge contract. Everything is stored on this computer; nothing is uploaded.\n\n"
           "Interface style inspired by QWidget-FancyUI (GPLv3); icons from Bootstrap Icons (MIT). "
           "SEEK is free software under the GNU GPL v3.")
            .arg(QStringLiteral(SEEK_VERSION), QString::fromLatin1(qVersion())), "Muted"));
    col->addWidget(about);
    col->addStretch(1);

    // -- wiring ---------------------------------------------------------------------------
    auto syncTheme = [this] {
        const auto mode = Theme::instance()->mode();
        (mode == Theme::Mode::Light ? m_light : mode == Theme::Mode::Dark ? m_dark : m_system)->setChecked(true);
    };
    syncTheme();
    connect(Theme::instance(), &Theme::changed, this, syncTheme);
    connect(m_light, &QRadioButton::clicked, this, [] { Theme::instance()->setMode(Theme::Mode::Light); });
    connect(m_dark, &QRadioButton::clicked, this, [] { Theme::instance()->setMode(Theme::Mode::Dark); });
    connect(m_system, &QRadioButton::clicked, this, [] { Theme::instance()->setMode(Theme::Mode::System); });
    connect(native, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("ui/nativeFrame", on); });
    connect(m_org, &QLineEdit::editingFinished, this, [this] { m_ctx->setOrganization(m_org->text().trimmed()); });
    connect(browsePy, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose Python interpreter"));
        if (!f.isEmpty()) m_python->setText(f);
    });
    connect(browseData, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choose data folder"));
        if (!d.isEmpty()) m_dataDir->setText(d);
    });
    connect(restart, &QPushButton::clicked, this, [this] {
        QSettings s;
        s.setValue("engine/python", m_python->text().trimmed());
        s.setValue("engine/dataDir", m_dataDir->text().trimmed());
        m_ctx->bridge()->restart();
    });
    connect(openData, &QPushButton::clicked, this, [this] {
        const QString dir = m_ctx->bridge()->status().value("data_dir").toString();
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    auto* bridge = ctx->bridge();
    for (const QString& line : bridge->diagnostics()) m_logs->appendPlainText(line);
    connect(bridge, &SeekBridge::diagnostic, m_logs, &QPlainTextEdit::appendPlainText);
    connect(bridge, &SeekBridge::stateChanged, this, &SettingsPage::refreshEngine);
    connect(bridge, &SeekBridge::ready, this, &SettingsPage::refreshEngine);
    refreshEngine();
}

void SettingsPage::activate(const QVariantMap&) { refreshEngine(); }

void SettingsPage::refreshEngine() {
    auto* bridge = m_ctx->bridge();
    const auto& t = Theme::instance()->p();
    QString state;
    QColor color = t.textMuted;
    switch (bridge->state()) {
    case SeekBridge::State::Ready: state = tr("● Running"); color = t.success; break;
    case SeekBridge::State::Starting: state = tr("● Starting…"); color = t.warning; break;
    case SeekBridge::State::Failed: state = tr("● Not running — %1").arg(bridge->lastError()); color = t.danger; break;
    case SeekBridge::State::Stopped: state = tr("● Stopped"); break;
    }
    m_engineState->setText(state);
    m_engineState->setStyleSheet(QStringLiteral("color:%1; font-weight:600;").arg(color.name()));
    const QJsonObject st = bridge->status();
    const QJsonObject nlp = st.value("nlp").toObject();
    QStringList lines;
    lines << tr("Python: %1 (%2)").arg(bridge->pythonPath(), st.value("python").toString());
    lines << tr("Engine folder: %1").arg(bridge->backendDir());
    if (!st.isEmpty()) {
        lines << tr("Data folder: %1").arg(st.value("data_dir").toString());
        lines << tr("spaCy %1 · model %2 · %3 mode").arg(nlp.value("spacy_version").toString(), nlp.value("model").toString(),
                                                         nlp.value("mode").toString());
        if (!nlp.value("install_hint").toString().isEmpty())
            lines << tr("Install the full model for better results: %1").arg(nlp.value("install_hint").toString());
    }
    m_engineDetails->setText(lines.join('\n'));
}
