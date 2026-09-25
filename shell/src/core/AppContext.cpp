#include "core/AppContext.h"

#include <QComboBox>
#include <QSettings>
#include <QSignalBlocker>

AppContext::AppContext(QObject* parent) : QObject(parent), m_bridge(new SeekBridge(this)) {
    connect(m_bridge, &SeekBridge::ready, this, [this](const QJsonObject& status) {
        m_activeProfile = status.value("active_profile").toString();
        refreshAll();
    });
}

QString AppContext::organization() const {
    return QSettings().value("ui/organization", tr("Community Justice Outreach")).toString();
}

void AppContext::setOrganization(const QString& name) {
    QSettings().setValue("ui/organization", name);
    emit organizationChanged(name);
}

void AppContext::refreshProfiles() {
    m_bridge->call("profile.list", [this](const QJsonValue& result, const BridgeError& err) {
        if (err.isError()) return reportError(tr("Loading profiles"), err);
        m_profiles = result.toArray();
        // The engine picks a new active profile when the active one is deleted (or clears it when none are
        // left): follow it, and say so, so nothing keeps using a deleted profile's id.
        QString active;
        for (const QJsonValue& p : std::as_const(m_profiles))
            if (p.toObject().value("active").toBool()) active = jstr(p, "id");
        const bool changed = active != m_activeProfile;
        m_activeProfile = active;
        emit profilesChanged();
        if (changed) emit activeProfileChanged(active);
    }, this);
}

void AppContext::refreshJobs() {
    m_bridge->call("job.list", [this](const QJsonValue& result, const BridgeError& err) {
        if (err.isError()) return reportError(tr("Loading jobs"), err);
        m_jobs = result.toArray();
        emit jobsChanged();
    }, this);
}

void AppContext::refreshAll() {
    refreshProfiles();
    refreshJobs();
}

void AppContext::setActiveProfile(const QString& id) {
    if (id.isEmpty() || id == m_activeProfile) return;
    m_bridge->call("profile.set_active", {{"profile_id", id}}, [this, id](const QJsonValue&, const BridgeError& err) {
        if (err.isError()) return reportError(tr("Switching profile"), err);
        m_activeProfile = id;
        emit activeProfileChanged(id);
        refreshProfiles();
    }, this);
}

void AppContext::fillProfileCombo(QComboBox* combo, const QString& preferId) const {
    const QString keep = !preferId.isEmpty() ? preferId
                         : combo->currentData().toString().isEmpty() ? m_activeProfile
                                                                     : combo->currentData().toString();
    QSignalBlocker block(combo);
    combo->clear();
    for (const QJsonValue& v : m_profiles) {
        const QJsonObject p = v.toObject();
        QString label = p.value("name").toString();
        const QString who = p.value("participant").toString();
        if (!who.isEmpty()) label = who + " — " + label;
        if (p.value("active").toBool()) label += tr("  (active)");
        combo->addItem(label, p.value("id").toString());
    }
    const int idx = combo->findData(keep);
    combo->setCurrentIndex(idx >= 0 ? idx : (combo->count() ? 0 : -1));
    block.unblock();
    if (combo->currentData().toString() != keep) emit combo->currentIndexChanged(combo->currentIndex());
}

void AppContext::fillJobCombo(QComboBox* combo, const QString& preferId, bool allowNone, const QString& noneLabel) const {
    const QString keep = preferId.isEmpty() ? combo->currentData().toString() : preferId;
    QSignalBlocker block(combo);
    combo->clear();
    if (allowNone) combo->addItem(noneLabel.isEmpty() ? tr("General letter — no specific posting") : noneLabel, QString());
    for (const QJsonValue& v : m_jobs) {
        const QJsonObject j = v.toObject();
        QString label = j.value("title").toString();
        if (!j.value("company").toString().isEmpty()) label += " · " + j.value("company").toString();
        combo->addItem(label, j.value("id").toString());
    }
    const int idx = combo->findData(keep);
    combo->setCurrentIndex(idx >= 0 ? idx : (combo->count() ? 0 : -1));
    block.unblock();
    if (combo->currentData().toString() != keep) emit combo->currentIndexChanged(combo->currentIndex());
}

void AppContext::toast(const QString& message, ToastKind kind) { emit toastRequested(message, kind); }

void AppContext::reportError(const QString& action, const BridgeError& error) {
    toast(QStringLiteral("%1 failed: %2").arg(action, error.message), ToastKind::Error);
}

void AppContext::navigate(const QString& page, const QVariantMap& args) { emit navigateRequested(page, args); }
