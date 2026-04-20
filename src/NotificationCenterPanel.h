#pragma once

#include <QColor>
#include <QDateTime>
#include <QEasingCurve>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "WardNcConfig.h"

class QFrame;
class QLabel;
class QHBoxLayout;
class QPushButton;
class QPropertyAnimation;
class QScreen;
class QScrollArea;
class QTimer;
class QVariantAnimation;
class QVBoxLayout;

#if WARDNC_HAS_LAYERSHELLQT
namespace LayerShellQt
{
class Window;
}
#endif

struct NotificationActionItem {
    QString key;
    QString label;
};

struct NotificationEntry {
    uint id = 0;
    QString stackTag;
    QString appName;
    QString summary;
    QString body;
    QString iconName;
    int timeoutMs = -1;
    QVariantMap hints;
    QList<NotificationActionItem> actions;
    QDateTime createdAt;

    QWidget *card = nullptr;
    QLabel *summaryLabel = nullptr;
    QLabel *bodyLabel = nullptr;
    QLabel *appLabel = nullptr;
    QLabel *timestampDayLabel = nullptr;
    QLabel *timestampLabel = nullptr;
    QLabel *iconLabel = nullptr;
    QTimer *expiryTimer = nullptr;
    bool historyOnly = false;
};

class NotificationCenterPanel : public QWidget {
    Q_OBJECT

public:
    explicit NotificationCenterPanel(WardNcConfigLoader *configLoader, QWidget *parent = nullptr);
    ~NotificationCenterPanel() override;

signals:
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &actionKey);

public slots:
    void showNotification(uint id,
                          const QString &appName,
                          const QString &summary,
                          const QString &body,
                          const QString &iconName,
                          int timeoutMs,
                          const QStringList &actions,
                          const QVariantMap &hints);
    void closeNotification(uint id, uint reason = 3);
    void reloadConfiguration();
    void openPanel();
    void closePanel();
    void togglePanel();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void applyConfig(const WardNcConfig &config);
    void applyStyle(const QString &styleSheet, const QHash<QString, QString> &styleVariables);
    void handleStateFileChanged(const QString &path);
    void handleStateDirectoryChanged(const QString &path);
    void handleHistoryFileChanged(const QString &path);
    void handleHistoryDirectoryChanged(const QString &path);

private:
    struct StyleMetrics {
        int panelPaddingX = 12;
        int panelPaddingY = 12;
        int panelSectionGap = 10;
        int headerHeight = 36;
        int headerPaddingX = 12;
        int headerGap = 8;
        int badgeHeight = 22;
        int badgeMinWidth = 30;
        int badgePaddingX = 14;
        int notificationGap = 10;
        int cardPaddingX = 14;
        int cardPaddingY = 14;
        int cardGap = 12;
        int iconSize = 40;
        int iconPadding = 6;
        int textGap = 6;
        int bodyMaxLines = 0;
        int dismissButtonSize = 20;
        int actionGap = 6;
        int actionHeight = 28;
        int actionMinWidth = 80;
        int actionHorizontalPadding = 22;
        int footerHeight = 34;
        int footerPaddingX = 12;
        int clearHeight = 22;
        int clearMinWidth = 56;
        int clearPaddingX = 4;
        int clearShadowBlur = 10;
        int clearShadowOffsetX = 0;
        int clearShadowOffsetY = 2;
        int clearShadowMarginY = 4;
    };

    void buildUi();
    void applyUiState();
    void applyStyleMetrics();
    void applyClearButtonShadows();
    void ensureCloseButtonInteractivity();
    void updateHeader();
    void updateFooter();
    void rebuildList();
    void clearNotifications(uint reason = 2);
    void enforceCapacity();

    NotificationEntry *findEntryById(uint id) const;
    NotificationEntry *findEntryByStackTag(const QString &stackTag) const;
    NotificationEntry *createEntry(uint id,
                                   const QString &appName,
                                   const QString &summary,
                                   const QString &body,
                                   const QString &iconName,
                                   int timeoutMs,
                                   const QStringList &actions,
                                   const QVariantMap &hints);
    void updateEntry(NotificationEntry *entry,
                     const QString &appName,
                     const QString &summary,
                     const QString &body,
                     const QString &iconName,
                     int timeoutMs,
                     const QStringList &actions,
                     const QVariantMap &hints);
    void removeEntry(NotificationEntry *entry, uint reason, bool emitSignal = true);

    void refreshEntryWidgets(NotificationEntry *entry);
    void rebuildEntryActions(NotificationEntry *entry, QVBoxLayout *cardLayout);
    QList<NotificationActionItem> parseActions(const QStringList &actions) const;

    QString sanitizeText(const QString &text) const;
    QString stripMarkup(const QString &text) const;
    QString decodeEntities(const QString &text) const;
    QString normalizedSummary(const QString &summary, const QString &body) const;
    QString displayAppName(const QString &appName) const;
    QString timestampDayText(const QDateTime &timestamp) const;
    QString timestampText(const QDateTime &timestamp) const;

    QPixmap notificationPixmap(const NotificationEntry *entry) const;
    int effectiveTimeoutMs(const NotificationEntry *entry) const;
    void restartExpiryTimer(NotificationEntry *entry);

    int styleLength(const QString &name, int fallback) const;
    void refreshStyleMetrics();

    QScreen *resolveScreen() const;
    QPoint openPosition(const QRect &availableGeometry, int panelHeight) const;
    QPoint closedPosition(const QRect &availableGeometry, int panelHeight) const;
    bool anchorAtRight() const;
    bool panelOnRightSide() const;
    bool scrollbarHidden() const;
    bool scrollbarOnLeft() const;
    bool anchorAtTop() const;
    bool layerShellUsesRightAnchor() const;
    bool slideTowardRight() const;
    bool usesLayerShellPlacement() const;
    QRect layerShellPlacementGeometry(QScreen *screen) const;
    int closedPanelOffset() const;
    int panelOffsetForState(bool open) const;
    void applyPanelOffset(int offset);
    void animatePanelOffsetTo(int offset);
    void updatePanelRootGeometry();
    void updateInputMask();
    void applyScrollbarPlacement();
    void positionSideHandle();
    void refreshWindowPlacement(bool animated);
    void setPanelOpen(bool open, bool animated, bool writeState);
    void applyPanelVisibility(bool animated);
    void animateToPosition(const QPoint &position);
    void animateLayerShellTo(const QPoint &position);
    void advanceLayerShellAnimation();
    void configureLayerShell(QScreen *screen, int panelHeight);
    void applyLayerShellPlacement(const QPoint &position, const QRect &availableGeometry);

    QString stateFilePath() const;
    QString historyFilePath() const;
    void loadNotificationHistory();
    void appendNotificationHistory(const NotificationEntry *entry);
    void trimNotificationHistoryFile() const;
    QString normalizedStateValue(const QString &state) const;
    QString readStateFile() const;
    void writeStateFile(const QString &state);
    void refreshStateWatcher();
    void refreshHistoryWatcher();

    WardNcConfigLoader *configLoader_ = nullptr;
    WardNcConfig config_;
    QString styleSheet_;
    QHash<QString, QString> styleVariables_;
    StyleMetrics styleMetrics_;
    QColor clearButtonShadowColor_ {0, 0, 0, 0};

    QFrame *panelRoot_ = nullptr;
    QVBoxLayout *panelLayout_ = nullptr;
    QFrame *headerBar_ = nullptr;
    QHBoxLayout *headerLayout_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *countBadgeLabel_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QScrollArea *scrollArea_ = nullptr;
    QWidget *listContainer_ = nullptr;
    QVBoxLayout *listLayout_ = nullptr;
    QFrame *footerBar_ = nullptr;
    QHBoxLayout *footerLayout_ = nullptr;
    QFrame *sideHandle_ = nullptr;

    QList<NotificationEntry *> entries_;
    QHash<uint, NotificationEntry *> entriesById_;
    QHash<QString, NotificationEntry *> entriesByStackTag_;

    QPointer<QVariantAnimation> slideAnimation_;
    QPointer<QPropertyAnimation> fadeAnimation_;
    QFileSystemWatcher stateWatcher_;
    QFileSystemWatcher historyWatcher_;
    QTimer statePollTimer_;

    bool open_ = false;
    bool stateWatcherReady_ = false;
    bool historyWatcherReady_ = false;
    bool writingState_ = false;
    bool loadingHistory_ = false;
    bool writingHistory_ = false;
    bool useCursorScreenForNextPlacement_ = false;
    int panelOffsetX_ = 0;
    uint nextHistoryId_ = 1000000000U;
    QSet<QString> historyRecordKeys_;
#if WARDNC_HAS_LAYERSHELLQT
    LayerShellQt::Window *layerShellWindow_ = nullptr;
    QPoint layerShellPosition_;
    QRect layerShellPlacementGeometryCache_;
    QTimer layerShellAnimationTimer_;
    QPoint layerShellAnimationStart_;
    QPoint layerShellAnimationEnd_;
    QEasingCurve layerShellAnimationEasing_ {QEasingCurve::OutCubic};
    qint64 layerShellAnimationStartMs_ = 0;
    int layerShellAnimationDurationMs_ = 0;
#endif
};
