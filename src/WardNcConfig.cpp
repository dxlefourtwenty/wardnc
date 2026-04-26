#include "WardNcConfig.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <unistd.h>

namespace {

struct LoadedStyleSheet {
    QString styleSheet;
    QHash<QString, QString> variables;
    QStringList dependencyFiles;
    QStringList dependencyDirectories;
};

struct ConfigLoadResult {
    WardNcConfig config;
    bool ok = false;
    QString error;
};

struct StyleLoadResult {
    LoadedStyleSheet styleSheet;
    bool ok = false;
    QString error;
};

struct StyleLoadContext {
    QSet<QString> activePaths;
    QSet<QString> dependencyFiles;
    QSet<QString> dependencyDirectories;
};

QString wardNcConfigDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/wardnc");
}

QString defaultConfigContents()
{
    return QStringLiteral(
        "# wardnc notification center config\n"
        "# any path field accepts ~, $VAR, or ${VAR}.\n"
        "\n"
        "[layout]\n"
        "anchor = \"top-right\"\n"
        "screen = \"HDMI-A-1\"\n"
        "monitor_position = \"auto\"\n"
        "width = 420\n"
        "peek_width = 0\n"
        "outer_margin = 0\n"
        "top_margin = 24\n"
        "right_margin = 24\n"
        "bottom_margin = 24\n"
        "left_margin = 0\n"
        "minimum_height = 240\n"
        "above_windows = true\n"
        "focusable = true\n"
        "exclusive_zone = 0\n"
        "\n"
        "[panel]\n"
        "start_open = false\n"
        "open_on_new_notification = false\n"
        "close_on_escape = true\n"
        "close_on_background_click = false\n"
        "show_header = true\n"
        "show_footer = true\n"
        "show_app_name = true\n"
        "show_timestamp = true\n"
        "time_format_24h = true\n"
        "show_actions = true\n"
        "clear_button = true\n"
        "reverse_order = true\n"
        "max_visible_notifications = 120\n"
        "max_tracked_notifications = 400\n"
        "show_handle = true\n"
        "scrollbar_position = \"auto\"\n"
        "header_glpyh = \"󰂚\"\n"
        "title = \"Notifications\"\n"
        "footer_text = \"wardnc\"\n"
        "\n"
        "[animation]\n"
        "enabled = true\n"
        "duration_ms = 220\n"
        "fade = true\n"
        "fade_duration_ms = 180\n"
        "slide_distance = 24\n"
        "slide_direction = \"auto\"\n"
        "easing = \"out-cubic\"\n"
        "\n"
        "[notifications]\n"
        "body_supported = true\n"
        "body_markup_supported = false\n"
        "body_hyperlinks_supported = false\n"
        "body_images_supported = false\n"
        "image_supported = true\n"
        "actions_supported = true\n"
        "action_icons_supported = false\n"
        "persistence_supported = true\n"
        "keep_on_reload = true\n"
        "ignore_transient = false\n"
        "auto_expire = false\n"
        "respect_timeout = true\n"
        "default_timeout_ms = 5000\n"
        "max_icon_size = 48\n"
        "history_enabled = true\n"
        "history_max_entries = 2000\n"
        "\n"
        "[text]\n"
        "strip_pango_markup = true\n"
        "decode_entities = true\n"
        "summary_fallback = \"Notification\"\n"
        "\n"
        "[paths]\n"
        "style_css = \"~/.config/wardnc/style.css\"\n"
        "colors_css = \"~/.config/wardnc/colors.css\"\n"
        "state_file = \"$XDG_RUNTIME_DIR/wardnc/panel.state\"\n"
        "history_file = \"~/.local/state/wardnc/notification-history.jsonl\"\n");
}

QString defaultColorsContents()
{
    return QStringLiteral(
        ":root {\n"
        "    --panel-bg: #1a1b26;\n"
        "    --panel-border: #414868;\n"
        "    --list-bg: #16161e;\n"
        "    --header-bg: #24283b;\n"
        "    --header-color: #c0caf5;\n"
        "    --footer-bg: #24283b;\n"
        "    --footer-color: #9aa5ce;\n"
        "\n"
        "    --card-bg: #24283b;\n"
        "    --card-border: #565f89;\n"
        "    --summary-color: #c0caf5;\n"
        "    --body-color: #a9b1d6;\n"
        "    --app-name-color: #7aa2f7;\n"
        "    --timestamp-color: #737aa2;\n"
        "\n"
        "    --badge-bg: #2f3549;\n"
        "    --badge-color: #7dcfff;\n"
        "\n"
        "    --clear-bg: #7aa2f7;\n"
        "    --clear-hover-bg: #7dcfff;\n"
        "    --clear-color: #1a1b26;\n"
        "    --clear-shadow-color: rgba(0, 0, 0, 1.0);\n"
        "\n"
        "    --dismiss-color: #a9b1d6;\n"
        "    --dismiss-hover-bg: #3b4261;\n"
        "\n"
        "    --action-bg: #2f3549;\n"
        "    --action-hover-bg: #3b4261;\n"
        "    --action-color: #c0caf5;\n"
        "\n"
        "    --icon-bg: #24283b;\n"
        "    --handle-bg: #7aa2f7;\n"
        "\n"
        "    --scrollbar-track-bg: #24283b;\n"
        "    --scrollbar-thumb-bg: #414868;\n"
        "    --scrollbar-thumb-hover-bg: #565f89;\n"
        "}\n");
}

QString defaultStyleContents()
{
    return QStringLiteral(
        "@import \"./colors.css\";\n"
        "\n"
        ":root {\n"
        "    --font-family: \"Liberation Sans\";\n"
        "\n"
        "    --panel-radius: 20px;\n"
        "    --panel-border-width: 1px;\n"
        "    --panel-padding-x: 12px;\n"
        "    --panel-padding-y: 12px;\n"
        "    --panel-section-gap: 10px;\n"
        "\n"
        "    --header-height: 36px;\n"
        "    --header-radius: 10px;\n"
        "    --header-padding-x: 12px;\n"
        "    --header-gap: 8px;\n"
        "    --header-size: 14px;\n"
        "\n"
        "    --badge-height: 22px;\n"
        "    --badge-radius: 8px;\n"
        "    --badge-min-width: 30px;\n"
        "    --badge-padding-x: 14px;\n"
        "    --badge-size: 11px;\n"
        "\n"
        "    --notification-gap: 10px;\n"
        "\n"
        "    --card-radius: 14px;\n"
        "    --card-border-width: 1px;\n"
        "    --card-padding-x: 14px;\n"
        "    --card-padding-y: 14px;\n"
        "    --card-gap: 12px;\n"
        "\n"
        "    --icon-size: 40px;\n"
        "    --icon-radius: 10px;\n"
        "    --icon-padding: 6px;\n"
        "\n"
        "    --text-gap: 6px;\n"
        "    --summary-size: 14px;\n"
        "    --body-size: 13px;\n"
        "    --body-max-lines: 0;\n"
        "    --app-name-size: 12px;\n"
        "    --timestamp-size: 11px;\n"
        "\n"
        "    --dismiss-button-size: 20px;\n"
        "    --dismiss-size: 11px;\n"
        "\n"
        "    --action-gap: 6px;\n"
        "    --action-height: 28px;\n"
        "    --action-radius: 8px;\n"
        "    --action-size: 12px;\n"
        "    --action-min-width: 80px;\n"
        "    --action-horizontal-padding: 22px;\n"
        "\n"
        "    --footer-height: 34px;\n"
        "    --footer-radius: 10px;\n"
        "    --footer-padding-x: 12px;\n"
        "    --footer-size: 12px;\n"
        "\n"
        "    --clear-height: 22px;\n"
        "    --clear-radius: 8px;\n"
        "    --clear-size: 12px;\n"
        "    --clear-min-width: 56px;\n"
        "    --clear-padding-x: 4px;\n"
        "    --clear-shadow-blur: 10px;\n"
        "    --clear-shadow-offset-x: 0px;\n"
        "    --clear-shadow-offset-y: 2px;\n"
        "    --clear-shadow-margin-y: 4px;\n"
        "\n"
        "    --scrollbar-width: 8px;\n"
        "    --scrollbar-track-radius: 4px;\n"
        "    --scrollbar-thumb-radius: 4px;\n"
        "    --scrollbar-margin-y: 4px;\n"
        "    --scrollbar-thumb-min-height: 28px;\n"
        "}\n"
        "\n"
        "QWidget#notificationCenterWindow {\n"
        "    background: transparent;\n"
        "}\n"
        "\n"
        "QWidget#notificationCenterWindow,\n"
        "QWidget#notificationCenterWindow * {\n"
        "    font-family: var(--font-family);\n"
        "}\n"
        "\n"
        "QFrame#panelRoot {\n"
        "    background-color: var(--panel-bg);\n"
        "    border: var(--panel-border-width) solid var(--panel-border);\n"
        "    border-radius: var(--panel-radius);\n"
        "}\n"
        "\n"
        "QScrollArea#notificationListScrollArea {\n"
        "    background: var(--list-bg);\n"
        "    border: none;\n"
        "}\n"
        "\n"
        "QWidget#notificationListViewport {\n"
        "    background: var(--list-bg);\n"
        "}\n"
        "\n"
        "QWidget#notificationListContainer {\n"
        "    background: var(--list-bg);\n"
        "}\n"
        "\n"
        "QScrollBar#notificationListScrollBar:vertical {\n"
        "    background: var(--scrollbar-track-bg);\n"
        "    border: none;\n"
        "    border-radius: var(--scrollbar-track-radius);\n"
        "    width: var(--scrollbar-width);\n"
        "    margin: var(--scrollbar-margin-y) 0px var(--scrollbar-margin-y) 0px;\n"
        "}\n"
        "\n"
        "QScrollBar#notificationListScrollBar::handle:vertical {\n"
        "    background: var(--scrollbar-thumb-bg);\n"
        "    border: none;\n"
        "    border-radius: var(--scrollbar-thumb-radius);\n"
        "    min-height: var(--scrollbar-thumb-min-height);\n"
        "}\n"
        "\n"
        "QScrollBar#notificationListScrollBar::handle:vertical:hover {\n"
        "    background: var(--scrollbar-thumb-hover-bg);\n"
        "}\n"
        "\n"
        "QScrollBar#notificationListScrollBar::add-line:vertical,\n"
        "QScrollBar#notificationListScrollBar::sub-line:vertical {\n"
        "    border: none;\n"
        "    background: transparent;\n"
        "    height: 0px;\n"
        "}\n"
        "\n"
        "QScrollBar#notificationListScrollBar::add-page:vertical,\n"
        "QScrollBar#notificationListScrollBar::sub-page:vertical {\n"
        "    background: transparent;\n"
        "}\n"
        "\n"
        "QFrame#headerBar {\n"
        "    background: var(--header-bg);\n"
        "    border-radius: var(--header-radius);\n"
        "}\n"
        "\n"
        "QLabel#headerTitle {\n"
        "    color: var(--header-color);\n"
        "    font-size: var(--header-size);\n"
        "    font-weight: 700;\n"
        "}\n"
        "\n"
        "QLabel#countBadge {\n"
        "    color: var(--badge-color);\n"
        "    background: var(--badge-bg);\n"
        "    border-radius: var(--badge-radius);\n"
        "    font-size: var(--badge-size);\n"
        "    font-weight: 700;\n"
        "}\n"
        "\n"
        "QPushButton#clearButton {\n"
        "    color: var(--clear-color);\n"
        "    background: var(--clear-bg);\n"
        "    border: none;\n"
        "    border-radius: var(--clear-radius);\n"
        "    font-size: var(--clear-size);\n"
        "    font-weight: 700;\n"
        "    padding: 0px var(--clear-padding-x);\n"
        "}\n"
        "\n"
        "QPushButton#clearButton:hover {\n"
        "    background: var(--clear-hover-bg);\n"
        "}\n"
        "\n"
        "QFrame#notificationCard {\n"
        "    background: var(--card-bg);\n"
        "    border: var(--card-border-width) solid var(--card-border);\n"
        "    border-radius: var(--card-radius);\n"
        "}\n"
        "\n"
        "QLabel#summaryLabel {\n"
        "    color: var(--summary-color);\n"
        "    font-size: var(--summary-size);\n"
        "    font-weight: 600;\n"
        "}\n"
        "\n"
        "QLabel#bodyLabel {\n"
        "    color: var(--body-color);\n"
        "    font-size: var(--body-size);\n"
        "}\n"
        "\n"
        "QLabel#appNameLabel {\n"
        "    color: var(--app-name-color);\n"
        "    font-size: var(--app-name-size);\n"
        "}\n"
        "\n"
        "QLabel#timestampLabel {\n"
        "    color: var(--timestamp-color);\n"
        "    font-size: var(--timestamp-size);\n"
        "}\n"
        "\n"
        "QLabel#timestampDayLabel {\n"
        "    color: var(--timestamp-color);\n"
        "    font-size: var(--timestamp-size);\n"
        "}\n"
        "\n"
        "QFrame#iconFrame {\n"
        "    background: var(--icon-bg);\n"
        "    border-radius: var(--icon-radius);\n"
        "}\n"
        "\n"
        "QPushButton#dismissButton {\n"
        "    color: var(--dismiss-color);\n"
        "    border: none;\n"
        "    border-radius: 10px;\n"
        "    background: transparent;\n"
        "}\n"
        "\n"
        "QPushButton#dismissButton:hover {\n"
        "    background: var(--dismiss-hover-bg);\n"
        "}\n"
        "\n"
        "QPushButton#actionButton {\n"
        "    color: var(--action-color);\n"
        "    background: var(--action-bg);\n"
        "    border: none;\n"
        "    border-radius: var(--action-radius);\n"
        "    font-size: var(--action-size);\n"
        "}\n"
        "\n"
        "QPushButton#actionButton:hover {\n"
        "    background: var(--action-hover-bg);\n"
        "}\n"
        "\n"
        "QFrame#footerBar {\n"
        "    background: var(--footer-bg);\n"
        "    border-radius: var(--footer-radius);\n"
        "}\n"
        "\n"
        "QLabel#footerLabel {\n"
        "    color: var(--footer-color);\n"
        "    font-size: var(--footer-size);\n"
        "}\n"
        "\n"
        "QFrame#sideHandle {\n"
        "    background: var(--handle-bg);\n"
        "    border-top-right-radius: 8px;\n"
        "    border-bottom-right-radius: 8px;\n"
        "}\n");
}

QString stripInlineComment(const QString &line)
{
    bool insideQuotes = false;

    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);

        if (character == '"') {
            insideQuotes = !insideQuotes;
            continue;
        }

        if (!insideQuotes && character == '#') {
            return line.left(index).trimmed();
        }
    }

    return line.trimmed();
}

QString configErrorMessage(int lineNumber, const QString &message)
{
    return QStringLiteral("config.toml:%1: %2").arg(lineNumber).arg(message);
}

QString styleErrorMessage(const QString &path, const QString &message)
{
    return QStringLiteral("%1: %2").arg(path, message);
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

QString normalizedPath(const QString &path)
{
    const QString expandedPath = expandPathValue(path);
    const QFileInfo info(expandedPath);
    return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}

bool parseStringValue(const QString &value, QString *parsed)
{
    const QString trimmed = stripInlineComment(value).trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const bool startsWithQuote = trimmed.startsWith('"');
    const bool endsWithQuote = trimmed.endsWith('"');
    if (startsWithQuote != endsWithQuote || (startsWithQuote && trimmed.size() < 2)) {
        return false;
    }

    *parsed = startsWithQuote ? trimmed.mid(1, trimmed.size() - 2) : trimmed;
    return true;
}

bool parseIntValue(const QString &value, int *parsed)
{
    bool ok = false;
    const int parsedValue = stripInlineComment(value).trimmed().toInt(&ok);
    if (!ok) {
        return false;
    }

    *parsed = parsedValue;
    return true;
}

bool parseBoolValue(const QString &value, bool *parsed)
{
    const QString lowered = stripInlineComment(value).trimmed().toLower();

    if (lowered == QStringLiteral("true")) {
        *parsed = true;
        return true;
    }

    if (lowered == QStringLiteral("false")) {
        *parsed = false;
        return true;
    }

    return false;
}

bool parseSectionString(const QString &value, QString *target, const QString &errorLabel, QString *error)
{
    QString parsed;
    if (!parseStringValue(value, &parsed)) {
        *error = QStringLiteral("invalid string for %1").arg(errorLabel);
        return false;
    }

    *target = parsed;
    return true;
}

bool parseSectionInt(const QString &value, int *target, const QString &errorLabel, QString *error)
{
    if (!parseIntValue(value, target)) {
        *error = QStringLiteral("invalid integer for %1").arg(errorLabel);
        return false;
    }

    return true;
}

bool parseSectionBool(const QString &value, bool *target, const QString &errorLabel, QString *error)
{
    if (!parseBoolValue(value, target)) {
        *error = QStringLiteral("invalid boolean for %1").arg(errorLabel);
        return false;
    }

    return true;
}

bool applyLayoutEntry(const QString &key, const QString &value, WardNcConfig *config, QString *error)
{
    if (key == QStringLiteral("anchor")) {
        return parseSectionString(value, &config->layout.anchor, QStringLiteral("layout.anchor"), error);
    }
    if (key == QStringLiteral("screen")) {
        return parseSectionString(value, &config->layout.screen, QStringLiteral("layout.screen"), error);
    }
    if (key == QStringLiteral("monitor_position") || key == QStringLiteral("monitor_side") ||
        key == QStringLiteral("position_on_monitor")) {
        return parseSectionString(value,
                                  &config->layout.monitorPosition,
                                  QStringLiteral("layout.monitor_position"),
                                  error);
    }
    if (key == QStringLiteral("width")) {
        return parseSectionInt(value, &config->layout.width, QStringLiteral("layout.width"), error);
    }
    if (key == QStringLiteral("peek_width")) {
        return parseSectionInt(value, &config->layout.peekWidth, QStringLiteral("layout.peek_width"), error);
    }
    if (key == QStringLiteral("gap") || key == QStringLiteral("outer_margin")) {
        return parseSectionInt(value, &config->layout.outerMargin, QStringLiteral("layout.outer_margin"), error);
    }
    if (key == QStringLiteral("top_margin")) {
        return parseSectionInt(value, &config->layout.marginTop, QStringLiteral("layout.top_margin"), error);
    }
    if (key == QStringLiteral("right_margin")) {
        return parseSectionInt(value, &config->layout.marginRight, QStringLiteral("layout.right_margin"), error);
    }
    if (key == QStringLiteral("bottom_margin")) {
        return parseSectionInt(value, &config->layout.marginBottom, QStringLiteral("layout.bottom_margin"), error);
    }
    if (key == QStringLiteral("left_margin")) {
        return parseSectionInt(value, &config->layout.marginLeft, QStringLiteral("layout.left_margin"), error);
    }
    if (key == QStringLiteral("minimum_height")) {
        return parseSectionInt(value, &config->layout.minimumHeight, QStringLiteral("layout.minimum_height"), error);
    }
    if (key == QStringLiteral("above_windows")) {
        return parseSectionBool(value, &config->layout.aboveWindows, QStringLiteral("layout.above_windows"), error);
    }
    if (key == QStringLiteral("focusable")) {
        return parseSectionBool(value, &config->layout.focusable, QStringLiteral("layout.focusable"), error);
    }
    if (key == QStringLiteral("exclusive_zone")) {
        return parseSectionInt(value, &config->layout.exclusiveZone, QStringLiteral("layout.exclusive_zone"), error);
    }

    return true;
}

bool applyPanelEntry(const QString &key, const QString &value, WardNcConfig *config, QString *error)
{
    if (key == QStringLiteral("start_open")) {
        return parseSectionBool(value, &config->panel.startOpen, QStringLiteral("panel.start_open"), error);
    }
    if (key == QStringLiteral("open_on_new_notification")) {
        return parseSectionBool(value,
                                &config->panel.openOnNewNotification,
                                QStringLiteral("panel.open_on_new_notification"),
                                error);
    }
    if (key == QStringLiteral("close_on_escape")) {
        return parseSectionBool(value, &config->panel.closeOnEscape, QStringLiteral("panel.close_on_escape"), error);
    }
    if (key == QStringLiteral("close_on_background_click")) {
        return parseSectionBool(value,
                                &config->panel.closeOnBackgroundClick,
                                QStringLiteral("panel.close_on_background_click"),
                                error);
    }
    if (key == QStringLiteral("show_header")) {
        return parseSectionBool(value, &config->panel.showHeader, QStringLiteral("panel.show_header"), error);
    }
    if (key == QStringLiteral("show_footer")) {
        return parseSectionBool(value, &config->panel.showFooter, QStringLiteral("panel.show_footer"), error);
    }
    if (key == QStringLiteral("show_app_name")) {
        return parseSectionBool(value, &config->panel.showAppName, QStringLiteral("panel.show_app_name"), error);
    }
    if (key == QStringLiteral("show_timestamp")) {
        return parseSectionBool(value,
                                &config->panel.showTimestamp,
                                QStringLiteral("panel.show_timestamp"),
                                error);
    }
    if (key == QStringLiteral("time_format_24h")) {
        return parseSectionBool(value,
                                &config->panel.timeFormat24h,
                                QStringLiteral("panel.time_format_24h"),
                                error);
    }
    if (key == QStringLiteral("show_actions")) {
        return parseSectionBool(value, &config->panel.showActions, QStringLiteral("panel.show_actions"), error);
    }
    if (key == QStringLiteral("clear_button")) {
        return parseSectionBool(value, &config->panel.clearButton, QStringLiteral("panel.clear_button"), error);
    }
    if (key == QStringLiteral("reverse_order")) {
        return parseSectionBool(value, &config->panel.reverseOrder, QStringLiteral("panel.reverse_order"), error);
    }
    if (key == QStringLiteral("max_visible_notifications")) {
        return parseSectionInt(value,
                               &config->panel.maxVisibleNotifications,
                               QStringLiteral("panel.max_visible_notifications"),
                               error);
    }
    if (key == QStringLiteral("max_tracked_notifications")) {
        return parseSectionInt(value,
                               &config->panel.maxTrackedNotifications,
                               QStringLiteral("panel.max_tracked_notifications"),
                               error);
    }
    if (key == QStringLiteral("show_handle")) {
        return parseSectionBool(value, &config->panel.showHandle, QStringLiteral("panel.show_handle"), error);
    }
    if (key == QStringLiteral("scrollbar_position") || key == QStringLiteral("scrollbar_side")) {
        return parseSectionString(value,
                                  &config->panel.scrollbarPosition,
                                  QStringLiteral("panel.scrollbar_position"),
                                  error);
    }
    if (key == QStringLiteral("header_glpyh")) {
        return parseSectionString(value, &config->panel.headerGlpyh, QStringLiteral("panel.header_glpyh"), error);
    }
    if (key == QStringLiteral("title")) {
        return parseSectionString(value, &config->panel.title, QStringLiteral("panel.title"), error);
    }
    if (key == QStringLiteral("footer_text")) {
        return parseSectionString(value, &config->panel.footerText, QStringLiteral("panel.footer_text"), error);
    }

    return true;
}

bool applyAnimationEntry(const QString &key, const QString &value, WardNcConfig *config, QString *error)
{
    if (key == QStringLiteral("enabled")) {
        return parseSectionBool(value, &config->animation.enabled, QStringLiteral("animation.enabled"), error);
    }
    if (key == QStringLiteral("duration_ms")) {
        return parseSectionInt(value, &config->animation.durationMs, QStringLiteral("animation.duration_ms"), error);
    }
    if (key == QStringLiteral("fade")) {
        return parseSectionBool(value, &config->animation.fade, QStringLiteral("animation.fade"), error);
    }
    if (key == QStringLiteral("fade_duration_ms")) {
        return parseSectionInt(value,
                               &config->animation.fadeDurationMs,
                               QStringLiteral("animation.fade_duration_ms"),
                               error);
    }
    if (key == QStringLiteral("slide_distance")) {
        return parseSectionInt(value,
                               &config->animation.slideDistance,
                               QStringLiteral("animation.slide_distance"),
                               error);
    }
    if (key == QStringLiteral("slide_direction")) {
        return parseSectionString(value,
                                  &config->animation.slideDirection,
                                  QStringLiteral("animation.slide_direction"),
                                  error);
    }
    if (key == QStringLiteral("easing")) {
        return parseSectionString(value, &config->animation.easing, QStringLiteral("animation.easing"), error);
    }

    return true;
}

bool applyNotificationsEntry(const QString &key,
                             const QString &value,
                             WardNcConfig *config,
                             QString *error)
{
    if (key == QStringLiteral("body_supported")) {
        return parseSectionBool(value,
                                &config->notifications.bodySupported,
                                QStringLiteral("notifications.body_supported"),
                                error);
    }
    if (key == QStringLiteral("body_markup_supported")) {
        return parseSectionBool(value,
                                &config->notifications.bodyMarkupSupported,
                                QStringLiteral("notifications.body_markup_supported"),
                                error);
    }
    if (key == QStringLiteral("body_hyperlinks_supported")) {
        return parseSectionBool(value,
                                &config->notifications.bodyHyperlinksSupported,
                                QStringLiteral("notifications.body_hyperlinks_supported"),
                                error);
    }
    if (key == QStringLiteral("body_images_supported")) {
        return parseSectionBool(value,
                                &config->notifications.bodyImagesSupported,
                                QStringLiteral("notifications.body_images_supported"),
                                error);
    }
    if (key == QStringLiteral("image_supported")) {
        return parseSectionBool(value,
                                &config->notifications.imageSupported,
                                QStringLiteral("notifications.image_supported"),
                                error);
    }
    if (key == QStringLiteral("actions_supported")) {
        return parseSectionBool(value,
                                &config->notifications.actionsSupported,
                                QStringLiteral("notifications.actions_supported"),
                                error);
    }
    if (key == QStringLiteral("action_icons_supported")) {
        return parseSectionBool(value,
                                &config->notifications.actionIconsSupported,
                                QStringLiteral("notifications.action_icons_supported"),
                                error);
    }
    if (key == QStringLiteral("persistence_supported")) {
        return parseSectionBool(value,
                                &config->notifications.persistenceSupported,
                                QStringLiteral("notifications.persistence_supported"),
                                error);
    }
    if (key == QStringLiteral("keep_on_reload")) {
        return parseSectionBool(value,
                                &config->notifications.keepOnReload,
                                QStringLiteral("notifications.keep_on_reload"),
                                error);
    }
    if (key == QStringLiteral("ignore_transient")) {
        return parseSectionBool(value,
                                &config->notifications.ignoreTransient,
                                QStringLiteral("notifications.ignore_transient"),
                                error);
    }
    if (key == QStringLiteral("auto_expire")) {
        return parseSectionBool(value,
                                &config->notifications.autoExpire,
                                QStringLiteral("notifications.auto_expire"),
                                error);
    }
    if (key == QStringLiteral("respect_timeout")) {
        return parseSectionBool(value,
                                &config->notifications.respectTimeout,
                                QStringLiteral("notifications.respect_timeout"),
                                error);
    }
    if (key == QStringLiteral("default_timeout_ms")) {
        return parseSectionInt(value,
                               &config->notifications.defaultTimeoutMs,
                               QStringLiteral("notifications.default_timeout_ms"),
                               error);
    }
    if (key == QStringLiteral("max_icon_size")) {
        return parseSectionInt(value,
                               &config->notifications.maxIconSize,
                               QStringLiteral("notifications.max_icon_size"),
                               error);
    }
    if (key == QStringLiteral("history_enabled")) {
        return parseSectionBool(value,
                                &config->notifications.historyEnabled,
                                QStringLiteral("notifications.history_enabled"),
                                error);
    }
    if (key == QStringLiteral("history_max_entries")) {
        return parseSectionInt(value,
                               &config->notifications.historyMaxEntries,
                               QStringLiteral("notifications.history_max_entries"),
                               error);
    }

    return true;
}

bool applyTextEntry(const QString &key, const QString &value, WardNcConfig *config, QString *error)
{
    if (key == QStringLiteral("strip_pango_markup")) {
        return parseSectionBool(value,
                                &config->text.stripPangoMarkup,
                                QStringLiteral("text.strip_pango_markup"),
                                error);
    }
    if (key == QStringLiteral("decode_entities")) {
        return parseSectionBool(value,
                                &config->text.decodeEntities,
                                QStringLiteral("text.decode_entities"),
                                error);
    }
    if (key == QStringLiteral("summary_fallback")) {
        return parseSectionString(value, &config->text.summaryFallback, QStringLiteral("text.summary_fallback"), error);
    }

    return true;
}

bool applyPathsEntry(const QString &key, const QString &value, WardNcConfig *config, QString *error)
{
    if (key == QStringLiteral("style_css")) {
        return parseSectionString(value, &config->paths.styleCss, QStringLiteral("paths.style_css"), error);
    }
    if (key == QStringLiteral("colors_css")) {
        return parseSectionString(value, &config->paths.colorsCss, QStringLiteral("paths.colors_css"), error);
    }
    if (key == QStringLiteral("state_file")) {
        return parseSectionString(value, &config->paths.stateFile, QStringLiteral("paths.state_file"), error);
    }
    if (key == QStringLiteral("history_file")) {
        return parseSectionString(value, &config->paths.historyFile, QStringLiteral("paths.history_file"), error);
    }

    return true;
}

ConfigLoadResult loadWardNcConfig(const QString &path)
{
    ConfigLoadResult result;
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = styleErrorMessage(path, QStringLiteral("failed to open config file"));
        return result;
    }

    QTextStream stream(&file);
    QString section;
    int lineNumber = 0;

    while (!stream.atEnd()) {
        ++lineNumber;
        const QString rawLine = stream.readLine().trimmed();

        if (rawLine.isEmpty() || rawLine.startsWith('#')) {
            continue;
        }

        if (rawLine.startsWith('[')) {
            if (!rawLine.endsWith(']')) {
                result.error = configErrorMessage(lineNumber,
                                                  QStringLiteral("unterminated section header"));
                return result;
            }
            section = rawLine.mid(1, rawLine.size() - 2).trimmed().toLower();
            continue;
        }

        const int separatorIndex = rawLine.indexOf('=');
        if (separatorIndex <= 0) {
            result.error = configErrorMessage(lineNumber, QStringLiteral("expected key = value"));
            return result;
        }

        const QString key = rawLine.left(separatorIndex).trimmed().toLower();
        const QString value = rawLine.mid(separatorIndex + 1).trimmed();
        QString error;

        bool parsed = true;
        if (section.isEmpty()) {
            result.error = configErrorMessage(lineNumber, QStringLiteral("key is outside a section"));
            return result;
        } else if (section == QStringLiteral("layout")) {
            parsed = applyLayoutEntry(key, value, &result.config, &error);
        } else if (section == QStringLiteral("panel")) {
            parsed = applyPanelEntry(key, value, &result.config, &error);
        } else if (section == QStringLiteral("animation")) {
            parsed = applyAnimationEntry(key, value, &result.config, &error);
        } else if (section == QStringLiteral("notifications")) {
            parsed = applyNotificationsEntry(key, value, &result.config, &error);
        } else if (section == QStringLiteral("text")) {
            parsed = applyTextEntry(key, value, &result.config, &error);
        } else if (section == QStringLiteral("paths")) {
            parsed = applyPathsEntry(key, value, &result.config, &error);
        }

        if (!parsed) {
            result.error = configErrorMessage(lineNumber, error);
            return result;
        }
    }

    result.config.layout.width = qMax(result.config.layout.width, 180);
    result.config.layout.peekWidth = qBound(0, result.config.layout.peekWidth, result.config.layout.width);
    result.config.layout.outerMargin = qMax(result.config.layout.outerMargin, 0);
    result.config.layout.marginTop = qMax(result.config.layout.marginTop, 0);
    result.config.layout.marginRight = qMax(result.config.layout.marginRight, 0);
    result.config.layout.marginBottom = qMax(result.config.layout.marginBottom, 0);
    result.config.layout.marginLeft = qMax(result.config.layout.marginLeft, 0);
    result.config.layout.minimumHeight = qMax(result.config.layout.minimumHeight, 1);

    result.config.panel.maxVisibleNotifications = qMax(1, result.config.panel.maxVisibleNotifications);
    result.config.panel.maxTrackedNotifications = qMax(1, result.config.panel.maxTrackedNotifications);
    const QString scrollbarPosition = result.config.panel.scrollbarPosition.trimmed().toLower();
    if (scrollbarPosition != QStringLiteral("auto") && scrollbarPosition != QStringLiteral("left") &&
        scrollbarPosition != QStringLiteral("none") &&
        scrollbarPosition != QStringLiteral("right")) {
        result.config.panel.scrollbarPosition = QStringLiteral("auto");
    }

    result.config.animation.durationMs = qMax(result.config.animation.durationMs, 0);
    result.config.animation.fadeDurationMs = qMax(result.config.animation.fadeDurationMs, 0);
    result.config.animation.slideDistance = qMax(result.config.animation.slideDistance, 0);
    const QString monitorPosition = result.config.layout.monitorPosition.trimmed().toLower();
    if (monitorPosition != QStringLiteral("auto") && monitorPosition != QStringLiteral("left") &&
        monitorPosition != QStringLiteral("right")) {
        result.config.layout.monitorPosition = QStringLiteral("auto");
    }
    const QString slideDirection = result.config.animation.slideDirection.trimmed().toLower();
    if (slideDirection != QStringLiteral("auto") && slideDirection != QStringLiteral("left") &&
        slideDirection != QStringLiteral("right")) {
        result.config.animation.slideDirection = QStringLiteral("auto");
    }

    result.config.notifications.defaultTimeoutMs = qMax(result.config.notifications.defaultTimeoutMs, 0);
    result.config.notifications.maxIconSize = qBound(0, result.config.notifications.maxIconSize, 128);
    result.config.notifications.historyMaxEntries = qMax(result.config.notifications.historyMaxEntries, 1);

    if (result.config.text.summaryFallback.trimmed().isEmpty()) {
        result.config.text.summaryFallback = QStringLiteral("Notification");
    }

    result.ok = true;
    return result;
}

QString resolveCustomPropertyValue(const QString &name,
                                   const QHash<QString, QString> &variables,
                                   QSet<QString> *resolvingNames);

QString substituteCustomProperties(const QString &input,
                                   const QHash<QString, QString> &variables,
                                   QSet<QString> *resolvingNames)
{
    static const QRegularExpression variablePattern(
        QStringLiteral(R"(var\(\s*(--[A-Za-z0-9_-]+)\s*(?:,\s*([^)]+))?\))"));

    QString output;
    output.reserve(input.size());

    int currentIndex = 0;
    QRegularExpressionMatchIterator matches = variablePattern.globalMatch(input);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        output += input.mid(currentIndex, match.capturedStart() - currentIndex);

        const QString variableName = match.captured(1).trimmed();
        QString replacement = resolveCustomPropertyValue(variableName, variables, resolvingNames);
        if (replacement.isEmpty() && match.lastCapturedIndex() >= 2) {
            replacement = substituteCustomProperties(match.captured(2).trimmed(),
                                                     variables,
                                                     resolvingNames);
        }

        output += replacement.isEmpty() ? match.captured(0) : replacement;
        currentIndex = match.capturedEnd();
    }

    output += input.mid(currentIndex);
    return output;
}

QString resolveCustomPropertyValue(const QString &name,
                                   const QHash<QString, QString> &variables,
                                   QSet<QString> *resolvingNames)
{
    if (!variables.contains(name) || resolvingNames->contains(name)) {
        return {};
    }

    resolvingNames->insert(name);
    const QString value = substituteCustomProperties(variables.value(name), variables, resolvingNames);
    resolvingNames->remove(name);
    return value;
}

LoadedStyleSheet stripAndResolveCustomProperties(const QString &styleSheet)
{
    LoadedStyleSheet result;
    static const QRegularExpression blockPattern(QStringLiteral(R"(([^{}]+)\{([^{}]*)\})"));

    QHash<QString, QString> variables;
    QString withoutVariables;
    withoutVariables.reserve(styleSheet.size());

    int currentIndex = 0;
    QRegularExpressionMatchIterator matches = blockPattern.globalMatch(styleSheet);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        withoutVariables += styleSheet.mid(currentIndex, match.capturedStart() - currentIndex);

        const QString selector = match.captured(1).trimmed();
        const QStringList declarations = match.captured(2).split(';', Qt::KeepEmptyParts);
        QStringList keptDeclarations;

        for (const QString &declaration : declarations) {
            const QString trimmedDeclaration = declaration.trimmed();
            if (trimmedDeclaration.isEmpty()) {
                continue;
            }

            const int separatorIndex = trimmedDeclaration.indexOf(':');
            if (separatorIndex <= 0) {
                keptDeclarations.append(trimmedDeclaration);
                continue;
            }

            const QString propertyName = trimmedDeclaration.left(separatorIndex).trimmed();
            const QString propertyValue = trimmedDeclaration.mid(separatorIndex + 1).trimmed();
            if (propertyName.startsWith(QStringLiteral("--"))) {
                variables.insert(propertyName, propertyValue);
                continue;
            }

            keptDeclarations.append(QStringLiteral("%1: %2;").arg(propertyName, propertyValue));
        }

        if (!keptDeclarations.isEmpty()) {
            withoutVariables += QStringLiteral("%1 {\n    %2\n}\n")
                .arg(selector, keptDeclarations.join(QStringLiteral("\n    ")));
        }

        currentIndex = match.capturedEnd();
    }

    withoutVariables += styleSheet.mid(currentIndex);

    QSet<QString> resolvingNames;
    for (auto it = variables.cbegin(); it != variables.cend(); ++it) {
        result.variables.insert(it.key(), resolveCustomPropertyValue(it.key(), variables, &resolvingNames));
    }

    result.styleSheet = substituteCustomProperties(withoutVariables, result.variables, &resolvingNames);
    return result;
}

QString normalizeWardNcSelectors(const QString &styleSheet)
{
    QString normalized = styleSheet;
    static const QRegularExpression centerSelectorPattern(
        QStringLiteral(R"((^|[\n\r}]\s*)NotificationCenter(\s*\{))"));
    static const QRegularExpression cardSelectorPattern(
        QStringLiteral(R"((^|[\n\r}]\s*)NotificationCard(\s*\{))"));
    normalized.replace(centerSelectorPattern,
                       QStringLiteral("\\1QWidget#notificationCenterWindow\\2"));
    normalized.replace(cardSelectorPattern,
                       QStringLiteral("\\1QFrame#notificationCard\\2"));
    return normalized;
}

bool validateStyleSheetSyntax(const QString &styleSheet, QString *error)
{
    int braceDepth = 0;
    bool insideSingleQuote = false;
    bool insideDoubleQuote = false;
    bool insideBlockComment = false;

    for (int index = 0; index < styleSheet.size(); ++index) {
        const QChar character = styleSheet.at(index);
        const QChar nextCharacter = index + 1 < styleSheet.size() ? styleSheet.at(index + 1) : QChar();
        const QChar previousCharacter = index > 0 ? styleSheet.at(index - 1) : QChar();

        if (insideBlockComment) {
            if (character == '*' && nextCharacter == '/') {
                insideBlockComment = false;
                ++index;
            }
            continue;
        }

        if (!insideSingleQuote && !insideDoubleQuote && character == '/' && nextCharacter == '*') {
            insideBlockComment = true;
            ++index;
            continue;
        }

        if (!insideDoubleQuote && character == '\'' && previousCharacter != '\\') {
            insideSingleQuote = !insideSingleQuote;
            continue;
        }

        if (!insideSingleQuote && character == '"' && previousCharacter != '\\') {
            insideDoubleQuote = !insideDoubleQuote;
            continue;
        }

        if (insideSingleQuote || insideDoubleQuote) {
            continue;
        }

        if (character == '{') {
            ++braceDepth;
            continue;
        }

        if (character == '}') {
            if (braceDepth == 0) {
                *error = QStringLiteral("unexpected closing brace");
                return false;
            }
            --braceDepth;
        }
    }

    if (insideSingleQuote || insideDoubleQuote) {
        *error = QStringLiteral("unterminated string");
        return false;
    }

    if (insideBlockComment) {
        *error = QStringLiteral("unterminated block comment");
        return false;
    }

    if (braceDepth != 0) {
        *error = QStringLiteral("unterminated rule block");
        return false;
    }

    return true;
}

LoadedStyleSheet finalizeStyleSheet(const QString &styleSheet)
{
    LoadedStyleSheet result = stripAndResolveCustomProperties(styleSheet);
    result.styleSheet = normalizeWardNcSelectors(result.styleSheet);
    return result;
}

StyleLoadResult loadStyleFile(const QString &path, bool isRootFile, StyleLoadContext *context);

StyleLoadResult inlineImports(const QString &styleSheet,
                              const QString &baseDirectory,
                              StyleLoadContext *context)
{
    static const QRegularExpression importPattern(
        QStringLiteral(R"(@import[ \t\f]+(?:url\([ \t\f]*(['"]?)([^'")\s]+)\1[ \t\f]*\)|(['"])([^'"]+)\3)[ \t\f]*[^;\n\r]*(?:;|(?=\r?\n)|$))"));

    StyleLoadResult result;
    QString output;
    output.reserve(styleSheet.size());

    int currentIndex = 0;
    QRegularExpressionMatchIterator matches = importPattern.globalMatch(styleSheet);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        output += styleSheet.mid(currentIndex, match.capturedStart() - currentIndex);

        const QString importPath = match.captured(2).isEmpty()
            ? match.captured(4).trimmed()
            : match.captured(2).trimmed();

        if (importPath.startsWith(QStringLiteral("http://")) ||
            importPath.startsWith(QStringLiteral("https://"))) {
            output += match.captured(0);
        } else {
            const QString resolvedPath = normalizedPath(QDir(baseDirectory).absoluteFilePath(importPath));
            context->dependencyDirectories.insert(QFileInfo(resolvedPath).absolutePath());
            const StyleLoadResult imported = loadStyleFile(resolvedPath, false, context);
            if (!imported.ok) {
                return imported;
            }
            output += imported.styleSheet.styleSheet;
            output += QLatin1Char('\n');
        }

        currentIndex = match.capturedEnd();
    }

    output += styleSheet.mid(currentIndex);
    result.styleSheet.styleSheet = output;
    result.ok = true;
    return result;
}

StyleLoadResult loadStyleFile(const QString &path, bool isRootFile, StyleLoadContext *context)
{
    StyleLoadResult result;
    const QString resolvedPath = normalizedPath(path);
    if (context->activePaths.contains(resolvedPath)) {
        result.ok = true;
        return result;
    }

    context->activePaths.insert(resolvedPath);
    context->dependencyDirectories.insert(QFileInfo(resolvedPath).absolutePath());

    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        context->activePaths.remove(resolvedPath);
        result.error = styleErrorMessage(resolvedPath,
                                         isRootFile
                                             ? QStringLiteral("failed to open style.css")
                                             : QStringLiteral("failed to open imported stylesheet"));
        return result;
    }

    context->dependencyFiles.insert(resolvedPath);
    const QString styleSheet = QString::fromUtf8(file.readAll());
    QString validationError;
    if (!validateStyleSheetSyntax(styleSheet, &validationError)) {
        context->activePaths.remove(resolvedPath);
        result.error = styleErrorMessage(resolvedPath, validationError);
        return result;
    }

    const StyleLoadResult inlinedStyleSheet = inlineImports(styleSheet,
                                                            QFileInfo(resolvedPath).absolutePath(),
                                                            context);
    if (!inlinedStyleSheet.ok) {
        context->activePaths.remove(resolvedPath);
        return inlinedStyleSheet;
    }

    context->activePaths.remove(resolvedPath);
    result.styleSheet.styleSheet = inlinedStyleSheet.styleSheet.styleSheet;
    result.ok = true;
    return result;
}

StyleLoadResult loadStyleSheet(const QString &path)
{
    StyleLoadResult result;
    StyleLoadContext context;

    const StyleLoadResult styleFile = loadStyleFile(path, true, &context);
    if (!styleFile.ok) {
        return styleFile;
    }

    result.styleSheet = finalizeStyleSheet(styleFile.styleSheet.styleSheet);
    result.styleSheet.dependencyFiles = context.dependencyFiles.values();
    result.styleSheet.dependencyDirectories = context.dependencyDirectories.values();
    result.ok = true;
    return result;
}

QString resolvedStylePath(const WardNcConfig &config)
{
    return normalizedPath(config.paths.styleCss);
}

QString resolvedColorsPath(const WardNcConfig &config)
{
    return normalizedPath(config.paths.colorsCss);
}

} // namespace

WardNcConfigLoader::WardNcConfigLoader(QObject *parent)
    : QObject(parent)
{
    ensureConfigFiles();
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (normalizedPath(path) == normalizedPath(configPath())) {
            reload();
            return;
        }

        reloadStyle();
    });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &WardNcConfigLoader::reloadStyle);
    reload();
}

const WardNcConfig &WardNcConfigLoader::config() const
{
    return config_;
}

const QString &WardNcConfigLoader::styleSheet() const
{
    return styleSheet_;
}

const QHash<QString, QString> &WardNcConfigLoader::styleVariables() const
{
    return styleVariables_;
}

QString WardNcConfigLoader::configPath() const
{
    return wardNcConfigDirectory() + QStringLiteral("/config.toml");
}

void WardNcConfigLoader::reload()
{
    const ConfigLoadResult loadedConfig = loadWardNcConfig(configPath());
    if (loadedConfig.ok) {
        config_ = loadedConfig.config;
    } else {
        qWarning().noquote() << "wardnc:" << loadedConfig.error;
    }

    loadStyle();
    refreshWatchers();

    emit configChanged(config_);
    emit styleChanged(styleSheet_, styleVariables_);
}

void WardNcConfigLoader::reloadStyle()
{
    loadStyle();
    refreshWatchers();

    emit styleChanged(styleSheet_, styleVariables_);
}

void WardNcConfigLoader::loadStyle()
{
    const QString stylePath = resolvedStylePath(config_);
    const StyleLoadResult loadedStyleSheet = loadStyleSheet(stylePath);
    if (loadedStyleSheet.ok) {
        styleSheet_ = loadedStyleSheet.styleSheet.styleSheet;
        styleVariables_ = loadedStyleSheet.styleSheet.variables;
        styleDependencyFiles_ = loadedStyleSheet.styleSheet.dependencyFiles;
        styleDependencyDirectories_ = loadedStyleSheet.styleSheet.dependencyDirectories;

        const bool missingWidgetRules =
            !styleSheet_.contains(QStringLiteral("QWidget#notificationCenterWindow")) &&
            !styleSheet_.contains(QStringLiteral("QFrame#panelRoot"));
        if (styleSheet_.trimmed().isEmpty() || missingWidgetRules) {
            const LoadedStyleSheet fallbackStyle = finalizeStyleSheet(defaultStyleContents());
            const LoadedStyleSheet fallbackColors = finalizeStyleSheet(defaultColorsContents());
            QHash<QString, QString> mergedVariables = fallbackStyle.variables;
            for (auto it = fallbackColors.variables.cbegin(); it != fallbackColors.variables.cend(); ++it) {
                mergedVariables.insert(it.key(), it.value());
            }
            for (auto it = styleVariables_.cbegin(); it != styleVariables_.cend(); ++it) {
                mergedVariables.insert(it.key(), it.value());
            }

            QSet<QString> resolvingNames;
            styleSheet_ = substituteCustomProperties(fallbackStyle.styleSheet,
                                                     mergedVariables,
                                                     &resolvingNames);
            styleVariables_ = mergedVariables;

            qWarning().noquote()
                << "wardnc: style.css has no widget rules; using built-in default widget styling.";
        }
    } else {
        if (styleSheet_.isEmpty()) {
            const LoadedStyleSheet fallbackStyle = finalizeStyleSheet(defaultStyleContents());
            const LoadedStyleSheet fallbackColors = finalizeStyleSheet(defaultColorsContents());
            styleVariables_ = fallbackStyle.variables;
            for (auto it = fallbackColors.variables.cbegin(); it != fallbackColors.variables.cend(); ++it) {
                styleVariables_.insert(it.key(), it.value());
            }

            QSet<QString> resolvingNames;
            styleSheet_ = substituteCustomProperties(fallbackStyle.styleSheet,
                                                     styleVariables_,
                                                     &resolvingNames);
        }
        qWarning().noquote() << "wardnc:" << loadedStyleSheet.error;
    }
}

void WardNcConfigLoader::ensureConfigFiles()
{
    QDir().mkpath(wardNcConfigDirectory());

    const QString configFile = configPath();
    const QString styleFile = wardNcConfigDirectory() + QStringLiteral("/style.css");
    const QString colorsFile = wardNcConfigDirectory() + QStringLiteral("/colors.css");

    if (!QFile::exists(configFile)) {
        QFile file(configFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(defaultConfigContents().toUtf8());
        }
    }

    if (!QFile::exists(styleFile)) {
        QFile file(styleFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(defaultStyleContents().toUtf8());
        }
    }

    if (!QFile::exists(colorsFile)) {
        QFile file(colorsFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(defaultColorsContents().toUtf8());
        }
    }
}

void WardNcConfigLoader::refreshWatchers()
{
    if (!watcher_.files().isEmpty()) {
        watcher_.removePaths(watcher_.files());
    }

    if (!watcher_.directories().isEmpty()) {
        watcher_.removePaths(watcher_.directories());
    }

    QSet<QString> filePaths = {
        normalizedPath(configPath()),
        resolvedStylePath(config_),
        resolvedColorsPath(config_)
    };

    for (const QString &path : styleDependencyFiles_) {
        if (QFileInfo::exists(path)) {
            filePaths.insert(normalizedPath(path));
        }
    }

    QSet<QString> directoryPaths = {
        wardNcConfigDirectory(),
        QFileInfo(resolvedStylePath(config_)).absolutePath(),
        QFileInfo(resolvedColorsPath(config_)).absolutePath()
    };

    for (const QString &path : styleDependencyDirectories_) {
        if (QDir(path).exists()) {
            directoryPaths.insert(path);
        }
    }

    watcher_.addPaths(QStringList(filePaths.begin(), filePaths.end()));
    watcher_.addPaths(QStringList(directoryPaths.begin(), directoryPaths.end()));
}
