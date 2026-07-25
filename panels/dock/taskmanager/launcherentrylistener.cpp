// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcherentrylistener.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(launcherEntryLog, "org.deepin.dde.shell.dock.taskmanger.launcherentry", QtDebugMsg)

namespace dock {

LauncherEntryListener *LauncherEntryListener::s_instance = nullptr;

static const QString UNITY_LAUNCHER_PREFIX = QStringLiteral("com.canonical.Unity.LauncherEntry");
static const QString MPRIS_PREFIX           = QStringLiteral("org.mpris.MediaPlayer2.");
static const QString UNITY_IFACE            = QStringLiteral("com.canonical.Unity.LauncherEntry");
static const QString MPRIS_ROOT_IFACE       = QStringLiteral("org.mpris.MediaPlayer2");
static const QString MPRIS_PLAYER_IFACE     = QStringLiteral("org.mpris.MediaPlayer2.Player");
static const QString PROPERTIES_IFACE       = QStringLiteral("org.freedesktop.DBus.Properties");
static const QString MPRIS_OBJECT_PATH      = QStringLiteral("/org/mpris/MediaPlayer2");

// ---------------------------------------------------------------------------
// constructor / singleton
// ---------------------------------------------------------------------------

LauncherEntryListener::LauncherEntryListener(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    bus.connect(QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("NameOwnerChanged"),
                this, SLOT(onNameOwnerChanged(QString, QString, QString)));

    qCDebug(launcherEntryLog) << "LauncherEntryListener initialized (Unity + MPRIS)";
}

LauncherEntryListener *LauncherEntryListener::instance()
{
    if (!s_instance) {
        s_instance = new LauncherEntryListener();
    }
    return s_instance;
}

// ---------------------------------------------------------------------------
// QML-accessible helpers
// ---------------------------------------------------------------------------

double LauncherEntryListener::progress(const QString &desktopId) const
{
    return m_progressMap.value(desktopId, 0.0);
}

bool LauncherEntryListener::progressVisible(const QString &desktopId) const
{
    return m_progressVisibleMap.value(desktopId, false);
}

void LauncherEntryListener::setProgress(const QString &desktopId, double value, bool visible)
{
    bool changed = false;

    if (!qFuzzyCompare(m_progressMap.value(desktopId, -1.0), value)) {
        m_progressMap[desktopId] = value;
        Q_EMIT progressChanged(desktopId);
        changed = true;
    }
    if (m_progressVisibleMap.value(desktopId) != visible) {
        m_progressVisibleMap[desktopId] = visible;
        Q_EMIT progressVisibleChanged(desktopId);
        changed = true;
    }

    if (changed) {
        qCDebug(launcherEntryLog) << "Progress:" << desktopId << value << (visible ? "visible" : "hidden");
    }
}

// ---------------------------------------------------------------------------
// DBus Properties helper
// ---------------------------------------------------------------------------

QVariant LauncherEntryListener::readProperty(const QString &service, const QString &path,
                                                const QString &interface, const QString &property)
{
    QDBusInterface iface(service, path, PROPERTIES_IFACE, QDBusConnection::sessionBus());
    QDBusMessage msg = iface.call(QStringLiteral("Get"), interface, property);
    if (msg.type() == QDBusMessage::ReplyMessage && msg.arguments().size() > 0) {
        return msg.arguments().at(0).value<QDBusVariant>().variant();
    }
    return QVariant();
}

// ---------------------------------------------------------------------------
// NameOwnerChanged dispatcher
// ---------------------------------------------------------------------------

void LauncherEntryListener::onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    const bool registered   = oldOwner.isEmpty() && !newOwner.isEmpty();
    const bool unregistered = !oldOwner.isEmpty() && newOwner.isEmpty();

    if (name.startsWith(UNITY_LAUNCHER_PREFIX)) {
        if (registered)   addUnityService(name);
        if (unregistered) removeUnityService(name);
    } else if (name.startsWith(MPRIS_PREFIX)) {
        if (registered)   addMprisService(name);
        if (unregistered) removeMprisService(name);
    }
}

// ===================================================================
//  Unity LauncherEntry
// ===================================================================

void LauncherEntryListener::addUnityService(const QString &serviceName)
{
    QString desktopId = serviceName.mid(UNITY_LAUNCHER_PREFIX.length() + 1);
    int underscorePos = desktopId.lastIndexOf(QLatin1Char('_'));
    if (underscorePos > 0) {
        desktopId = desktopId.left(underscorePos);
    }
    if (!desktopId.endsWith(QStringLiteral(".desktop"))) {
        desktopId += QStringLiteral(".desktop");
    }

    qCDebug(launcherEntryLog) << "Unity registered:" << serviceName << "→" << desktopId;
    m_serviceToDesktopIdMap.insert(serviceName, desktopId);

    for (const auto &path : {QStringLiteral("/"), QStringLiteral("/com/canonical/unity/launcherentry")}) {
        readUnityProperties(serviceName, path, desktopId);
    }
}

void LauncherEntryListener::removeUnityService(const QString &serviceName)
{
    QString desktopId = m_serviceToDesktopIdMap.take(serviceName);
    if (!desktopId.isEmpty()) {
        qCDebug(launcherEntryLog) << "Unity unregistered:" << serviceName;
        setProgress(desktopId, 0.0, false);
    }
}

void LauncherEntryListener::readUnityProperties(const QString &serviceName, const QString &objectPath,
                                                  const QString &desktopId)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface(serviceName, objectPath, UNITY_IFACE, bus, this);
    if (!iface.isValid()) return;

    QVariant pv = readProperty(serviceName, objectPath, UNITY_IFACE, QStringLiteral("progress"));
    QVariant vv = readProperty(serviceName, objectPath, UNITY_IFACE, QStringLiteral("progress-visible"));

    setProgress(desktopId, pv.isValid() ? pv.toDouble() : 0.0, vv.isValid() && vv.toBool());

    bus.connect(serviceName, objectPath, PROPERTIES_IFACE,
                QStringLiteral("PropertiesChanged"),
                this, SLOT(onProgressPropsChanged(QString, QVariantMap, QStringList)));
}

// ---------------------------------------------------------------------------
// Unity PropertiesChanged slot
// ---------------------------------------------------------------------------

void LauncherEntryListener::onProgressPropsChanged(const QString &interfaceName,
                                                     const QVariantMap &changedProperties,
                                                     const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)
    if (interfaceName != UNITY_IFACE) return;

    QString serviceName = message().service();
    if (serviceName.isEmpty()) return;

    QString desktopId = m_serviceToDesktopIdMap.value(serviceName);
    if (desktopId.isEmpty()) return;

    double progress = m_progressMap.value(desktopId);
    bool visible = m_progressVisibleMap.value(desktopId);

    if (changedProperties.contains(QStringLiteral("progress")))
        progress = changedProperties.value(QStringLiteral("progress")).toDouble();
    if (changedProperties.contains(QStringLiteral("progress-visible")))
        visible = changedProperties.value(QStringLiteral("progress-visible")).toBool();

    setProgress(desktopId, progress, visible);
}

// ===================================================================
//  MPRIS MediaPlayer2
// ===================================================================

void LauncherEntryListener::addMprisService(const QString &serviceName)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Read DesktopEntry from root interface → maps to desktop ID
    QString desktopEntry = readProperty(serviceName, MPRIS_OBJECT_PATH,
                                        MPRIS_ROOT_IFACE, QStringLiteral("DesktopEntry")).toString();
    if (desktopEntry.isEmpty()) {
        // Fallback: derive from service name "org.mpris.MediaPlayer2.vlc" → "vlc"
        desktopEntry = serviceName.mid(MPRIS_PREFIX.length());
    }

    QString desktopId = desktopEntry;
    if (!desktopId.endsWith(QStringLiteral(".desktop"))) {
        desktopId += QStringLiteral(".desktop");
    }

    qCDebug(launcherEntryLog) << "MPRIS registered:" << serviceName << "→" << desktopId;
    m_mprisServiceToDesktopIdMap.insert(serviceName, desktopId);

    readMprisPlayerProperties(serviceName, desktopId);

    // Subscribe to PropertiesChanged on the Player interface
    bus.connect(serviceName, MPRIS_OBJECT_PATH, PROPERTIES_IFACE,
                QStringLiteral("PropertiesChanged"),
                this, SLOT(onMprisPropsChanged(QString, QVariantMap, QStringList)));
}

void LauncherEntryListener::removeMprisService(const QString &serviceName)
{
    QString desktopId = m_mprisServiceToDesktopIdMap.take(serviceName);
    if (!desktopId.isEmpty()) {
        qCDebug(launcherEntryLog) << "MPRIS unregistered:" << serviceName;
        setProgress(desktopId, 0.0, false);
    }
}

void LauncherEntryListener::readMprisPlayerProperties(const QString &serviceName, const QString &desktopId)
{
    // Read PlaybackStatus
    QString status = readProperty(serviceName, MPRIS_OBJECT_PATH,
                                  MPRIS_PLAYER_IFACE, QStringLiteral("PlaybackStatus")).toString();

    // Read Position (microseconds)
    qlonglong position = readProperty(serviceName, MPRIS_OBJECT_PATH,
                                      MPRIS_PLAYER_IFACE, QStringLiteral("Position")).toLongLong();

    // Read mpris:length from Metadata
    QVariant metadataVar = readProperty(serviceName, MPRIS_OBJECT_PATH,
                                        MPRIS_PLAYER_IFACE, QStringLiteral("Metadata"));
    qlonglong length = 0;
    if (metadataVar.isValid()) {
        // Metadata is a{sv} (QMap<QString, QVariant> as QDBusArgument)
        QDBusArgument arg = metadataVar.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::MapType) {
            arg.beginMap();
            while (!arg.atEnd()) {
                arg.beginMapEntry();
                QString key;
                arg >> key;
                QDBusVariant val;
                arg >> val;
                arg.endMapEntry();
                if (key == QStringLiteral("mpris:length")) {
                    length = val.variant().toLongLong();
                }
            }
            arg.endMap();
        }
    }

    // Compute progress
    bool isPlaying = (status == QStringLiteral("Playing") || status == QStringLiteral("Paused"));
    bool hasLength = (length > 0);
    double progress = (isPlaying && hasLength) ? static_cast<double>(position) / static_cast<double>(length) : 0.0;
    bool visible = isPlaying && hasLength;

    qCDebug(launcherEntryLog) << "MPRIS read:" << desktopId << "status=" << status
                              << "pos=" << position << "len=" << length
                              << "→ progress=" << progress << visible;
    setProgress(desktopId, progress, visible);
}

// ---------------------------------------------------------------------------
// MPRIS PropertiesChanged slot
// ---------------------------------------------------------------------------

void LauncherEntryListener::onMprisPropsChanged(const QString &interfaceName,
                                                  const QVariantMap &changedProperties,
                                                  const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)
    if (interfaceName != MPRIS_PLAYER_IFACE) return;

    QString serviceName = message().service();
    if (serviceName.isEmpty()) return;

    QString desktopId = m_mprisServiceToDesktopIdMap.value(serviceName);
    if (desktopId.isEmpty()) return;

    // Re-read relevant properties on any change
    readMprisPlayerProperties(serviceName, desktopId);
}

} // namespace dock
