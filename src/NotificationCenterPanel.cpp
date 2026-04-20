#include "NotificationCenterPanel.h"

#include <QBoxLayout>
#include <QDBusArgument>
#include <QDateTime>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QTextStream>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>
#include <limits>
#include <unistd.h>

#include "NotificationTypes.h"

#if WARDNC_HAS_LAYERSHELLQT
#include <LayerShellQt/window.h>
#endif

namespace {

constexpr int kMaxIconSize = 128;
constexpr int kNormalizedIconSize = 64;

QEasingCurve::Type easingFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();

    if (normalized == QStringLiteral("linear")) {
        return QEasingCurve::Linear;
    }
    if (normalized == QStringLiteral("out-quad")) {
        return QEasingCurve::OutQuad;
    }
    if (normalized == QStringLiteral("out-quart")) {
        return QEasingCurve::OutQuart;
    }
    if (normalized == QStringLiteral("out-quint")) {
        return QEasingCurve::OutQuint;
    }

    return QEasingCurve::OutCubic;
}

QString expandPathValue(QString value)
{
    if (value == QStringLiteral("~")) {
        value = QDir::homePath();
    } else if (value.startsWith(QStringLiteral("~/"))) {
        value = QDir::homePath() + value.mid(1);
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const auto runtimeDirFallback = []() {
        const QString runUserPath = QStringLiteral("/run/user/%1").arg(static_cast<qulonglong>(::getuid()));
        if (QDir(runUserPath).exists()) {
            return runUserPath;
        }

        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (!runtime.isEmpty()) {
            return runtime;
        }

        return QDir::tempPath();
    };

    static const QRegularExpression bracedPattern(QStringLiteral(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})"));
    QRegularExpressionMatchIterator bracedMatches = bracedPattern.globalMatch(value);
    while (bracedMatches.hasNext()) {
        const QRegularExpressionMatch match = bracedMatches.next();
        const QString key = match.captured(1);
        QString replacement = env.value(key);
        if (replacement.isEmpty() && key == QStringLiteral("XDG_RUNTIME_DIR")) {
            replacement = runtimeDirFallback();
        }
        value.replace(match.captured(0), replacement);
    }

    static const QRegularExpression simplePattern(QStringLiteral(R"(\$([A-Za-z_][A-Za-z0-9_]*))"));
    QRegularExpressionMatchIterator simpleMatches = simplePattern.globalMatch(value);
    while (simpleMatches.hasNext()) {
        const QRegularExpressionMatch match = simpleMatches.next();
        const QString key = match.captured(1);
        QString replacement = env.value(key);
        if (replacement.isEmpty() && key == QStringLiteral("XDG_RUNTIME_DIR")) {
            replacement = runtimeDirFallback();
        }
        value.replace(match.captured(0), replacement);
    }

    return value;
}

bool parseStyleLength(const QString &value, int *parsed)
{
    QString trimmed = value.trimmed().toLower();
    if (trimmed.endsWith(QStringLiteral("px"))) {
        trimmed.chop(2);
        trimmed = trimmed.trimmed();
    }

    bool ok = false;
    const int parsedValue = trimmed.toInt(&ok);
    if (!ok) {
        return false;
    }

    *parsed = parsedValue;
    return true;
}

struct WaybarLayerInfo {
    int level = -1;
    int topInset = 0;
};

WaybarLayerInfo waybarLayerInfoOnScreen(QScreen *screen)
{
    WaybarLayerInfo info;
    if (!screen) {
        return info;
    }

    QProcess process;
    process.start(QStringLiteral("/usr/bin/hyprctl"),
                  {QStringLiteral("-j"), QStringLiteral("layers")});
    if (!process.waitForFinished(180)) {
        process.kill();
        process.waitForFinished();
        return info;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return info;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return info;
    }

    const QRect screenGeometry = screen->geometry();
    const auto collectMonitorWaybarInfo = [&screenGeometry](const QJsonObject &monitorObject) {
        WaybarLayerInfo monitorInfo;
        const QJsonObject levels = monitorObject.value(QStringLiteral("levels")).toObject();
        for (auto it = levels.constBegin(); it != levels.constEnd(); ++it) {
            bool levelOk = false;
            const int level = it.key().toInt(&levelOk);
            if (!levelOk) {
                continue;
            }

            const QJsonArray surfaces = it.value().toArray();
            for (const QJsonValue &surfaceValue : surfaces) {
                const QJsonObject surface = surfaceValue.toObject();
                if (surface.value(QStringLiteral("namespace")).toString() != QStringLiteral("waybar")) {
                    continue;
                }

                const int x = surface.value(QStringLiteral("x")).toInt();
                const int y = surface.value(QStringLiteral("y")).toInt();
                const int w = surface.value(QStringLiteral("w")).toInt();
                const int h = surface.value(QStringLiteral("h")).toInt();
                if (w <= 0 || h <= 0) {
                    continue;
                }

                const QRect layerRect(x, y, w, h);
                if (layerRect.intersects(screenGeometry)) {
                    if (monitorInfo.level < 0 || level < monitorInfo.level) {
                        monitorInfo.level = level;
                    }
                    const int topInset = qMax(0, layerRect.bottom() - screenGeometry.top() + 1);
                    if (topInset > monitorInfo.topInset) {
                        monitorInfo.topInset = topInset;
                    }
                }
            }
        }
        return monitorInfo;
    };

    const QJsonObject root = doc.object();
    const QJsonValue screenValue = root.value(screen->name());
    if (screenValue.isObject()) {
        return collectMonitorWaybarInfo(screenValue.toObject());
    }

    return info;
}

int hyprReservedTopOnScreen(QScreen *screen)
{
    if (!screen) {
        return 0;
    }

    QProcess process;
    process.start(QStringLiteral("/usr/bin/hyprctl"),
                  {QStringLiteral("-j"), QStringLiteral("monitors")});
    if (!process.waitForFinished(180)) {
        process.kill();
        process.waitForFinished();
        return 0;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return 0;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return 0;
    }

    const QJsonArray monitors = doc.array();
    for (const QJsonValue &monitorValue : monitors) {
        const QJsonObject monitor = monitorValue.toObject();
        if (monitor.value(QStringLiteral("name")).toString() != screen->name()) {
            continue;
        }

        const QJsonArray reserved = monitor.value(QStringLiteral("reserved")).toArray();
        if (reserved.size() >= 2) {
            return qMax(0, reserved.at(1).toInt());
        }
        return 0;
    }

    return 0;
}

QColor styleColorValue(const QHash<QString, QString> &styleVariables,
                       const QString &name,
                       const QColor &fallback)
{
    const QString raw = styleVariables.value(name).trimmed();
    if (raw.isEmpty()) {
        return fallback;
    }

    const QColor parsed(raw);
    return parsed.isValid() ? parsed : fallback;
}

QIcon dismissGlyphIcon(int buttonSize, const QColor &color)
{
    const int size = qMax(12, buttonSize);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color);
    pen.setWidthF(qMax(1.6, size * 0.11));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const qreal inset = size * 0.32;
    painter.drawLine(QPointF(inset, inset), QPointF(size - inset, size - inset));
    painter.drawLine(QPointF(size - inset, inset), QPointF(inset, size - inset));

    return QIcon(pixmap);
}

QPixmap loadPixmapFromImageData(const QVariant &value)
{
    if (!value.isValid() || !value.canConvert<QDBusArgument>()) {
        return {};
    }

    const QDBusArgument argument = value.value<QDBusArgument>();
    int imageWidth = 0;
    int imageHeight = 0;
    int rowStride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray imageData;

    argument.beginStructure();
    argument >> imageWidth >> imageHeight >> rowStride >> hasAlpha >> bitsPerSample >> channels >> imageData;
    argument.endStructure();

    if (imageWidth <= 0 || imageHeight <= 0 || rowStride <= 0 || bitsPerSample != 8 || imageData.isEmpty()) {
        return {};
    }

    QImage image;

    if (channels == 4) {
        image = QImage(reinterpret_cast<const uchar *>(imageData.constData()),
                       imageWidth,
                       imageHeight,
                       rowStride,
                       hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGBX8888)
                    .copy();
    } else if (channels == 3) {
        image = QImage(reinterpret_cast<const uchar *>(imageData.constData()),
                       imageWidth,
                       imageHeight,
                       rowStride,
                       QImage::Format_RGB888)
                    .copy();
    } else if (channels == 1) {
        image = QImage(reinterpret_cast<const uchar *>(imageData.constData()),
                       imageWidth,
                       imageHeight,
                       rowStride,
                       QImage::Format_Grayscale8)
                    .copy();
    }

    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}

QPixmap pixmapFromPath(const QString &path)
{
    if (path.startsWith(QStringLiteral("file://"))) {
        return QPixmap(path.mid(7));
    }

    return QPixmap(path);
}

} // namespace

NotificationCenterPanel::NotificationCenterPanel(WardNcConfigLoader *configLoader, QWidget *parent)
    : QWidget(parent)
    , configLoader_(configLoader)
{
    setObjectName(QStringLiteral("notificationCenterWindow"));
    setAttribute(Qt::WA_TranslucentBackground);

    buildUi();

    connect(&stateWatcher_, &QFileSystemWatcher::fileChanged,
            this, &NotificationCenterPanel::handleStateFileChanged);
    connect(&stateWatcher_, &QFileSystemWatcher::directoryChanged,
            this, &NotificationCenterPanel::handleStateDirectoryChanged);
    connect(&historyWatcher_, &QFileSystemWatcher::fileChanged,
            this, &NotificationCenterPanel::handleHistoryFileChanged);
    connect(&historyWatcher_, &QFileSystemWatcher::directoryChanged,
            this, &NotificationCenterPanel::handleHistoryDirectoryChanged);
#if WARDNC_HAS_LAYERSHELLQT
    layerShellAnimationTimer_.setInterval(16);
    connect(&layerShellAnimationTimer_, &QTimer::timeout, this,
            &NotificationCenterPanel::advanceLayerShellAnimation);
#endif

    if (configLoader_) {
        config_ = configLoader_->config();
        styleSheet_ = configLoader_->styleSheet();
        styleVariables_ = configLoader_->styleVariables();

        connect(configLoader_, &WardNcConfigLoader::configChanged,
                this, &NotificationCenterPanel::applyConfig);
        connect(configLoader_, &WardNcConfigLoader::styleChanged,
                this, &NotificationCenterPanel::applyStyle);
    }

    applyStyle(styleSheet_, styleVariables_);
    applyConfig(config_);
    loadNotificationHistory();

    const QString state = readStateFile();
    if (state.isEmpty()) {
        open_ = config_.panel.startOpen;
        writeStateFile(open_ ? QStringLiteral("open") : QStringLiteral("closed"));
    } else {
        open_ = (state == QStringLiteral("open"));
    }

    statePollTimer_.setInterval(350);
    connect(&statePollTimer_, &QTimer::timeout, this, [this]() {
        if (writingState_) {
            return;
        }

        const QString state = readStateFile();
        if (state.isEmpty()) {
            return;
        }

        const bool shouldOpen = (state == QStringLiteral("open"));
        setPanelOpen(shouldOpen, false, false);
    });
    statePollTimer_.start();

    setPanelOpen(open_, false, false);
}

NotificationCenterPanel::~NotificationCenterPanel()
{
    while (!entries_.isEmpty()) {
        removeEntry(entries_.first(), 4, false);
    }
}

void NotificationCenterPanel::buildUi()
{
    panelRoot_ = new QFrame(this);
    panelRoot_->setObjectName(QStringLiteral("panelRoot"));

    panelLayout_ = new QVBoxLayout(panelRoot_);
    panelLayout_->setContentsMargins(12, 12, 12, 12);
    panelLayout_->setSpacing(10);

    headerBar_ = new QFrame(panelRoot_);
    headerBar_->setObjectName(QStringLiteral("headerBar"));
    headerLayout_ = new QHBoxLayout(headerBar_);
    headerLayout_->setContentsMargins(12, 0, 12, 0);
    headerLayout_->setSpacing(8);

    titleLabel_ = new QLabel(headerBar_);
    titleLabel_->setObjectName(QStringLiteral("headerTitle"));

    countBadgeLabel_ = new QLabel(headerBar_);
    countBadgeLabel_->setObjectName(QStringLiteral("countBadge"));
    countBadgeLabel_->setAlignment(Qt::AlignCenter);

    closeButton_ = new QPushButton(headerBar_);
    closeButton_->setObjectName(QStringLiteral("clearButton"));
    closeButton_->setText(QStringLiteral("Close"));
    closeButton_->setCursor(Qt::PointingHandCursor);
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        closePanel();
    });

    headerLayout_->addWidget(titleLabel_);
    headerLayout_->addWidget(countBadgeLabel_, 0, Qt::AlignVCenter);
    headerLayout_->addStretch(1);
    headerLayout_->addWidget(closeButton_, 0, Qt::AlignVCenter);
    panelLayout_->addWidget(headerBar_);

    scrollArea_ = new QScrollArea(panelRoot_);
    scrollArea_->setObjectName(QStringLiteral("notificationListScrollArea"));
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->viewport()->setObjectName(QStringLiteral("notificationListViewport"));
    scrollArea_->verticalScrollBar()->setObjectName(QStringLiteral("notificationListScrollBar"));

    listContainer_ = new QWidget(scrollArea_);
    listContainer_->setObjectName(QStringLiteral("notificationListContainer"));
    listLayout_ = new QVBoxLayout(listContainer_);
    listLayout_->setContentsMargins(0, 0, 0, 0);
    listLayout_->setSpacing(10);
    listLayout_->addStretch(1);

    scrollArea_->setWidget(listContainer_);
    panelLayout_->addWidget(scrollArea_, 1);

    footerBar_ = new QFrame(panelRoot_);
    footerBar_->setObjectName(QStringLiteral("footerBar"));
    footerLayout_ = new QHBoxLayout(footerBar_);
    footerLayout_->setContentsMargins(12, 0, 12, 0);

    clearButton_ = new QPushButton(footerBar_);
    clearButton_->setObjectName(QStringLiteral("clearButton"));
    clearButton_->setText(QStringLiteral("Clear"));
    clearButton_->setCursor(Qt::PointingHandCursor);
    connect(clearButton_, &QPushButton::clicked, this, [this]() {
        clearNotifications(2);
    });

    footerLayout_->addStretch(1);
    footerLayout_->addWidget(clearButton_, 0, Qt::AlignVCenter);
    footerLayout_->addStretch(1);
    panelLayout_->addWidget(footerBar_);

    sideHandle_ = new QFrame(panelRoot_);
    sideHandle_->setObjectName(QStringLiteral("sideHandle"));

    applyUiState();
    updatePanelRootGeometry();
}

void NotificationCenterPanel::reloadConfiguration()
{
    if (configLoader_) {
        configLoader_->reload();
    }
}

void NotificationCenterPanel::applyConfig(const WardNcConfig &config)
{
    const QString previousStatePath = stateFilePath();
    config_ = config;
    const bool wasVisible = isVisible();

    Qt::WindowFlags flags = Qt::FramelessWindowHint;
    if (usesLayerShellPlacement()) {
        flags |= Qt::Window;
    } else {
        flags |= Qt::Tool;
        if (config_.layout.aboveWindows) {
            flags |= Qt::WindowStaysOnTopHint;
        }
        if (!config_.layout.focusable) {
            flags |= Qt::WindowDoesNotAcceptFocus;
        }
    }

    const bool needsFlagUpdate = (flags != windowFlags());
    if (needsFlagUpdate) {
        setWindowFlags(flags);
    }

    if (!config_.notifications.keepOnReload) {
        clearNotifications(4);
        historyRecordKeys_.clear();
        nextHistoryId_ = 1000000000U;
        loadNotificationHistory();
    } else {
        for (NotificationEntry *entry : entries_) {
            restartExpiryTimer(entry);
        }
    }

    applyUiState();

    if (previousStatePath != stateFilePath()) {
        refreshStateWatcher();
    }

    const QString state = readStateFile();
    if (!state.isEmpty()) {
        open_ = (state == QStringLiteral("open"));
    }

    refreshWindowPlacement(false);

    if (needsFlagUpdate && wasVisible) {
        show();
    }
}

void NotificationCenterPanel::applyStyle(const QString &styleSheet,
                                         const QHash<QString, QString> &styleVariables)
{
    styleSheet_ = styleSheet;
    styleVariables_ = styleVariables;

    refreshStyleMetrics();
    setStyleSheet(styleSheet_);
    applyStyleMetrics();

    for (NotificationEntry *entry : entries_) {
        refreshEntryWidgets(entry);
    }

    rebuildList();
    updateHeader();
    updateFooter();
}

void NotificationCenterPanel::applyUiState()
{
    titleLabel_->setText(config_.panel.title);
    headerBar_->setVisible(config_.panel.showHeader);
    footerBar_->setVisible(config_.panel.showFooter);
    clearButton_->setVisible(config_.panel.clearButton);
    sideHandle_->setVisible(config_.panel.showHandle);
    applyScrollbarPlacement();

    setFocusPolicy(config_.layout.focusable ? Qt::StrongFocus : Qt::NoFocus);

    refreshStateWatcher();
    refreshHistoryWatcher();
    applyStyleMetrics();
    updateHeader();
    updateFooter();
}

void NotificationCenterPanel::refreshStyleMetrics()
{
    styleMetrics_.panelPaddingX = styleLength(QStringLiteral("--panel-padding-x"), 12);
    styleMetrics_.panelPaddingY = styleLength(QStringLiteral("--panel-padding-y"), 12);
    styleMetrics_.panelSectionGap = styleLength(QStringLiteral("--panel-section-gap"), 10);
    styleMetrics_.headerHeight = styleLength(QStringLiteral("--header-height"), 36);
    styleMetrics_.headerPaddingX = styleLength(QStringLiteral("--header-padding-x"), 12);
    styleMetrics_.headerGap = styleLength(QStringLiteral("--header-gap"), 8);
    styleMetrics_.badgeHeight = styleLength(QStringLiteral("--badge-height"), 22);
    styleMetrics_.badgeMinWidth = styleLength(QStringLiteral("--badge-min-width"), 30);
    styleMetrics_.badgePaddingX = styleLength(QStringLiteral("--badge-padding-x"), 14);
    styleMetrics_.notificationGap = styleLength(QStringLiteral("--notification-gap"), 10);
    styleMetrics_.cardPaddingX = styleLength(QStringLiteral("--card-padding-x"), 14);
    styleMetrics_.cardPaddingY = styleLength(QStringLiteral("--card-padding-y"), 14);
    styleMetrics_.cardGap = styleLength(QStringLiteral("--card-gap"), 12);
    styleMetrics_.iconSize = kNormalizedIconSize;
    styleMetrics_.iconPadding = 0;
    styleMetrics_.textGap = styleLength(QStringLiteral("--text-gap"), 6);
    styleMetrics_.bodyMaxLines = styleLength(QStringLiteral("--body-max-lines"), 0);
    styleMetrics_.dismissButtonSize = styleLength(QStringLiteral("--dismiss-button-size"), 20);
    styleMetrics_.actionGap = styleLength(QStringLiteral("--action-gap"), 6);
    styleMetrics_.actionHeight = styleLength(QStringLiteral("--action-height"), 28);
    styleMetrics_.actionMinWidth = styleLength(QStringLiteral("--action-min-width"), 80);
    styleMetrics_.actionHorizontalPadding = styleLength(QStringLiteral("--action-horizontal-padding"), 22);
    styleMetrics_.footerHeight = styleLength(QStringLiteral("--footer-height"), 34);
    styleMetrics_.footerPaddingX = styleLength(QStringLiteral("--footer-padding-x"), 12);
    styleMetrics_.clearHeight = styleLength(QStringLiteral("--clear-height"), 22);
    styleMetrics_.clearMinWidth = styleLength(QStringLiteral("--clear-min-width"), 56);
    styleMetrics_.clearPaddingX = styleLength(QStringLiteral("--clear-padding-x"), 4);
    styleMetrics_.clearShadowBlur = styleLength(QStringLiteral("--clear-shadow-blur"), 10);
    styleMetrics_.clearShadowOffsetX = styleLength(QStringLiteral("--clear-shadow-offset-x"), 0);
    styleMetrics_.clearShadowOffsetY = styleLength(QStringLiteral("--clear-shadow-offset-y"), 2);
    styleMetrics_.clearShadowMarginY = styleLength(QStringLiteral("--clear-shadow-margin-y"), 4);
    clearButtonShadowColor_ = styleColorValue(styleVariables_,
                                              QStringLiteral("--clear-shadow-color"),
                                              QColor(0, 0, 0, 0));
}

void NotificationCenterPanel::applyStyleMetrics()
{
    if (!panelLayout_ || !headerLayout_ || !listLayout_ || !footerLayout_) {
        return;
    }

    panelLayout_->setContentsMargins(styleMetrics_.panelPaddingX,
                                     styleMetrics_.panelPaddingY,
                                     styleMetrics_.panelPaddingX,
                                     styleMetrics_.panelPaddingY);
    panelLayout_->setSpacing(styleMetrics_.panelSectionGap);

    headerBar_->setFixedHeight(qMax(1, styleMetrics_.headerHeight));
    const int clearShadowMarginY = qMax(0, styleMetrics_.clearShadowMarginY);
    headerLayout_->setContentsMargins(styleMetrics_.headerPaddingX,
                                      clearShadowMarginY,
                                      styleMetrics_.headerPaddingX,
                                      clearShadowMarginY);
    headerLayout_->setSpacing(styleMetrics_.headerGap);

    countBadgeLabel_->setMinimumHeight(qMax(1, styleMetrics_.badgeHeight));
    countBadgeLabel_->setMinimumWidth(qMax(1, styleMetrics_.badgeMinWidth));
    countBadgeLabel_->setContentsMargins(styleMetrics_.badgePaddingX, 0, styleMetrics_.badgePaddingX, 0);

    if (closeButton_) {
        closeButton_->setFixedHeight(qMax(1, styleMetrics_.clearHeight));
        closeButton_->setMinimumWidth(qMax(1, styleMetrics_.clearMinWidth));
        closeButton_->setContentsMargins(styleMetrics_.clearPaddingX, 0, styleMetrics_.clearPaddingX, 0);
    }

    clearButton_->setFixedHeight(qMax(1, styleMetrics_.clearHeight));
    clearButton_->setMinimumWidth(qMax(1, styleMetrics_.clearMinWidth));
    clearButton_->setContentsMargins(styleMetrics_.clearPaddingX, 0, styleMetrics_.clearPaddingX, 0);
    applyClearButtonShadows();

    listLayout_->setSpacing(styleMetrics_.notificationGap);

    footerBar_->setFixedHeight(qMax(1, styleMetrics_.footerHeight));
    footerLayout_->setContentsMargins(styleMetrics_.footerPaddingX,
                                      clearShadowMarginY,
                                      styleMetrics_.footerPaddingX,
                                      clearShadowMarginY);

    const int handleWidth = qMax(6, qMin(16, qMax(config_.layout.peekWidth, 1)));
    const int handleHeight = qMax(40, qMin(96, height() / 4));
    sideHandle_->setFixedSize(handleWidth, handleHeight);

    for (NotificationEntry *entry : entries_) {
        refreshEntryWidgets(entry);
    }

    rebuildList();
}

void NotificationCenterPanel::applyClearButtonShadows()
{
    const auto applyShadow = [this](QPushButton *button) {
        if (!button) {
            return;
        }

        if (styleMetrics_.clearShadowBlur <= 0 || clearButtonShadowColor_.alpha() <= 0) {
            button->setGraphicsEffect(nullptr);
            return;
        }

        auto *effect = qobject_cast<QGraphicsDropShadowEffect *>(button->graphicsEffect());
        if (!effect) {
            effect = new QGraphicsDropShadowEffect(button);
            button->setGraphicsEffect(effect);
        }

        effect->setBlurRadius(styleMetrics_.clearShadowBlur);
        effect->setOffset(styleMetrics_.clearShadowOffsetX, styleMetrics_.clearShadowOffsetY);
        effect->setColor(clearButtonShadowColor_);
    };

    applyShadow(closeButton_);
    applyShadow(clearButton_);
}

void NotificationCenterPanel::updateHeader()
{
    const int count = entries_.size();
    countBadgeLabel_->setText(QString::number(count));
    countBadgeLabel_->setVisible(count > 0);
    clearButton_->setEnabled(count > 0);
    ensureCloseButtonInteractivity();
}

void NotificationCenterPanel::updateFooter()
{
    clearButton_->setVisible(config_.panel.clearButton);
}

void NotificationCenterPanel::rebuildList()
{
    if (!listLayout_) {
        return;
    }

    for (NotificationEntry *entry : entries_) {
        if (entry && entry->card) {
            entry->card->setVisible(false);
        }
    }

    while (QLayoutItem *item = listLayout_->takeAt(0)) {
        delete item;
    }

    int shown = 0;
    if (config_.panel.reverseOrder) {
        for (int index = entries_.size() - 1; index >= 0; --index) {
            if (shown >= config_.panel.maxVisibleNotifications) {
                break;
            }
            NotificationEntry *entry = entries_.at(index);
            if (!entry || !entry->card) {
                continue;
            }
            entry->card->setVisible(true);
            listLayout_->addWidget(entry->card);
            ++shown;
        }
    } else {
        for (NotificationEntry *entry : entries_) {
            if (shown >= config_.panel.maxVisibleNotifications) {
                break;
            }
            if (!entry || !entry->card) {
                continue;
            }
            entry->card->setVisible(true);
            listLayout_->addWidget(entry->card);
            ++shown;
        }
    }

    listLayout_->addStretch(1);
}

void NotificationCenterPanel::clearNotifications(uint reason)
{
    while (!entries_.isEmpty()) {
        removeEntry(entries_.first(), reason);
    }
}

void NotificationCenterPanel::enforceCapacity()
{
    while (entries_.size() > config_.panel.maxTrackedNotifications) {
        NotificationEntry *oldest = entries_.first();
        if (!oldest) {
            break;
        }
        removeEntry(oldest, 4);
    }
}

NotificationEntry *NotificationCenterPanel::findEntryById(uint id) const
{
    return entriesById_.value(id, nullptr);
}

NotificationEntry *NotificationCenterPanel::findEntryByStackTag(const QString &stackTag) const
{
    return entriesByStackTag_.value(stackTag, nullptr);
}

NotificationEntry *NotificationCenterPanel::createEntry(uint id,
                                                        const QString &appName,
                                                        const QString &summary,
                                                        const QString &body,
                                                        const QString &iconName,
                                                        int timeoutMs,
                                                        const QStringList &actions,
                                                        const QVariantMap &hints)
{
    auto *entry = new NotificationEntry;
    entry->id = id;
    entry->createdAt = QDateTime::currentDateTime();

    updateEntry(entry, appName, summary, body, iconName, timeoutMs, actions, hints);

    entries_.append(entry);
    entriesById_.insert(entry->id, entry);
    if (!entry->stackTag.isEmpty()) {
        entriesByStackTag_.insert(entry->stackTag, entry);
    }

    restartExpiryTimer(entry);
    return entry;
}

void NotificationCenterPanel::updateEntry(NotificationEntry *entry,
                                          const QString &appName,
                                          const QString &summary,
                                          const QString &body,
                                          const QString &iconName,
                                          int timeoutMs,
                                          const QStringList &actions,
                                          const QVariantMap &hints)
{
    if (!entry) {
        return;
    }

    if (!entry->stackTag.isEmpty() && entriesByStackTag_.value(entry->stackTag) == entry) {
        entriesByStackTag_.remove(entry->stackTag);
    }

    entry->appName = appName;
    entry->summary = summary;
    entry->body = body;
    entry->iconName = iconName;
    entry->timeoutMs = timeoutMs;
    entry->hints = hints;
    entry->actions = parseActions(actions);
    entry->stackTag = notificationStackTag(entry->hints);
    entry->createdAt = QDateTime::currentDateTime();

    if (!entry->stackTag.isEmpty()) {
        entriesByStackTag_.insert(entry->stackTag, entry);
    }

    refreshEntryWidgets(entry);
    restartExpiryTimer(entry);
}

void NotificationCenterPanel::removeEntry(NotificationEntry *entry, uint reason, bool emitSignal)
{
    if (!entry) {
        return;
    }

    entries_.removeAll(entry);
    entriesById_.remove(entry->id);
    if (!entry->stackTag.isEmpty() && entriesByStackTag_.value(entry->stackTag) == entry) {
        entriesByStackTag_.remove(entry->stackTag);
    }

    if (entry->expiryTimer) {
        entry->expiryTimer->stop();
        entry->expiryTimer->deleteLater();
        entry->expiryTimer = nullptr;
    }

    if (entry->card) {
        entry->card->deleteLater();
        entry->card = nullptr;
    }

    const uint id = entry->id;
    delete entry;

    rebuildList();
    updateHeader();
    updateFooter();

    if (emitSignal && !entry->historyOnly) {
        emit notificationClosed(id, reason);
    }
}

void NotificationCenterPanel::refreshEntryWidgets(NotificationEntry *entry)
{
    if (!entry) {
        return;
    }

    if (entry->card) {
        entry->card->deleteLater();
        entry->card = nullptr;
    }

    auto *card = new QFrame(listContainer_);
    card->setObjectName(QStringLiteral("notificationCard"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(styleMetrics_.cardPaddingX,
                                   styleMetrics_.cardPaddingY,
                                   styleMetrics_.cardPaddingX,
                                   styleMetrics_.cardPaddingY);
    cardLayout->setSpacing(styleMetrics_.cardGap);

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(styleMetrics_.cardGap);

    auto *iconFrame = new QFrame(card);
    iconFrame->setObjectName(QStringLiteral("iconFrame"));
    iconFrame->setFixedSize(styleMetrics_.iconSize, styleMetrics_.iconSize);

    auto *iconLayout = new QVBoxLayout(iconFrame);
    iconLayout->setContentsMargins(styleMetrics_.iconPadding,
                                   styleMetrics_.iconPadding,
                                   styleMetrics_.iconPadding,
                                   styleMetrics_.iconPadding);
    iconLayout->setSpacing(0);

    auto *iconLabel = new QLabel(iconFrame);
    iconLabel->setObjectName(QStringLiteral("iconLabel"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLayout->addWidget(iconLabel);

    QPixmap iconPixmap = notificationPixmap(entry);
    if (!iconPixmap.isNull()) {
        iconPixmap = iconPixmap.scaled(styleMetrics_.iconSize - (styleMetrics_.iconPadding * 2),
                                       styleMetrics_.iconSize - (styleMetrics_.iconPadding * 2),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        iconLabel->setPixmap(iconPixmap);
        iconFrame->setVisible(true);
    } else {
        iconFrame->setVisible(false);
    }

    auto *textColumn = new QVBoxLayout();
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(styleMetrics_.textGap);

    auto *summaryLabel = new QLabel(card);
    summaryLabel->setObjectName(QStringLiteral("summaryLabel"));
    summaryLabel->setWordWrap(true);
    summaryLabel->setTextFormat(Qt::PlainText);

    auto *bodyLabel = new QLabel(card);
    bodyLabel->setObjectName(QStringLiteral("bodyLabel"));
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextFormat(Qt::PlainText);

    const QString summaryText = normalizedSummary(entry->summary, entry->body);
    const QString bodyText = sanitizeText(entry->body).trimmed();

    summaryLabel->setText(summaryText);
    bodyLabel->setText(bodyText);
    bodyLabel->setVisible(!bodyText.isEmpty());

    if (styleMetrics_.bodyMaxLines > 0 && bodyLabel->isVisible()) {
        QStringList lines = bodyLabel->text().split(QLatin1Char('\n'));
        if (lines.size() > styleMetrics_.bodyMaxLines) {
            lines = lines.mid(0, styleMetrics_.bodyMaxLines);
            QString &lastLine = lines.last();
            lastLine = lastLine + QStringLiteral("…");
            bodyLabel->setText(lines.join(QLatin1Char('\n')));
        }
    }

    auto *metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(styleMetrics_.textGap);

    auto *appLabel = new QLabel(card);
    appLabel->setObjectName(QStringLiteral("appNameLabel"));
    appLabel->setText(displayAppName(entry->appName));
    appLabel->setVisible(config_.panel.showAppName && !appLabel->text().trimmed().isEmpty());

    auto *timestampLabel = new QLabel(card);
    timestampLabel->setObjectName(QStringLiteral("timestampLabel"));
    timestampLabel->setText(timestampText(entry->createdAt));
    timestampLabel->setVisible(config_.panel.showTimestamp);

    auto *timestampDayLabel = new QLabel(card);
    timestampDayLabel->setObjectName(QStringLiteral("timestampDayLabel"));
    timestampDayLabel->setText(timestampDayText(entry->createdAt));
    timestampDayLabel->setVisible(config_.panel.showTimestamp);

    metaRow->addWidget(appLabel);
    metaRow->addStretch(1);
    metaRow->addWidget(timestampDayLabel);
    metaRow->addWidget(timestampLabel);

    textColumn->addWidget(summaryLabel);
    textColumn->addWidget(bodyLabel);
    textColumn->addLayout(metaRow);

    auto *dismissButton = new QPushButton(card);
    dismissButton->setObjectName(QStringLiteral("dismissButton"));
    dismissButton->setFixedSize(styleMetrics_.dismissButtonSize, styleMetrics_.dismissButtonSize);
    dismissButton->setText(QString());
    const QColor dismissColor = styleColorValue(styleVariables_,
                                                QStringLiteral("--dismiss-color"),
                                                QColor(QStringLiteral("#d2d8e4")));
    const int dismissGlyphSize = qMax(10, styleMetrics_.dismissButtonSize - 4);
    dismissButton->setIcon(dismissGlyphIcon(dismissGlyphSize, dismissColor));
    dismissButton->setIconSize(QSize(dismissGlyphSize, dismissGlyphSize));
    dismissButton->setCursor(Qt::PointingHandCursor);

    connect(dismissButton, &QPushButton::clicked, this, [this, entry]() {
        removeEntry(entry, 2);
    });

    topRow->addWidget(iconFrame, 0, Qt::AlignVCenter);
    topRow->addLayout(textColumn, 1);
    topRow->addWidget(dismissButton, 0, Qt::AlignTop);

    cardLayout->addLayout(topRow);

    rebuildEntryActions(entry, cardLayout);

    entry->card = card;
    entry->summaryLabel = summaryLabel;
    entry->bodyLabel = bodyLabel;
    entry->appLabel = appLabel;
    entry->timestampDayLabel = timestampDayLabel;
    entry->timestampLabel = timestampLabel;
    entry->iconLabel = iconLabel;
}

void NotificationCenterPanel::rebuildEntryActions(NotificationEntry *entry, QVBoxLayout *cardLayout)
{
    if (!entry || !cardLayout) {
        return;
    }

    if (!config_.panel.showActions || !config_.notifications.actionsSupported || entry->actions.isEmpty()) {
        return;
    }

    auto *actionsRow = new QHBoxLayout();
    actionsRow->setContentsMargins(0, 0, 0, 0);
    actionsRow->setSpacing(styleMetrics_.actionGap);

    for (const NotificationActionItem &action : entry->actions) {
        if (action.key.trimmed().isEmpty()) {
            continue;
        }

        auto *button = new QPushButton(action.label.trimmed().isEmpty() ? action.key : action.label,
                                       entry->card);
        button->setObjectName(QStringLiteral("actionButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumWidth(styleMetrics_.actionMinWidth);
        button->setFixedHeight(styleMetrics_.actionHeight);
        button->setContentsMargins(styleMetrics_.actionHorizontalPadding,
                                   0,
                                   styleMetrics_.actionHorizontalPadding,
                                   0);

        const uint notificationId = entry->id;
        const QString actionKey = action.key;
        connect(button, &QPushButton::clicked, this, [this, notificationId, actionKey]() {
            emit actionInvoked(notificationId, actionKey);
        });

        actionsRow->addWidget(button);
    }

    if (actionsRow->count() > 0) {
        actionsRow->addStretch(1);
        cardLayout->addLayout(actionsRow);
    } else {
        delete actionsRow;
    }
}

QList<NotificationActionItem> NotificationCenterPanel::parseActions(const QStringList &actions) const
{
    QList<NotificationActionItem> parsedActions;
    if (actions.isEmpty()) {
        return parsedActions;
    }

    for (int index = 0; index + 1 < actions.size(); index += 2) {
        const QString key = actions.at(index).trimmed();
        const QString label = actions.at(index + 1).trimmed();
        if (key.isEmpty()) {
            continue;
        }

        NotificationActionItem item;
        item.key = key;
        item.label = label.isEmpty() ? key : label;
        parsedActions.append(item);
    }

    return parsedActions;
}

QString NotificationCenterPanel::stripMarkup(const QString &text) const
{
    static const QRegularExpression tagPattern(QStringLiteral(R"(<[^>]+>)"));
    QString stripped = text;
    stripped.replace(tagPattern, QString());
    return stripped;
}

QString NotificationCenterPanel::decodeEntities(const QString &text) const
{
    QString output = text;

    output.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    output.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    output.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    output.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    output.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    output.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));

    static const QRegularExpression decEntity(QStringLiteral(R"(&#(\d+);)"));
    QRegularExpressionMatchIterator decMatches = decEntity.globalMatch(output);
    while (decMatches.hasNext()) {
        const QRegularExpressionMatch match = decMatches.next();
        bool ok = false;
        const int codepoint = match.captured(1).toInt(&ok);
        if (!ok || codepoint < 0) {
            continue;
        }
        output.replace(match.captured(0), QString(QChar(codepoint)));
    }

    static const QRegularExpression hexEntity(QStringLiteral(R"(&#x([0-9A-Fa-f]+);)"));
    QRegularExpressionMatchIterator hexMatches = hexEntity.globalMatch(output);
    while (hexMatches.hasNext()) {
        const QRegularExpressionMatch match = hexMatches.next();
        bool ok = false;
        const int codepoint = match.captured(1).toInt(&ok, 16);
        if (!ok || codepoint < 0) {
            continue;
        }
        output.replace(match.captured(0), QString(QChar(codepoint)));
    }

    return output;
}

QString NotificationCenterPanel::sanitizeText(const QString &text) const
{
    QString result = text;

    result.replace(QChar::Nbsp, QLatin1Char(' '));
    result.replace(QChar(0x202F), QLatin1Char(' '));
    result.replace(QChar(0x2007), QLatin1Char(' '));
    result.replace(QChar(0xFEFF), QLatin1Char(' '));
    result.remove(QChar(0x2060));

    if (config_.text.stripPangoMarkup) {
        result = stripMarkup(result);
    }

    if (config_.text.decodeEntities) {
        result = decodeEntities(result);
    }

    return result;
}

QString NotificationCenterPanel::normalizedSummary(const QString &summary, const QString &body) const
{
    const QString sanitizedSummary = sanitizeText(summary).trimmed();
    if (!sanitizedSummary.isEmpty()) {
        return sanitizedSummary;
    }

    const QString sanitizedBody = sanitizeText(body).trimmed();
    if (!sanitizedBody.isEmpty()) {
        return config_.text.summaryFallback;
    }

    return config_.text.summaryFallback;
}

QString NotificationCenterPanel::displayAppName(const QString &appName) const
{
    const QString cleaned = sanitizeText(appName).trimmed();
    return cleaned;
}

QString NotificationCenterPanel::timestampText(const QDateTime &timestamp) const
{
    if (!timestamp.isValid()) {
        return {};
    }

    return config_.panel.timeFormat24h
        ? timestamp.toString(QStringLiteral("HH:mm"))
        : timestamp.toString(QStringLiteral("h:mm AP"));
}

QString NotificationCenterPanel::timestampDayText(const QDateTime &timestamp) const
{
    if (!timestamp.isValid()) {
        return {};
    }

    return timestamp.toString(QStringLiteral("ddd"));
}

QPixmap NotificationCenterPanel::notificationPixmap(const NotificationEntry *entry) const
{
    if (!entry) {
        return {};
    }

    const QStringList imageDataKeys = {
        QStringLiteral("image-data"),
        QStringLiteral("image_data"),
        QStringLiteral("icon_data")
    };

    for (const QString &key : imageDataKeys) {
        const QPixmap pixmap = loadPixmapFromImageData(unwrapHintValue(entry->hints.value(key)));
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }

    const QStringList imagePathKeys = {
        QStringLiteral("image-path"),
        QStringLiteral("image_path")
    };

    for (const QString &key : imagePathKeys) {
        const QString path = unwrapHintValue(entry->hints.value(key)).toString().trimmed();
        if (path.isEmpty()) {
            continue;
        }

        const QPixmap pixmap = pixmapFromPath(path);
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }

    if (!entry->iconName.trimmed().isEmpty()) {
        const QPixmap filePixmap = pixmapFromPath(entry->iconName.trimmed());
        if (!filePixmap.isNull()) {
            return filePixmap;
        }

        const QIcon icon = QIcon::fromTheme(entry->iconName.trimmed());
        const int desiredSize = qBound(0, config_.notifications.maxIconSize, kMaxIconSize);
        const QPixmap themePixmap = icon.pixmap(QSize(desiredSize, desiredSize));
        if (!themePixmap.isNull()) {
            return themePixmap;
        }
    }

    return {};
}

int NotificationCenterPanel::effectiveTimeoutMs(const NotificationEntry *entry) const
{
    if (!entry || !config_.notifications.autoExpire) {
        return 0;
    }

    if (entry->timeoutMs == 0) {
        return 0;
    }

    if (config_.notifications.respectTimeout && entry->timeoutMs > 0) {
        return entry->timeoutMs;
    }

    if (entry->timeoutMs < 0 && notificationUrgency(entry->hints) >= 2) {
        return 0;
    }

    return config_.notifications.defaultTimeoutMs;
}

void NotificationCenterPanel::restartExpiryTimer(NotificationEntry *entry)
{
    if (!entry) {
        return;
    }

    if (!entry->expiryTimer) {
        entry->expiryTimer = new QTimer(this);
        entry->expiryTimer->setSingleShot(true);

        const uint id = entry->id;
        connect(entry->expiryTimer, &QTimer::timeout, this, [this, id]() {
            NotificationEntry *current = findEntryById(id);
            if (current) {
                removeEntry(current, 1);
            }
        });
    }

    entry->expiryTimer->stop();

    const int timeout = effectiveTimeoutMs(entry);
    if (timeout > 0) {
        entry->expiryTimer->start(timeout);
    }
}

int NotificationCenterPanel::styleLength(const QString &name, int fallback) const
{
    if (!styleVariables_.contains(name)) {
        return fallback;
    }

    int parsed = 0;
    if (!parseStyleLength(styleVariables_.value(name), &parsed)) {
        return fallback;
    }

    return qMax(0, parsed);
}

QScreen *NotificationCenterPanel::resolveScreen() const
{
    if (useCursorScreenForNextPlacement_) {
        if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
            return screen;
        }
    }

    const QString requestedScreen = config_.layout.screen.trimmed();

    if (requestedScreen.compare(QStringLiteral("primary"), Qt::CaseInsensitive) == 0 ||
        requestedScreen.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }

    if (requestedScreen.compare(QStringLiteral("cursor"), Qt::CaseInsensitive) == 0) {
        if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
            return screen;
        }
        return QGuiApplication::primaryScreen();
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen && screen->name().compare(requestedScreen, Qt::CaseInsensitive) == 0) {
            return screen;
        }
    }

    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos())) {
        return screen;
    }

    return QGuiApplication::primaryScreen();
}

QPoint NotificationCenterPanel::openPosition(const QRect &availableGeometry, int panelHeight) const
{
    const int x = panelOnRightSide()
        ? availableGeometry.right() - config_.layout.marginRight - width() + 1
        : availableGeometry.left() + config_.layout.marginLeft;

    const int y = anchorAtTop()
        ? availableGeometry.top() + config_.layout.marginTop
        : availableGeometry.bottom() - config_.layout.marginBottom - panelHeight + 1;

    return QPoint(x, y);
}

QPoint NotificationCenterPanel::closedPosition(const QRect &availableGeometry, int panelHeight) const
{
    const QPoint opened = openPosition(availableGeometry, panelHeight);
    return QPoint(opened.x() + closedPanelOffset(), opened.y());
}

bool NotificationCenterPanel::anchorAtRight() const
{
    return config_.layout.anchor.trimmed().toLower().endsWith(QStringLiteral("right"));
}

bool NotificationCenterPanel::panelOnRightSide() const
{
    const QString side = config_.layout.monitorPosition.trimmed().toLower();
    if (side == QStringLiteral("left")) {
        return false;
    }
    if (side == QStringLiteral("right")) {
        return true;
    }

    return anchorAtRight();
}

bool NotificationCenterPanel::scrollbarOnLeft() const
{
    const QString position = config_.panel.scrollbarPosition.trimmed().toLower();
    if (position == QStringLiteral("none")) {
        return false;
    }
    if (position == QStringLiteral("left")) {
        return true;
    }
    if (position == QStringLiteral("right")) {
        return false;
    }

    return !panelOnRightSide();
}

bool NotificationCenterPanel::scrollbarHidden() const
{
    return config_.panel.scrollbarPosition.trimmed().compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

bool NotificationCenterPanel::anchorAtTop() const
{
    return config_.layout.anchor.trimmed().toLower().startsWith(QStringLiteral("top"));
}

bool NotificationCenterPanel::layerShellUsesRightAnchor() const
{
    return panelOnRightSide();
}

bool NotificationCenterPanel::slideTowardRight() const
{
    const QString direction = config_.animation.slideDirection.trimmed().toLower();
    if (direction == QStringLiteral("left")) {
        return false;
    }
    if (direction == QStringLiteral("right")) {
        return true;
    }

    return panelOnRightSide();
}

bool NotificationCenterPanel::usesLayerShellPlacement() const
{
#if WARDNC_HAS_LAYERSHELLQT
    return QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
#else
    return false;
#endif
}

QRect NotificationCenterPanel::layerShellPlacementGeometry(QScreen *screen) const
{
    if (!screen) {
        return {};
    }

    QRect geometry = screen->availableGeometry();
    const QRect screenGeometry = screen->geometry();
    geometry.setLeft(screenGeometry.left());
    geometry.setRight(screenGeometry.right());
    return geometry;
}

int NotificationCenterPanel::closedPanelOffset() const
{
    const int hiddenDistance = qMax(
        0,
        width() + qMax(0, config_.animation.slideDistance));
    return slideTowardRight() ? hiddenDistance : -hiddenDistance;
}

int NotificationCenterPanel::panelOffsetForState(bool open) const
{
    return open ? 0 : closedPanelOffset();
}

void NotificationCenterPanel::applyPanelOffset(int offset)
{
    panelOffsetX_ = offset;
    if (!panelRoot_) {
        updateInputMask();
        return;
    }

    panelRoot_->move(panelOffsetX_, 0);
    updateInputMask();
}

void NotificationCenterPanel::animatePanelOffsetTo(int offset)
{
    if (!panelRoot_) {
        return;
    }

    if (!config_.animation.enabled) {
        applyPanelOffset(offset);
        return;
    }

    if (slideAnimation_) {
        slideAnimation_->stop();
        slideAnimation_->deleteLater();
        slideAnimation_ = nullptr;
    }

    slideAnimation_ = new QVariantAnimation(this);
    slideAnimation_->setDuration(config_.animation.durationMs);
    slideAnimation_->setEasingCurve(easingFromName(config_.animation.easing));
    slideAnimation_->setStartValue(panelOffsetX_);
    slideAnimation_->setEndValue(offset);
    connect(slideAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        applyPanelOffset(value.toInt());
    });
    slideAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationCenterPanel::updatePanelRootGeometry()
{
    if (!panelRoot_) {
        updateInputMask();
        return;
    }

    panelRoot_->setFixedSize(size());
    applyPanelOffset(panelOffsetX_);
}

void NotificationCenterPanel::updateInputMask()
{
    const QRect viewport(0, 0, width(), height());
    if (!viewport.isValid() || viewport.isEmpty()) {
        setMask(QRegion());
        return;
    }

    if (!usesLayerShellPlacement()) {
        setMask(QRegion(viewport));
        return;
    }

    if (!panelRoot_) {
        setMask(QRegion(viewport));
        return;
    }

    const QRect panelRect(panelOffsetX_, 0, panelRoot_->width(), panelRoot_->height());
    const QRect visibleRect = viewport.intersected(panelRect);
    if (!visibleRect.isValid() || visibleRect.isEmpty()) {
        setMask(QRegion());
        return;
    }

    setMask(QRegion(visibleRect));
}

void NotificationCenterPanel::applyScrollbarPlacement()
{
    if (!scrollArea_ || !listContainer_) {
        return;
    }

    if (scrollbarHidden()) {
        scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea_->setLayoutDirection(Qt::LeftToRight);
        scrollArea_->viewport()->setLayoutDirection(Qt::LeftToRight);
        listContainer_->setLayoutDirection(Qt::LeftToRight);
        return;
    }

    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const bool onLeft = scrollbarOnLeft();
    scrollArea_->setLayoutDirection(onLeft ? Qt::RightToLeft : Qt::LeftToRight);
    scrollArea_->viewport()->setLayoutDirection(Qt::LeftToRight);
    listContainer_->setLayoutDirection(Qt::LeftToRight);
}

void NotificationCenterPanel::positionSideHandle()
{
    if (!sideHandle_) {
        return;
    }

    const int handleWidth = sideHandle_->width();
    const int handleHeight = sideHandle_->height();
    const int handleX = panelOnRightSide()
        ? 0
        : qMax(0, width() - handleWidth);
    sideHandle_->move(handleX, qMax(0, (height() - handleHeight) / 2));
    sideHandle_->setFixedWidth(handleWidth);
}

void NotificationCenterPanel::refreshWindowPlacement(bool animated)
{
    constexpr int kMonitorBottomInsetPx = 80;
    constexpr int kCenteredHeightPercentWithWaybar = 90;
    constexpr int kCenteredHeightPercentWithoutWaybar = 100;
    constexpr int kWaybarGapReductionPx = 20;

    QScreen *screen = resolveScreen();
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const QRect screenGeometry = screen->geometry();
    const WaybarLayerInfo waybarInfo = waybarLayerInfoOnScreen(screen);
    const int reservedTop = hyprReservedTopOnScreen(screen);
    const bool waybarActive = anchorAtTop() && !(waybarInfo.level == 1 && reservedTop == 0);
    const int centeredHeightPercent = waybarActive
        ? kCenteredHeightPercentWithWaybar
        : kCenteredHeightPercentWithoutWaybar;
    const int topInset = waybarActive ? waybarInfo.topInset : 0;
    const int bottomInset = waybarActive ? kMonitorBottomInsetPx : 0;
    const int effectiveTopMargin = waybarActive
        ? qMax(0, config_.layout.marginTop - kWaybarGapReductionPx)
        : config_.layout.marginTop;
    const int topBound = screenGeometry.top() + topInset + effectiveTopMargin;
    const int bottomBound = screenGeometry.bottom() - bottomInset - config_.layout.marginBottom;
    const int maxHeight = qMax(1, bottomBound - topBound + 1);
    const int preferredHeight = qMax(config_.layout.minimumHeight,
                                     (maxHeight * centeredHeightPercent) / 100);
    setFixedWidth(qMax(1, config_.layout.width));
    int panelHeight = qBound(1, preferredHeight, maxHeight);
    if (anchorAtTop() && waybarActive) {
        panelHeight = qBound(1, (panelHeight * 12) / 10, maxHeight);
    }
    setFixedHeight(panelHeight);

    const QRect placementGeometry = usesLayerShellPlacement()
        ? screenGeometry
        : available;

#if WARDNC_HAS_LAYERSHELLQT
    if (usesLayerShellPlacement()) {
        configureLayerShell(screen, panelHeight);
        layerShellPlacementGeometryCache_ = placementGeometry;
    }
#endif

    QPoint target = open_
        ? openPosition(placementGeometry, panelHeight)
        : closedPosition(placementGeometry, panelHeight);

    if (anchorAtTop()) {
        target.setY(topBound + ((maxHeight - panelHeight) / 2));
    }

#if WARDNC_HAS_LAYERSHELLQT
    if (usesLayerShellPlacement()) {
        applyPanelOffset(0);
        updatePanelRootGeometry();

        if (animated) {
            animateToPosition(target);
        } else {
            layerShellPosition_ = target;
            applyLayerShellPlacement(layerShellPosition_, placementGeometry);
        }

        positionSideHandle();
        useCursorScreenForNextPlacement_ = false;
        return;
    }
#endif

    applyPanelOffset(0);
    updatePanelRootGeometry();

    if (animated) {
        animateToPosition(target);
    } else {
        move(target);
    }

    positionSideHandle();
    useCursorScreenForNextPlacement_ = false;
}

void NotificationCenterPanel::setPanelOpen(bool open, bool animated, bool writeState)
{
    const bool stateChanged = (open_ != open);
    if (!open_ && open) {
        useCursorScreenForNextPlacement_ =
            config_.layout.screen.trimmed().compare(QStringLiteral("cursor"), Qt::CaseInsensitive) == 0;
    }

    open_ = open;

    if (open_ && !isVisible()) {
        show();
    }

    ensureCloseButtonInteractivity();

    refreshWindowPlacement(animated);
    applyPanelVisibility(animated);

    if (stateChanged && animated && usesLayerShellPlacement()) {
        const int settleDelayMs = qMax(80, config_.animation.durationMs + 30);
        QTimer::singleShot(settleDelayMs, this, [this, open]() {
            if (open_ != open) {
                return;
            }
            refreshWindowPlacement(false);
        });
    }

    if (writeState) {
        writeStateFile(open_ ? QStringLiteral("open") : QStringLiteral("closed"));
    }
}

void NotificationCenterPanel::ensureCloseButtonInteractivity()
{
    if (!closeButton_) {
        return;
    }

    closeButton_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    closeButton_->setEnabled(open_);
    if (open_) {
        closeButton_->raise();
    }
}

void NotificationCenterPanel::applyPanelVisibility(bool animated)
{
    if (open_) {
        raise();
        return;
    }

    if (usesLayerShellPlacement()) {
        return;
    }

    if (!isVisible()) {
        return;
    }

    const bool deferHide = animated && config_.animation.enabled;
    if (!deferHide) {
        hide();
        return;
    }

    const int hideDelayMs = qMax(80, config_.animation.durationMs + 30);
    QTimer::singleShot(hideDelayMs, this, [this]() {
        if (open_) {
            return;
        }
        hide();
    });
}

void NotificationCenterPanel::animateToPosition(const QPoint &position)
{
    if (usesLayerShellPlacement()) {
#if WARDNC_HAS_LAYERSHELLQT
        animateLayerShellTo(position);
#endif
        return;
    }

    if (!config_.animation.enabled) {
        move(position);
        return;
    }

    if (slideAnimation_) {
        slideAnimation_->stop();
        slideAnimation_->deleteLater();
        slideAnimation_ = nullptr;
    }

    slideAnimation_ = new QVariantAnimation(this);
    slideAnimation_->setDuration(config_.animation.durationMs);
    slideAnimation_->setEasingCurve(easingFromName(config_.animation.easing));
    slideAnimation_->setStartValue(usesLayerShellPlacement()
#if WARDNC_HAS_LAYERSHELLQT
                                       ? layerShellPosition_
#else
                                       ? QPoint()
#endif
                                       : pos());
    slideAnimation_->setEndValue(position);
    connect(slideAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const QPoint current = value.toPoint();
        if (usesLayerShellPlacement()) {
#if WARDNC_HAS_LAYERSHELLQT
            layerShellPosition_ = current;
            applyLayerShellPlacement(layerShellPosition_, layerShellPlacementGeometryCache_);
#endif
            return;
        }

        move(current);
    });
    slideAnimation_->start(QAbstractAnimation::DeleteWhenStopped);

    if (!config_.animation.fade) {
        return;
    }

    if (fadeAnimation_) {
        fadeAnimation_->stop();
        fadeAnimation_->deleteLater();
        fadeAnimation_ = nullptr;
    }

    fadeAnimation_ = new QPropertyAnimation(this, "windowOpacity", this);
    fadeAnimation_->setDuration(config_.animation.fadeDurationMs);
    fadeAnimation_->setEasingCurve(easingFromName(config_.animation.easing));
    fadeAnimation_->setStartValue(windowOpacity());
    fadeAnimation_->setEndValue(open_ ? 1.0 : 0.96);
    fadeAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationCenterPanel::animateLayerShellTo(const QPoint &position)
{
#if WARDNC_HAS_LAYERSHELLQT
    layerShellAnimationTimer_.stop();

    if (!config_.animation.enabled || config_.animation.durationMs <= 0) {
        layerShellPosition_ = position;
        applyLayerShellPlacement(layerShellPosition_, layerShellPlacementGeometryCache_);
        return;
    }

    layerShellAnimationStart_ = layerShellPosition_;
    layerShellAnimationEnd_ = position;
    layerShellAnimationDurationMs_ = config_.animation.durationMs;
    layerShellAnimationStartMs_ = QDateTime::currentMSecsSinceEpoch();
    layerShellAnimationEasing_ = QEasingCurve(easingFromName(config_.animation.easing));
    layerShellAnimationTimer_.start();
    advanceLayerShellAnimation();
#else
    Q_UNUSED(position);
#endif
}

void NotificationCenterPanel::advanceLayerShellAnimation()
{
#if WARDNC_HAS_LAYERSHELLQT
    if (!layerShellAnimationDurationMs_) {
        layerShellAnimationTimer_.stop();
        return;
    }

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - layerShellAnimationStartMs_;
    const qreal progress = qBound<qreal>(0.0,
                                         static_cast<qreal>(elapsedMs) /
                                             static_cast<qreal>(layerShellAnimationDurationMs_),
                                         1.0);
    const qreal eased = layerShellAnimationEasing_.valueForProgress(progress);
    const int x = layerShellAnimationStart_.x() +
        static_cast<int>((layerShellAnimationEnd_.x() - layerShellAnimationStart_.x()) * eased);
    const int y = layerShellAnimationStart_.y() +
        static_cast<int>((layerShellAnimationEnd_.y() - layerShellAnimationStart_.y()) * eased);

    layerShellPosition_ = QPoint(x, y);
    applyLayerShellPlacement(layerShellPosition_, layerShellPlacementGeometryCache_);

    if (progress >= 1.0) {
        layerShellAnimationTimer_.stop();
    }
#endif
}

#if WARDNC_HAS_LAYERSHELLQT
void NotificationCenterPanel::configureLayerShell(QScreen *screen, int panelHeight)
{
    if (!usesLayerShellPlacement()) {
        return;
    }

    if (!windowHandle()) {
        winId();
    }

    if (!windowHandle()) {
        return;
    }

    if (!layerShellWindow_) {
        layerShellWindow_ = LayerShellQt::Window::get(windowHandle());
    }

    if (!layerShellWindow_) {
        return;
    }

    LayerShellQt::Window::Anchors anchors;
    anchors |= anchorAtTop()
        ? LayerShellQt::Window::AnchorTop
        : LayerShellQt::Window::AnchorBottom;
    anchors |= layerShellUsesRightAnchor()
        ? LayerShellQt::Window::AnchorRight
        : LayerShellQt::Window::AnchorLeft;

    layerShellWindow_->setAnchors(anchors);
    layerShellWindow_->setLayer(config_.layout.aboveWindows
                                    ? LayerShellQt::Window::LayerOverlay
                                    : LayerShellQt::Window::LayerTop);
    layerShellWindow_->setKeyboardInteractivity(
        config_.layout.focusable
            ? LayerShellQt::Window::KeyboardInteractivityOnDemand
            : LayerShellQt::Window::KeyboardInteractivityNone);
    layerShellWindow_->setExclusiveZone(config_.layout.exclusiveZone);
    layerShellWindow_->setActivateOnShow(config_.layout.focusable);
    layerShellWindow_->setScope(QStringLiteral("wardnc"));
    layerShellWindow_->setDesiredSize(QSize(width(), panelHeight));

    if (screen) {
        layerShellWindow_->setScreen(screen);
    }
}

void NotificationCenterPanel::applyLayerShellPlacement(const QPoint &position,
                                                       const QRect &availableGeometry)
{
    if (!layerShellWindow_ || !availableGeometry.isValid()) {
        return;
    }

    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    if (layerShellUsesRightAnchor()) {
        right = availableGeometry.right() - (position.x() + width() - 1);
    } else {
        left = position.x() - availableGeometry.left();
    }

    if (anchorAtTop()) {
        top = position.y() - availableGeometry.top();
    } else {
        bottom = availableGeometry.bottom() - (position.y() + height() - 1);
    }

    layerShellWindow_->setMargins(QMargins(left, top, right, bottom));
    update();
    if (QWindow *window = windowHandle()) {
        window->requestUpdate();
    }
}
#else
void NotificationCenterPanel::configureLayerShell(QScreen *, int)
{
}

void NotificationCenterPanel::applyLayerShellPlacement(const QPoint &, const QRect &)
{
}
#endif

QString NotificationCenterPanel::historyFilePath() const
{
    const QString overridePath = QString::fromUtf8(qgetenv("WARDNC_HISTORY_FILE")).trimmed();
    if (!overridePath.isEmpty()) {
        return expandPathValue(overridePath);
    }

    return expandPathValue(config_.paths.historyFile);
}

void NotificationCenterPanel::trimNotificationHistoryFile() const
{
    if (!config_.notifications.historyEnabled) {
        return;
    }

    const QString path = historyFilePath();
    if (path.trimmed().isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QStringList lines;
    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        if (!line.trimmed().isEmpty()) {
            lines.append(line);
        }
    }
    file.close();

    const int maxEntries = qMax(1, config_.notifications.historyMaxEntries);
    if (lines.size() <= maxEntries) {
        return;
    }

    const QStringList trimmed = lines.mid(lines.size() - maxEntries);
    const QString tmpPath = path + QStringLiteral(".tmp");

    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    QTextStream output(&tmpFile);
    for (const QString &line : trimmed) {
        output << line << '\n';
    }
    tmpFile.close();

    QFile::remove(path);
    QFile::rename(tmpPath, path);
}

void NotificationCenterPanel::loadNotificationHistory()
{
    if (loadingHistory_ || !config_.notifications.historyEnabled) {
        return;
    }

    const QString path = historyFilePath();
    if (path.trimmed().isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QList<QPair<QString, QJsonObject>> records;
    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        const QString compactLine = line.trimmed();
        if (compactLine.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(compactLine.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        records.append(qMakePair(compactLine, document.object()));
    }

    loadingHistory_ = true;
    const int maxEntries = qMax(1, config_.notifications.historyMaxEntries);
    const int startIndex = qMax(0, records.size() - maxEntries);
    for (int index = startIndex; index < records.size(); ++index) {
        const QString recordKey = records.at(index).first;
        if (historyRecordKeys_.contains(recordKey)) {
            continue;
        }

        const QJsonObject record = records.at(index).second;
        const QString appName = sanitizeText(record.value(QStringLiteral("app")).toString()).trimmed();
        const QString summary = sanitizeText(record.value(QStringLiteral("summary")).toString()).trimmed();
        const QString body = sanitizeText(record.value(QStringLiteral("body")).toString()).trimmed();
        if (summary.isEmpty() && body.isEmpty()) {
            continue;
        }

        const QString iconName = record.value(QStringLiteral("icon")).toString().trimmed();
        const QString timestampRaw = record.value(QStringLiteral("time")).toString().trimmed();
        QDateTime timestamp = QDateTime::fromString(timestampRaw, Qt::ISODateWithMs);
        if (!timestamp.isValid()) {
            timestamp = QDateTime::fromString(timestampRaw, Qt::ISODate);
        }
        if (timestamp.isValid()) {
            timestamp = timestamp.toLocalTime();
        } else {
            timestamp = QDateTime::currentDateTime();
        }

        const qint64 storedId = record.value(QStringLiteral("id")).toInteger(-1);
        uint resolvedId = 0;
        if (storedId > 0 && storedId <= std::numeric_limits<uint>::max()) {
            resolvedId = static_cast<uint>(storedId);
        }
        if (resolvedId == 0 || entriesById_.contains(resolvedId)) {
            resolvedId = nextHistoryId_++;
        }

        auto *entry = new NotificationEntry;
        entry->id = resolvedId;
        entry->appName = appName;
        entry->summary = summary;
        entry->body = body;
        entry->iconName = iconName;
        entry->timeoutMs = 0;
        entry->createdAt = timestamp;
        entry->historyOnly = true;

        refreshEntryWidgets(entry);
        entries_.append(entry);
        entriesById_.insert(entry->id, entry);
        historyRecordKeys_.insert(recordKey);
    }
    loadingHistory_ = false;

    enforceCapacity();
    rebuildList();
    updateHeader();
    updateFooter();
    trimNotificationHistoryFile();
}

void NotificationCenterPanel::appendNotificationHistory(const NotificationEntry *entry)
{
    if (!entry || entry->historyOnly || loadingHistory_ || !config_.notifications.historyEnabled) {
        return;
    }

    const QString path = historyFilePath();
    if (path.trimmed().isEmpty()) {
        return;
    }

    const QString appName = displayAppName(entry->appName);
    const QString summary = normalizedSummary(entry->summary, entry->body).trimmed();
    const QString body = sanitizeText(entry->body).trimmed();
    if (appName.isEmpty() && summary.isEmpty() && body.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    const QDateTime timestamp = entry->createdAt.isValid()
        ? entry->createdAt.toUTC()
        : QDateTime::currentDateTimeUtc();

    QJsonObject record;
    record.insert(QStringLiteral("time"), timestamp.toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("app"), appName);
    record.insert(QStringLiteral("summary"), summary);
    record.insert(QStringLiteral("body"), body);
    record.insert(QStringLiteral("icon"), entry->iconName.trimmed());
    record.insert(QStringLiteral("id"), static_cast<qint64>(entry->id));

    const QByteArray compact = QJsonDocument(record).toJson(QJsonDocument::Compact);
    writingHistory_ = true;
    file.write(compact);
    file.write("\n");
    file.close();
    writingHistory_ = false;

    historyRecordKeys_.insert(QString::fromUtf8(compact));
    refreshHistoryWatcher();

    trimNotificationHistoryFile();
}

QString NotificationCenterPanel::stateFilePath() const
{
    const QString overridePath = QString::fromUtf8(qgetenv("WARDNC_STATE_FILE")).trimmed();
    if (!overridePath.isEmpty()) {
        return expandPathValue(overridePath);
    }

    return expandPathValue(config_.paths.stateFile);
}

QString NotificationCenterPanel::normalizedStateValue(const QString &state) const
{
    const QString compact = state.trimmed().toLower().remove(QRegularExpression(QStringLiteral("\\s+")));

    if (compact == QStringLiteral("open") ||
        compact == QStringLiteral("opened") ||
        compact == QStringLiteral("true") ||
        compact == QStringLiteral("on") ||
        compact == QStringLiteral("1")) {
        return QStringLiteral("open");
    }

    if (compact == QStringLiteral("closed") ||
        compact == QStringLiteral("close") ||
        compact == QStringLiteral("false") ||
        compact == QStringLiteral("off") ||
        compact == QStringLiteral("0")) {
        return QStringLiteral("closed");
    }

    return {};
}

QString NotificationCenterPanel::readStateFile() const
{
    const QString path = stateFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString raw = QString::fromUtf8(file.readLine());
    return normalizedStateValue(raw);
}

void NotificationCenterPanel::writeStateFile(const QString &state)
{
    const QString normalized = normalizedStateValue(state);
    if (normalized.isEmpty()) {
        return;
    }

    const QString path = stateFilePath();
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return;
    }

    writingState_ = true;

    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile tmpFile(tmpPath);
    if (tmpFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        tmpFile.write(normalized.toUtf8());
        tmpFile.write("\n");
        tmpFile.close();
        QFile::remove(path);
        QFile::rename(tmpPath, path);
    }

    writingState_ = false;
    refreshStateWatcher();
}

void NotificationCenterPanel::refreshStateWatcher()
{
    if (!stateWatcher_.files().isEmpty()) {
        stateWatcher_.removePaths(stateWatcher_.files());
    }
    if (!stateWatcher_.directories().isEmpty()) {
        stateWatcher_.removePaths(stateWatcher_.directories());
    }

    const QString path = stateFilePath();
    const QFileInfo info(path);

    if (QDir(info.absolutePath()).exists()) {
        stateWatcher_.addPath(info.absolutePath());
    }

    if (info.exists()) {
        stateWatcher_.addPath(path);
    }

    stateWatcherReady_ = true;
}

void NotificationCenterPanel::refreshHistoryWatcher()
{
    if (!historyWatcher_.files().isEmpty()) {
        historyWatcher_.removePaths(historyWatcher_.files());
    }
    if (!historyWatcher_.directories().isEmpty()) {
        historyWatcher_.removePaths(historyWatcher_.directories());
    }

    if (!config_.notifications.historyEnabled) {
        historyWatcherReady_ = false;
        return;
    }

    const QString path = historyFilePath();
    const QFileInfo info(path);

    if (QDir(info.absolutePath()).exists()) {
        historyWatcher_.addPath(info.absolutePath());
    }

    if (info.exists()) {
        historyWatcher_.addPath(path);
    }

    historyWatcherReady_ = true;
}

void NotificationCenterPanel::handleStateFileChanged(const QString &)
{
    if (!stateWatcherReady_ || writingState_) {
        return;
    }

    refreshStateWatcher();

    const QString state = readStateFile();
    if (state.isEmpty()) {
        return;
    }

    setPanelOpen(state == QStringLiteral("open"), true, false);
}

void NotificationCenterPanel::handleStateDirectoryChanged(const QString &)
{
    if (!stateWatcherReady_ || writingState_) {
        return;
    }

    refreshStateWatcher();

    const QString state = readStateFile();
    if (state.isEmpty()) {
        return;
    }

    setPanelOpen(state == QStringLiteral("open"), true, false);
}

void NotificationCenterPanel::handleHistoryFileChanged(const QString &)
{
    if (!historyWatcherReady_ || writingHistory_) {
        return;
    }

    refreshHistoryWatcher();
    loadNotificationHistory();
}

void NotificationCenterPanel::handleHistoryDirectoryChanged(const QString &)
{
    if (!historyWatcherReady_ || writingHistory_) {
        return;
    }

    refreshHistoryWatcher();
    loadNotificationHistory();
}

void NotificationCenterPanel::showNotification(uint id,
                                               const QString &appName,
                                               const QString &summary,
                                               const QString &body,
                                               const QString &iconName,
                                               int timeoutMs,
                                               const QStringList &actions,
                                               const QVariantMap &hints)
{
    if (config_.notifications.ignoreTransient && notificationIsTransient(hints)) {
        return;
    }

    NotificationEntry *entry = findEntryById(id);
    const QString stackTag = notificationStackTag(hints);
    NotificationEntry *stackEntry = stackTag.isEmpty() ? nullptr : findEntryByStackTag(stackTag);

    if (stackEntry && stackEntry != entry) {
        removeEntry(stackEntry, 4);
    }

    if (!entry) {
        entry = createEntry(id, appName, summary, body, iconName, timeoutMs, actions, hints);
    } else {
        updateEntry(entry, appName, summary, body, iconName, timeoutMs, actions, hints);
        entries_.removeAll(entry);
        entries_.append(entry);
    }

    enforceCapacity();
    rebuildList();
    updateHeader();
    updateFooter();
    appendNotificationHistory(entry);

    if (config_.panel.openOnNewNotification) {
        setPanelOpen(true, true, true);
    }
}

void NotificationCenterPanel::closeNotification(uint id, uint reason)
{
    NotificationEntry *entry = findEntryById(id);
    if (!entry) {
        return;
    }

    removeEntry(entry, reason);
}

void NotificationCenterPanel::openPanel()
{
    setPanelOpen(true, true, true);
}

void NotificationCenterPanel::closePanel()
{
    setPanelOpen(false, true, true);
}

void NotificationCenterPanel::togglePanel()
{
    setPanelOpen(!open_, true, true);
}

void NotificationCenterPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePanelRootGeometry();

    if (!sideHandle_) {
        return;
    }

    const int handleHeight = qMax(40, qMin(96, height() / 4));
    sideHandle_->setFixedHeight(handleHeight);
    positionSideHandle();
}

void NotificationCenterPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && config_.panel.closeOnEscape) {
        closePanel();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}
