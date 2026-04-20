#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>

struct WardNcLayoutConfig {
    QString anchor = QStringLiteral("top-right");
    QString screen = QStringLiteral("HDMI-A-1");
    int width = 420;
    int peekWidth = 0;
    int outerMargin = 0;
    int marginTop = 24;
    int marginRight = 24;
    int marginBottom = 24;
    int marginLeft = 0;
    int minimumHeight = 240;
    bool aboveWindows = true;
    bool focusable = true;
    int exclusiveZone = 0;
};

struct WardNcPanelConfig {
    bool startOpen = false;
    bool openOnNewNotification = false;
    bool closeOnEscape = true;
    bool closeOnBackgroundClick = false;
    bool showHeader = true;
    bool showFooter = true;
    bool showAppName = true;
    bool showTimestamp = true;
    bool timeFormat24h = true;
    bool showActions = true;
    bool clearButton = true;
    bool reverseOrder = true;
    int maxVisibleNotifications = 120;
    int maxTrackedNotifications = 400;
    bool showHandle = true;
    QString title = QStringLiteral("Notifications");
    QString footerText = QStringLiteral("wardnc");
};

struct WardNcAnimationConfig {
    bool enabled = true;
    int durationMs = 220;
    bool fade = true;
    int fadeDurationMs = 180;
    int slideDistance = 24;
    QString easing = QStringLiteral("out-cubic");
};

struct WardNcNotificationsConfig {
    bool bodySupported = true;
    bool bodyMarkupSupported = false;
    bool bodyHyperlinksSupported = false;
    bool bodyImagesSupported = false;
    bool imageSupported = true;
    bool actionsSupported = true;
    bool actionIconsSupported = false;
    bool persistenceSupported = true;
    bool keepOnReload = true;
    bool ignoreTransient = false;
    bool autoExpire = false;
    bool respectTimeout = true;
    int defaultTimeoutMs = 5000;
    int maxIconSize = 48;
    bool historyEnabled = true;
    int historyMaxEntries = 2000;
};

struct WardNcTextConfig {
    bool stripPangoMarkup = true;
    bool decodeEntities = true;
    QString summaryFallback = QStringLiteral("Notification");
};

struct WardNcPathsConfig {
    QString styleCss = QStringLiteral("~/.config/wardnc/style.css");
    QString colorsCss = QStringLiteral("~/.config/wardnc/colors.css");
    QString stateFile = QStringLiteral("$XDG_RUNTIME_DIR/wardnc/panel.state");
    QString historyFile = QStringLiteral("~/.local/state/wardnc/notification-history.jsonl");
};

struct WardNcConfig {
    WardNcLayoutConfig layout;
    WardNcPanelConfig panel;
    WardNcAnimationConfig animation;
    WardNcNotificationsConfig notifications;
    WardNcTextConfig text;
    WardNcPathsConfig paths;
};

class WardNcConfigLoader : public QObject {
    Q_OBJECT

public:
    explicit WardNcConfigLoader(QObject *parent = nullptr);

    const WardNcConfig &config() const;
    const QString &styleSheet() const;
    const QHash<QString, QString> &styleVariables() const;
    QString configPath() const;

public slots:
    void reload();

signals:
    void configChanged(const WardNcConfig &config);
    void styleChanged(const QString &styleSheet, const QHash<QString, QString> &styleVariables);

private:
    void ensureConfigFiles();
    void refreshWatchers();

    QFileSystemWatcher watcher_;
    WardNcConfig config_;
    QString styleSheet_;
    QHash<QString, QString> styleVariables_;
    QStringList styleDependencyFiles_;
    QStringList styleDependencyDirectories_;
};
