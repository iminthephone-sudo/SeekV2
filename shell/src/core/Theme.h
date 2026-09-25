#pragma once

// Fluent-style theming modelled on QWidget-FancyUI's ColorProfile.json
// (light: #F3F3F3 window, dark: #202020 window, accent-driven highlights).
// Every widget colour comes from here so light/dark switch in one place.

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QString>

struct ThemePalette {
    QColor window;        // title bar + sidebar
    QColor content;       // rounded content area
    QColor card;          // card surfaces
    QColor cardHover;
    QColor border;
    QColor text;
    QColor textMuted;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor input;
    QColor inputFocus;
    QColor inputBorder;
    QColor mask;          // hover overlay
    QColor maskPressed;
    QColor selection;
    QColor success;
    QColor warning;
    QColor danger;
    QColor closeHover;
};

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { Light, Dark, System };
    Q_ENUM(Mode)

    static Theme* instance();

    Mode mode() const { return m_mode; }
    bool isDark() const { return m_dark; }
    const ThemePalette& palette() const { return m_palette; }
    const ThemePalette& p() const { return m_palette; }

    void setMode(Mode mode);
    void toggle();
    void apply();  // pushes stylesheet + QPalette to the application

    // Bootstrap icons bundled from FancyUI, recoloured for the current theme.
    QIcon icon(const QString& name, const QColor& color = QColor()) const;
    QPixmap pixmap(const QString& name, int size, const QColor& color = QColor()) const;
    QIcon appIcon() const;

    static QString css(const QColor& c);  // rgba() string usable in QSS

signals:
    void changed();

private:
    explicit Theme(QObject* parent = nullptr);
    void rebuild();
    QString styleSheet() const;

    Mode m_mode = Mode::System;
    bool m_dark = false;
    ThemePalette m_palette;
    mutable QHash<QString, QIcon> m_iconCache;
};
