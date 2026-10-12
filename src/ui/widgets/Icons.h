// Icons.h - lightweight QPainter-drawn icons (no icon theme dependency).

#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace lspa::icons {

enum class Icon {
    Play,
    Pause,
    Stop,
    Prev,
    Next,
    Shuffle,
    Repeat,
    Volume,
    VolumeMute,
    Plus,
    Trash,
    ClearList,
    Kebab,
    Power,
    Search,
    Gear,
    Link,
    Music,
    Plugin,
    Wave,
    Folder,
    Save,
    Load,
    ArrowRight,
    Check,
    Warning,
    Close,
    Drag,
    Windows,
    Linux,
    Cover,
    Metronome,
    Logo
};

// color <= invalid() means "use default" (white-ish).
QIcon icon(Icon id, const QColor& color = QColor(), int size = 24);
QPixmap pixmap(Icon id, const QColor& color, int size);

// Material Symbols Rounded SVG shipped in src/ui/resources/material (qrc
// ":/material/<name>.svg"), tinted to `color` and rendered at `size`.
QPixmap material(const char* name, const QColor& color, int size);

// Header mark from the mockup: glowing ring (Main, teal with dark hole) or
// glowing disc (BGM, green).
QPixmap playerMark(bool isMain, int size);

}  // namespace lspa::icons
