// Theme.cpp - application palette and stylesheet (dark studio look).

#include "ui/theme/Theme.h"

namespace lspa::theme {

QColor rackColor(int index) {
    // Vivid but not garish; all read well on #161B22 panels. First entries
    // match the mockup accents (Main Player blue, BGM green).
    static const QColor kPalette[] = {
        QColor(56, 139, 253),    // #388BFD blue
        QColor(63, 220, 168),    // #3FDCA8 green
        QColor(210, 168, 74),    // #D2A84A amber
        QColor(188, 140, 242),   // #BC8CF2 violet
        QColor(88, 166, 255),    // #58A6FF sky
        QColor(247, 120, 186),   // #F778BA pink
        QColor(126, 231, 135),   // #7EE787 lime
        QColor(255, 158, 100),   // #FF9E64 orange
    };
    static constexpr int kCount = int(sizeof(kPalette) / sizeof(kPalette[0]));
    int i = index % kCount;
    if (i < 0) {
        i += kCount;  // C++ modulo keeps the sign; normalize for negatives
    }
    return kPalette[i];
}

QString appStyleSheet() {
    // Restrained dark studio sheet: near-black surfaces, 1px #303947 borders,
    // 6-8px radii, generous padding. Accent per variant, blue default.
    return QStringLiteral(R"QSS(
/* ---------- base ---------- */
QMainWindow {
    background: #0D1117;
}
QWidget {
    background: transparent;
    color: #E6EDF3;
    font-family: "Sans Serif";
    font-size: 10pt;
}
/* monospace readouts (dynamic property or objectName containing Mono/Value) */
QLabel[value="mono"], QLineEdit[value="mono"], QSpinBox[value="mono"],
QLabel[class="mono"], QLineEdit[class="mono"],
QLabel[objectName="valueLabel"], QLabel[objectName="monoLabel"] {
    font-family: "DejaVu Sans Mono";
}

/* ---------- buttons ---------- */
QPushButton {
    background: #1E242D;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 8px;
    padding: 8px 14px;
    min-height: 18px;
}
QPushButton:hover {
    background: #242C37;
    border-color: #3D4859;
}
QPushButton:pressed {
    background: #161B22;
}
QPushButton:disabled { color: #5C6674; background: #161B22; border-color: #262E3A; }
QPushButton:focus { border-color: #388BFD; }

QPushButton[variant="primary"] {
    background: #388BFD;
    color: #0D1117;
    border: 1px solid #388BFD;
    font-weight: 600;
}
QPushButton[variant="primary"]:hover { background: #58A6FF; border-color: #58A6FF; }
QPushButton[variant="primary"]:pressed { background: #246AD2; border-color: #246AD2; }
QPushButton[variant="primary"]:disabled { background: #1E242D; color: #5C6674; border-color: #262E3A; }

QPushButton[variant="success"] {
    background: #3FDCA8;
    color: #0D1117;
    border: 1px solid #3FDCA8;
    font-weight: 600;
}
QPushButton[variant="success"]:hover { background: #6FE9C2; border-color: #6FE9C2; }
QPushButton[variant="success"]:pressed { background: #26AA80; border-color: #26AA80; }
QPushButton[variant="success"]:disabled { background: #1E242D; color: #5C6674; border-color: #262E3A; }

QPushButton[variant="danger"] {
    background: #F85157;
    color: #0D1117;
    border: 1px solid #F85157;
    font-weight: 600;
}
QPushButton[variant="danger"]:hover { background: #FF7A80; border-color: #FF7A80; }
QPushButton[variant="danger"]:pressed { background: #D23B41; border-color: #D23B41; }
QPushButton[variant="danger"]:disabled { background: #1E242D; color: #5C6674; border-color: #262E3A; }

QPushButton[variant="ghost"] {
    background: transparent;
    color: #8B949E;
    border: 1px solid #303947;
}
QPushButton[variant="ghost"]:hover { color: #E6EDF3; border-color: #3D4859; background: #1A2029; }
QPushButton[variant="ghost"]:pressed { background: #161B22; }

QToolButton {
    background: transparent;
    color: #8B949E;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 6px;
}
QToolButton:hover {
    background: #1E242D;
    color: #E6EDF3;
    border-color: #303947;
}
QToolButton:pressed {
    background: #161B22;
}
QToolButton:checked { background: #1E242D; color: #388BFD; border-color: #303947; }
QToolButton:disabled { color: #5C6674; }

/* ---------- lists ---------- */
QListWidget, QListView, QTreeWidget, QTreeView, QTableWidget {
    background: #10151B;
    alternate-background-color: #161B22;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 8px;
    padding: 4px;
    outline: none;
}
QListWidget::item, QListView::item, QTreeWidget::item {
    padding: 7px 8px;
    border-radius: 5px;
    min-height: 18px;
}
QListWidget::item:hover, QListView::item:hover, QTreeWidget::item:hover {
    background: #1E242D;
}
QListWidget::item:selected, QListView::item:selected, QTreeWidget::item:selected {
    background: rgba(56, 139, 253, 40);
    color: #E6EDF3;
    border: 1px solid #388BFD;
}
QListWidget::item:selected:!active, QListView::item:selected:!active {
    background: rgba(56, 139, 253, 24);
    color: #8B949E;
    border: 1px solid #263950;
}
QHeaderView::section {
    background: #161B22;
    color: #8B949E;
    border: none;
    border-bottom: 1px solid #303947;
    padding: 5px 8px;
}

/* ---------- combo / edit ---------- */
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #10151B;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 6px;
    padding: 6px 10px;
    min-height: 18px;
    selection-background-color: #388BFD;
    selection-color: #0D1117;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #3D4859; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #388BFD; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 22px;
    border: none;
    background: transparent;
}
QComboBox QAbstractItemView {
    background: #161B22;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 6px;
    selection-background-color: rgba(56, 139, 253, 40);
    selection-color: #E6EDF3;
    padding: 4px;
}
QComboBox QAbstractItemView::item { padding: 6px 10px; min-height: 18px; }

/* ---------- sliders ---------- */
QSlider::groove:horizontal {
    height: 5px;
    background: #10151B;
    border: 1px solid #262E3A;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: #388BFD;
    border: 1px solid #388BFD;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #E6EDF3;
    border: 1px solid #303947;
    width: 13px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: #FFFFFF; border-color: #388BFD; }
QSlider::groove:vertical {
    width: 5px;
    background: #10151B;
    border: 1px solid #262E3A;
    border-radius: 2px;
}
QSlider::add-page:vertical {
    background: #388BFD;
    border: 1px solid #388BFD;
    border-radius: 2px;
}
QSlider::handle:vertical {
    background: #E6EDF3;
    border: 1px solid #303947;
    height: 13px;
    margin: 0 -5px;
    border-radius: 7px;
}
QSlider::handle:vertical:hover { background: #FFFFFF; border-color: #388BFD; }

/* ---------- scrollbar ---------- */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #303947;
    min-height: 30px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover { background: #3D4859; }
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #303947;
    min-width: 30px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover { background: #3D4859; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; background: none; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QScrollBarCorner { background: transparent; }

/* ---------- checkbox / groupbox ---------- */
QCheckBox {
    spacing: 8px;
    color: #E6EDF3;
    padding: 2px;
}
QCheckBox::indicator {
    width: 17px;
    height: 17px;
    border: 1px solid #303947;
    border-radius: 4px;
    background: #10151B;
}
QCheckBox::indicator:hover { border-color: #3D4859; }
QCheckBox::indicator:checked {
    background: #388BFD;
    border-color: #388BFD;
}
QCheckBox::indicator:disabled { background: #161B22; border-color: #262E3A; }
QGroupBox {
    background: #161B22;
    border: 1px solid #303947;
    border-radius: 8px;
    margin-top: 14px;
    padding: 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    top: 2px;
    padding: 0 4px;
    color: #8B949E;
}

/* ---------- menu / tooltip ---------- */
QMenu {
    background: #161B22;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 8px;
    padding: 6px;
}
QMenu::item {
    background: transparent;
    border-radius: 5px;
    padding: 7px 22px 7px 12px;
}
QMenu::item:selected { background: rgba(56, 139, 253, 40); }
QMenu::item:disabled { color: #5C6674; }
QMenu::separator { height: 1px; background: #262E3A; margin: 5px 8px; }
QToolTip {
    background: #1E242D;
    color: #E6EDF3;
    border: 1px solid #303947;
    border-radius: 5px;
    padding: 5px 8px;
    font-size: 9pt;
}

/* ---------- tabs ---------- */
QTabWidget::pane {
    border: 1px solid #303947;
    border-radius: 8px;
    background: #161B22;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #8B949E;
    border: 1px solid transparent;
    border-bottom: 2px solid transparent;
    padding: 7px 14px;
    margin-right: 2px;
}
QTabBar::tab:selected {
    color: #E6EDF3;
    border-bottom: 2px solid #388BFD;
}
QTabBar::tab:hover:!selected { color: #E6EDF3; background: #1A2029; }

/* ---------- progress ---------- */
QProgressBar {
    background: #10151B;
    border: 1px solid #303947;
    border-radius: 5px;
    text-align: center;
    color: #8B949E;
    min-height: 10px;
}
QProgressBar::chunk {
    background: #388BFD;
    border-radius: 4px;
}
)QSS");
}

}  // namespace lspa::theme
