#pragma once

// Shared, bridge-fed view model. Pages read profiles/jobs from here and are
// told when they change, so every combo box and list stays in sync. The engine
// remains the source of truth; this is only the live cache.

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include "core/SeekBridge.h"

class QComboBox;

class AppContext : public QObject {
    Q_OBJECT
public:
    enum class ToastKind { Info, Success, Warning, Error };
    Q_ENUM(ToastKind)

    explicit AppContext(QObject* parent = nullptr);

    SeekBridge* bridge() const { return m_bridge; }

    const QJsonArray& profiles() const { return m_profiles; }
    const QJsonArray& jobs() const { return m_jobs; }
    QString activeProfileId() const { return m_activeProfile; }
    QString organization() const;
    void setOrganization(const QString& name);

    void refreshProfiles();
    void refreshJobs();
    void refreshAll();
    void setActiveProfile(const QString& id);

    // Fill a combo with profiles / jobs, keeping the current selection when possible.
    void fillProfileCombo(QComboBox* combo, const QString& preferId = {}) const;
    // allowNone adds a first "no posting" entry; noneLabel overrides its text.
    void fillJobCombo(QComboBox* combo, const QString& preferId = {}, bool allowNone = false,
                      const QString& noneLabel = {}) const;

    void toast(const QString& message, ToastKind kind = ToastKind::Info);
    void reportError(const QString& action, const BridgeError& error);
    void navigate(const QString& page, const QVariantMap& args = {});

signals:
    void profilesChanged();
    void jobsChanged();
    void activeProfileChanged(const QString& id);
    void organizationChanged(const QString& name);
    void toastRequested(const QString& message, AppContext::ToastKind kind);
    void navigateRequested(const QString& page, const QVariantMap& args);

private:
    SeekBridge* m_bridge;
    QJsonArray m_profiles;
    QJsonArray m_jobs;
    QString m_activeProfile;
};

// Tiny helpers used everywhere.
inline QString jstr(const QJsonValue& v, const char* key) { return v.toObject().value(QLatin1String(key)).toString(); }
