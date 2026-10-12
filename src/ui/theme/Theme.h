// Theme.h - application palette and stylesheet (dark studio look).

#pragma once

#include <QColor>
#include <QString>

namespace lspa::theme {

// Base palette tuned for the LSP plugin family look: dark charcoal surfaces,
// warm orange accents, and muted teal highlights, while keeping the Linux Stage app shell.
inline const QColor kWindow(9, 12, 18);          // #090C12
inline const QColor kPanel(21, 25, 31);           // dark panel surface
inline const QColor kPanelAlt(31, 36, 44);        // cards / elevated rows
inline const QColor kPanelDeep(13, 16, 22);       // inner wells
inline const QColor kBorder(58, 63, 72);          // subtle separators
inline const QColor kBorderSoft(45, 51, 60);
inline const QColor kText(234, 238, 244);          // light text
inline const QColor kTextDim(148, 155, 168);       // secondary text
inline const QColor kTextFaint(110, 120, 136);

// LSP-inspired accent palette: warm orange primary, teal secondary.
inline const QColor kAccent(255, 138, 61);        // #FF8A3D
inline const QColor kAccentDark(197, 99, 42);
inline const QColor kAccent2(70, 196, 181);       // #46C4B5
inline const QColor kAccent2Dark(42, 149, 138);

// Status
inline const QColor kOk(63, 220, 168);
inline const QColor kWarn(210, 168, 74);
inline const QColor kError(248, 81, 87);
inline const QColor kWavedeck(88, 166, 255);

// Returns a stable color for rack slot index (mini-graph strokes etc.)
QColor rackColor(int index);

// Full application stylesheet.
QString appStyleSheet();

}  // namespace lspa::theme
