#include "NotificationServer.h"

/*
 * This file is the freedesktop notification D-Bus adapter. It translates the
 * external protocol into Qt signals and deliberately leaves presentation,
 * history, replacement policy beyond ids, and timeout behavior to
 * NotificationCenterPanel.
 *
 * Notify reuses replacesId when provided and otherwise allocates a local
 * monotonic id. Close and action callbacks are routed back through this object
 * so D-Bus clients receive the standard NotificationClosed and ActionInvoked
 * signals while the panel remains the source of UI truth.
 */
NotificationServer::NotificationServer(QObject *parent)
    : QObject(parent)
{
}

uint NotificationServer::Notify(const QString &appName,
                                uint replacesId,
                                const QString &icon,
                                const QString &summary,
                                const QString &body,
                                const QStringList &actions,
                                const QVariantMap &hints,
                                int timeout)
{
    const uint notificationId = replacesId > 0 ? replacesId : nextId_++;

    emit newNotification(notificationId,
                         appName,
                         summary,
                         body,
                         icon,
                         timeout,
                         actions,
                         hints);

    return notificationId;
}

QStringList NotificationServer::GetCapabilities() const
{
    QStringList capabilities;

    if (notificationsConfig_.bodySupported) {
        capabilities.append(QStringLiteral("body"));
    }
    if (notificationsConfig_.bodyMarkupSupported) {
        capabilities.append(QStringLiteral("body-markup"));
    }
    if (notificationsConfig_.imageSupported) {
        capabilities.append(QStringLiteral("icon-static"));
    }
    if (notificationsConfig_.actionsSupported) {
        capabilities.append(QStringLiteral("actions"));
    }
    if (notificationsConfig_.persistenceSupported) {
        capabilities.append(QStringLiteral("persistence"));
    }

    return capabilities;
}

void NotificationServer::GetServerInformation(QString &name,
                                              QString &vendor,
                                              QString &version,
                                              QString &specVersion) const
{
    name = QStringLiteral("wardnc");
    vendor = QStringLiteral("dxle");
    version = QStringLiteral("0.1.0");
    specVersion = QStringLiteral("1.3");
}

void NotificationServer::CloseNotification(uint id)
{
    emit closeRequested(id, 3);
}

void NotificationServer::handleNotificationClosed(uint id, uint reason)
{
    emit NotificationClosed(id, reason);
}

void NotificationServer::invokeAction(uint id, const QString &actionKey)
{
    emit ActionInvoked(id, actionKey);
}

void NotificationServer::applyConfig(const WardNcConfig &config)
{
    notificationsConfig_ = config.notifications;
}
