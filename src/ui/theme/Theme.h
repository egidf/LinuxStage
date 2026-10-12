// Theme.h - application palette and stylesheet (dark studio look).

#pragma once

#include <QColor>
#include <QString>

namespace lspa::theme {

// Base palette
inline const QColor kWindow(4, 11, 19);          // #040B13 app background (mockup)
inline const QColor kPanel(22, 27, 34);         // #161B22 panels
inline const QColor kPanelAlt(30, 36, 45);      // #1E242D cards / list rows
inline const QColor kPanelDeep(16, 21, 27);     // inner wells
inline const QColor kBorder(48, 57, 71);        // #303947 borders
inline const QColor kBorderSoft(38, 46, 58);
inline const QColor kText(230, 237, 243);       // #E6EDF3
inline const QColor kTextDim(139, 148, 158);    // #8B949E
inline const QColor kTextFaint(92, 102, 116);

// Accents: Main Player = blue, BGM Player = green (see mockup)
inline const QColor kAccent(56, 139, 253);      // #388BFD
inline const QColor kAccentDark(36, 106, 210);
inline const QColor kAccent2(63, 220, 168);     // #3FDCA8
inline const QColor kAccent2Dark(38, 170, 128);

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
