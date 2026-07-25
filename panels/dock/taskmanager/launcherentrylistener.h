// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusContext>
#include <QHash>
#include <QObject>

namespace dock {

class LauncherEntryListener : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    static LauncherEntryListener *instance();

    Q_INVOKABLE double progress(const QString &desktopId) const;
    Q_INVOKABLE bool progressVisible(const QString &desktopId) const;

Q_SIGNALS:
    void progressChanged(const QString &desktopId);
    void progressVisibleChanged(const QString &desktopId);

private Q_SLOTS:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onProgressPropsChanged(const QString &interfaceName, const QVariantMap &changedProperties,
                                const QStringList &invalidatedProperties);
    void onMprisPropsChanged(const QString &interfaceName, const QVariantMap &changedProperties,
                             const QStringList &invalidatedProperties);

private:
    explicit LauncherEntryListener(QObject *parent = nullptr);

    // Unity LauncherEntry
    void addUnityService(const QString &serviceName);
    void removeUnityService(const QString &serviceName);
    void readUnityProperties(const QString &serviceName, const QString &objectPath, const QString &desktopId);

    // MPRIS
    void addMprisService(const QString &serviceName);
    void removeMprisService(const QString &serviceName);
    void readMprisPlayerProperties(const QString &serviceName, const QString &desktopId);

    QVariant readProperty(const QString &service, const QString &path, const QString &interface, const QString &property);
    void setProgress(const QString &desktopId, double value, bool visible);

    static LauncherEntryListener *s_instance;

    QHash<QString, double> m_progressMap;
    QHash<QString, bool> m_progressVisibleMap;
    QHash<QString, QString> m_serviceToDesktopIdMap;   // unity serviceName → desktopId
    QHash<QString, QString> m_mprisServiceToDesktopIdMap; // mpris serviceName → desktopId
};

} // namespace dock
