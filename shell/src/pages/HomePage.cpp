#include "pages/HomePage.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QScrollArea>
#include <QVBoxLayout>

#include "core/AppContext.h"
#include "core/Theme.h"

HomePage::HomePage(AppContext* ctx, QWidget* parent) : Page(parent), m_ctx(ctx) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* canvas = new QWidget;
    scroll->setWidget(canvas);
    auto* col = new QVBoxLayout(canvas);
    col->setContentsMargins(0, 0, 24, 28);
    col->setSpacing(18);

    m_hero = new HeroBanner(canvas);
    m_hero->setTexts(ctx->organization(), QStringLiteral("SEEK"),
                     tr("Resumes, cover letters and job matching for people rebuilding their careers — "
                        "private, local, and powered by spaCy."));
    connect(ctx, &AppContext::organizationChanged, this, [this](const QString& org) {
        m_hero->setTexts(org, QStringLiteral("SEEK"),
                         tr("Resumes, cover letters and job matching for people rebuilding their careers — "
                            "private, local, and powered by spaCy."));
    });
    auto* glassRow = new QHBoxLayout;
    glassRow->setSpacing(16);
    auto* g1 = new GlassCard("people", tr("Start with a profile"),
                             tr("Add a participant and build one or more resume profiles."), m_hero);
    auto* g2 = new GlassCard("link", tr("Import a job posting"),
                             tr("Paste a link or the description; spaCy pulls the keywords."), m_hero);
    auto* g3 = new GlassCard("envelope-paper", tr("Write a cover letter"),
                             tr("Drafted from their own experience, matched to the job."), m_hero);
    connect(g1, &GlassCard::clicked, this, [ctx] { ctx->navigate("profiles"); });
    connect(g2, &GlassCard::clicked, this, [ctx] { ctx->navigate("jobs"); });
    connect(g3, &GlassCard::clicked, this, [ctx] { ctx->navigate("letters"); });
    glassRow->addWidget(g1);
    glassRow->addWidget(g2);
    glassRow->addWidget(g3);
    glassRow->addStretch(1);
    m_hero->overlay()->addLayout(glassRow);
    col->addWidget(m_hero);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(24, 0, 0, 0);
    body->setSpacing(14);
    col->addLayout(body);

    body->addWidget(ui::label(tr("Workflow"), "SectionTitle"));
    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    grid->addWidget(featureCard("file-earmark-person", tr("Resume builder"),
                                tr("Three clean templates, live preview, PDF / Word export and bullet-by-bullet coaching."),
                                "resume"), 0, 0);
    grid->addWidget(featureCard("bullseye", tr("Keyword optimizer"),
                                tr("Score a profile against a posting, surface skills they already have, and tailor honestly."),
                                "optimize"), 0, 1);
    grid->addWidget(featureCard("shield-check", tr("Fair-chance review"),
                                tr("Spot wording that leads with the setting instead of the skill, and fill employment gaps."),
                                "resume"), 0, 2);
    body->addLayout(grid);

    body->addWidget(ui::label(tr("At a glance"), "SectionTitle"));
    auto* stats = new QHBoxLayout;
    stats->setSpacing(12);

    auto* engine = new Card;
    engine->body()->addWidget(ui::label(tr("Engine"), "Muted"));
    m_engineState = ui::label(tr("Starting…"), "CardTitle");
    m_engineDetail = ui::label(QString(), "Muted");
    engine->body()->addWidget(m_engineState);
    engine->body()->addWidget(m_engineDetail);
    engine->body()->addStretch(1);
    stats->addWidget(engine, 2);

    auto numberCard = [&](const QString& caption, QLabel*& value, const QString& page) {
        auto* c = new Card(nullptr, true);
        c->body()->addWidget(ui::label(caption, "Muted"));
        value = ui::label("0", "BigNumber");
        c->body()->addWidget(value);
        c->body()->addStretch(1);
        connect(c, &Card::clicked, this, [this, page] { m_ctx->navigate(page); });
        stats->addWidget(c, 1);
    };
    numberCard(tr("Profiles"), m_profiles, "profiles");
    numberCard(tr("Saved postings"), m_jobs, "jobs");
    numberCard(tr("Cover letters"), m_letters, "letters");

    auto* active = new Card(nullptr, true);
    active->body()->addWidget(ui::label(tr("Active profile"), "Muted"));
    m_active = ui::label(tr("None yet"), "CardTitle");
    active->body()->addWidget(m_active);
    active->body()->addStretch(1);
    connect(active, &Card::clicked, this, [ctx] { ctx->navigate("profiles"); });
    stats->addWidget(active, 2);
    body->addLayout(stats);

    body->addWidget(ui::label(tr("Recent activity"), "SectionTitle"));
    auto* recentCard = new Card;
    m_recent = new QVBoxLayout;
    m_recent->setSpacing(6);
    recentCard->body()->addLayout(m_recent);
    body->addWidget(recentCard);
    col->addStretch(1);

    connect(ctx->bridge(), &SeekBridge::stateChanged, this, &HomePage::refreshStatus);
    connect(ctx->bridge(), &SeekBridge::ready, this, &HomePage::refresh);
    connect(ctx, &AppContext::profilesChanged, this, &HomePage::refresh);
    connect(ctx, &AppContext::jobsChanged, this, &HomePage::refresh);
    refreshStatus();
}

Card* HomePage::featureCard(const QString& icon, const QString& title, const QString& body, const QString& page) {
    auto* card = new Card(nullptr, true);
    auto* row = new QHBoxLayout;
    row->setSpacing(14);
    auto* iconLabel = new QLabel;
    auto paint = [iconLabel, icon] { iconLabel->setPixmap(Theme::instance()->pixmap(icon, 30, Theme::instance()->p().accent)); };
    paint();
    connect(Theme::instance(), &Theme::changed, iconLabel, paint);
    row->addWidget(iconLabel, 0, Qt::AlignTop);
    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    text->addWidget(ui::label(title, "CardTitle"));
    text->addWidget(ui::label(body, "Muted"));
    row->addLayout(text, 1);
    card->body()->addLayout(row);
    card->setMinimumHeight(110);
    connect(card, &Card::clicked, this, [this, page] { m_ctx->navigate(page); });
    return card;
}

void HomePage::activate(const QVariantMap&) { refresh(); }

void HomePage::refreshStatus() {
    auto* bridge = m_ctx->bridge();
    switch (bridge->state()) {
    case SeekBridge::State::Ready: {
        const QJsonObject nlp = bridge->status().value("nlp").toObject();
        const bool full = nlp.value("mode").toString() == "full";
        m_engineState->setText(full ? tr("Ready — spaCy %1").arg(nlp.value("spacy_version").toString())
                                    : tr("Ready (basic language mode)"));
        m_engineDetail->setText(full ? tr("Model %1 · %2 known skills")
                                           .arg(nlp.value("model").toString())
                                           .arg(nlp.value("skills_known").toInt())
                                     : tr("For better keyword matching run: %1").arg(nlp.value("install_hint").toString()));
        break;
    }
    case SeekBridge::State::Starting:
        m_engineState->setText(tr("Starting the language engine…"));
        m_engineDetail->setText(tr("Loading spaCy takes a few seconds the first time."));
        break;
    case SeekBridge::State::Failed:
        m_engineState->setText(tr("Engine unavailable"));
        m_engineDetail->setText(bridge->lastError());
        break;
    case SeekBridge::State::Stopped:
        m_engineState->setText(tr("Engine stopped"));
        m_engineDetail->setText(tr("Restart it from Settings."));
        break;
    }
}

void HomePage::refresh() {
    refreshStatus();
    if (!m_ctx->bridge()->isReady()) return;
    m_profiles->setText(QString::number(m_ctx->profiles().size()));
    m_jobs->setText(QString::number(m_ctx->jobs().size()));
    m_active->setText(tr("None yet"));
    for (const QJsonValue& v : m_ctx->profiles()) {
        const QJsonObject p = v.toObject();
        if (p.value("id").toString() == m_ctx->activeProfileId()) {
            const QString who = p.value("participant").toString();
            m_active->setText((who.isEmpty() ? QString() : who + " — ") + p.value("name").toString());
        }
    }
    m_ctx->bridge()->call("letter.list", [this](const QJsonValue& r, const BridgeError& e) {
        if (!e.isError()) m_letters->setText(QString::number(r.toArray().size()));
    }, this);
    m_ctx->bridge()->call("history.list", {{"limit", 6}}, [this](const QJsonValue& r, const BridgeError& e) {
        if (e.isError()) return;
        ui::clearLayout(m_recent);
        const QJsonArray items = r.toArray();
        if (items.isEmpty()) m_recent->addWidget(ui::label(tr("Nothing yet — start by creating a profile."), "Muted"));
        for (const QJsonValue& v : items) {
            const QJsonObject h = v.toObject();
            auto* row = new QHBoxLayout;
            const QDateTime when = QDateTime::fromString(h.value("at").toString(), Qt::ISODate).toLocalTime();
            auto* time = ui::label(QLocale().toString(when, QLocale::ShortFormat), "Muted");
            time->setFixedWidth(150);
            time->setWordWrap(false);
            row->addWidget(time);
            row->addWidget(ui::badge(h.value("kind").toString(), "accent"));
            row->addWidget(ui::label(h.value("summary").toString()), 1);
            m_recent->addLayout(row);
        }
    }, this);
}
