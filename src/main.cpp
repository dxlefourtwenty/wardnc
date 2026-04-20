#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QLockFile>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "NotificationCenterPanel.h"
#include "NotificationServer.h"
#include "WardNcConfig.h"
#include "WardNcControl.h"

namespace {

constexpr auto kNotificationService = "org.freedesktop.Notifications";
constexpr auto kNotificationPath = "/org/freedesktop/Notifications";
constexpr auto kControlService = "org.dxle.wardnc";
constexpr auto kControlPath = "/org/dxle/wardnc";
constexpr auto kControlInterface = "org.dxle.wardnc.Control";

QString instanceLockPath()
{
    const QString runtimePath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimePath.isEmpty()) {
        return runtimePath + QStringLiteral("/wardnc.lock");
    }

    return QDir::tempPath() + QStringLiteral("/wardnc.lock");
}

int callControlMethod(const QDBusConnection &bus, const QString &method)
{
    QDBusInterface interface(kControlService, kControlPath, kControlInterface, bus);
    if (!interface.isValid()) {
        qCritical() << "No running wardnc instance found.";
        return 1;
    }

    const QDBusReply<void> reply = interface.call(method);
    if (!reply.isValid()) {
        qCritical() << "Failed to call" << method << "on running wardnc:" << reply.error().message();
        return 1;
    }

    return 0;
}

bool registerService(const QDBusConnection &bus,
                     const QString &serviceName,
                     const QString &failureMessage,
                     QDBusConnectionInterface::ServiceQueueOptions queueOptions,
                     QDBusConnectionInterface::ServiceReplacementOptions replacementOptions)
{
    const auto registration = bus.interface()->registerService(
        serviceName,
        queueOptions,
        replacementOptions);

    if (!registration.isValid()) {
        qCritical() << failureMessage << registration.error().message();
        return false;
    }

    if (registration.value() == QDBusConnectionInterface::ServiceRegistered) {
        return true;
    }

    if (registration.value() == QDBusConnectionInterface::ServiceQueued) {
        qWarning() << failureMessage << serviceName
                   << "(service queued behind existing owner)";
        return false;
    }

    if (registration.value() != QDBusConnectionInterface::ServiceRegistered) {
        qCritical() << failureMessage << serviceName;
        return false;
    }
    return false;
}

bool registerObject(QDBusConnection &bus, const QString &path, QObject *object)
{
    if (bus.registerObject(path,
                           object,
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        return true;
    }

    qCritical() << "Failed to register D-Bus object at" << path << ":" << bus.lastError().message();
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const bool hasQtPlatformOverride = environment.contains(QStringLiteral("QT_QPA_PLATFORM"));
    const bool hasWaylandDisplay = environment.contains(QStringLiteral("WAYLAND_DISPLAY"));
    const bool hasWaylandShellIntegrationOverride =
        environment.contains(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));

    if (!hasQtPlatformOverride && hasWaylandDisplay) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
    }
    if (!hasWaylandShellIntegrationOverride && hasWaylandDisplay) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", QByteArrayLiteral("layer-shell"));
    }

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("wardnc notification center daemon"));
    parser.addHelpOption();

    QCommandLineOption reloadOption(QStringList() << QStringLiteral("reload"),
                                    QStringLiteral("Reload a running wardnc instance configuration."));
    QCommandLineOption openOption(QStringList() << QStringLiteral("open"),
                                  QStringLiteral("Open a running wardnc panel."));
    QCommandLineOption closeOption(QStringList() << QStringLiteral("close"),
                                   QStringLiteral("Close a running wardnc panel."));
    QCommandLineOption toggleOption(QStringList() << QStringLiteral("toggle"),
                                    QStringLiteral("Toggle a running wardnc panel."));

    parser.addOption(reloadOption);
    parser.addOption(openOption);
    parser.addOption(closeOption);
    parser.addOption(toggleOption);
    parser.process(app);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical() << "Failed to connect to the session D-Bus.";
        return 1;
    }

    if (parser.isSet(reloadOption)) {
        return callControlMethod(bus, QStringLiteral("Reload"));
    }
    if (parser.isSet(openOption)) {
        return callControlMethod(bus, QStringLiteral("Open"));
    }
    if (parser.isSet(closeOption)) {
        return callControlMethod(bus, QStringLiteral("Close"));
    }
    if (parser.isSet(toggleOption)) {
        return callControlMethod(bus, QStringLiteral("Toggle"));
    }

    QLockFile instanceLock(instanceLockPath());
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock()) {
        qCritical() << "wardnc is already running.";
        return 1;
    }

    WardNcConfigLoader configLoader;
    NotificationCenterPanel panel(&configLoader);
    NotificationServer server;
    WardNcControl control;

    if (!registerService(bus,
                         kControlService,
                         QStringLiteral("Failed to acquire wardnc control service:"),
                         QDBusConnectionInterface::DontQueueService,
                         QDBusConnectionInterface::DontAllowReplacement)) {
        return 1;
    }

    if (!registerObject(bus, kControlPath, &control)) {
        return 1;
    }

    const bool notificationServiceRegistered = registerService(
        bus,
        kNotificationService,
        QStringLiteral("Failed to acquire org.freedesktop.Notifications:"),
        QDBusConnectionInterface::ReplaceExistingService,
        QDBusConnectionInterface::AllowReplacement);

    if (notificationServiceRegistered) {
        if (!registerObject(bus, kNotificationPath, &server)) {
            return 1;
        }
    } else {
        qWarning() << "wardnc started without org.freedesktop.Notifications ownership;"
                   << "panel control is available, but incoming desktop notifications"
                   << "will be handled by the existing notification daemon.";
    }

    QObject::connect(&server,
                     &NotificationServer::newNotification,
                     &panel,
                     &NotificationCenterPanel::showNotification);

    QObject::connect(&server,
                     &NotificationServer::closeRequested,
                     &panel,
                     &NotificationCenterPanel::closeNotification);

    QObject::connect(&panel,
                     &NotificationCenterPanel::notificationClosed,
                     &server,
                     &NotificationServer::handleNotificationClosed);

    QObject::connect(&panel,
                     &NotificationCenterPanel::actionInvoked,
                     &server,
                     &NotificationServer::invokeAction);

    QObject::connect(&control, &WardNcControl::reloadRequested, &panel, &NotificationCenterPanel::reloadConfiguration);
    QObject::connect(&control, &WardNcControl::openRequested, &panel, &NotificationCenterPanel::openPanel);
    QObject::connect(&control, &WardNcControl::closeRequested, &panel, &NotificationCenterPanel::closePanel);
    QObject::connect(&control, &WardNcControl::toggleRequested, &panel, &NotificationCenterPanel::togglePanel);

    return app.exec();
}
