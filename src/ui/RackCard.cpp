#include "ui/RackCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

#include "plugins/Lv2UiHost.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"
#include "ui/widgets/Knob.h"

namespace lspa {

namespace {

// Generic painting surface used for the mini visualisation.
class PaintWidget : public QWidget {
public:
    std::function<void(QPainter&, const QRect&)> onPaint;

    explicit PaintWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(74);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (onPaint) onPaint(p, rect().adjusted(1, 1, -1, -1));
    }
};

QString elideMiddle(const QString& text, int maxChars) {
    if (text.size() <= maxChars) return text;
    return text.left(maxChars / 2 - 1) + QChar(0x2026) + text.right(maxChars / 2 - 1);
}

}  // namespace

int graphKindForCategory(const QString& category, const QString& name) {
    const QString c = (category + " " + name).toLower();
    if (c.contains("eq") || c.contains("equalizer")) return 1;
    if (c.contains("limiter")) return 3;
    if (c.contains("compress") || c.contains("dynamics") || c.contains("gate") ||
        c.contains("expander"))
        return 2;
    if (c.contains("reverb")) return 4;
    if (c.contains("delay") || c.contains("echo")) return 5;
    if (c.contains("filter") || c.contains("crossover")) return 6;
    if (c.contains("distortion") || c.contains("overdrive") || c.contains("saturation"))
        return 7;
    return 0;
}

RackCard::RackCard(const QString& uid, RackTarget target, int number, const QString& title,
                   const QString& subtitle, const QColor& accent, QWidget* parent)
    : QWidget(parent), uid_(uid), target_(target), accent_(accent) {
    setObjectName("rackCard");
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    buildUi(title, subtitle, number);
}

RackCard::~RackCard() { closeNativeEditor(); }

void RackCard::buildUi(const QString& title, const QString& subtitle, int number) {
    auto* root = new QVBoxLayout(this);
    rootLayout_ = root;
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    // header: "1. EQ" + power + kebab
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    auto* titleLabel = new QLabel(QString("%1. %2").arg(number).arg(title), this);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: 700;");
    header->addWidget(titleLabel, 1);

    auto* powerBtn = new QToolButton(this);
    powerBtn->setCheckable(true);
    powerBtn->setChecked(true);
    powerBtn->setToolTip(tr("Enable / bypass this effect"));
    powerBtn->setFixedSize(24, 24);
    powerBtn->setAutoRaise(true);
    powerBtn->setCursor(Qt::PointingHandCursor);
    auto paintPower = [powerBtn, this](bool on) {
        // Mockup shows the enabled power icon in green on every card
        // (bypassed = dim), regardless of the card's accent color.
        powerBtn->setIcon(QIcon(icons::pixmap(icons::Icon::Power,
                                              on ? theme::kOk : theme::kTextFaint, 16)));
    };
    paintPower(true);
    connect(powerBtn, &QToolButton::toggled, this, [this, paintPower](bool on) {
        enabled_ = on;
        paintPower(on);
        emit toggled(uid_, on);
        update();
    });
    header->addWidget(powerBtn);

    auto* menuBtn = new QToolButton(this);
    menuBtn->setAutoRaise(true);
    menuBtn->setFixedSize(24, 24);
    menuBtn->setIcon(QIcon(icons::pixmap(icons::Icon::Kebab, theme::kTextDim, 16)));
    menuBtn->setToolTip(tr("Effect options"));
    connect(menuBtn, &QToolButton::clicked, this, &RackCard::showMenu);
    header->addWidget(menuBtn);
    root->addLayout(header);

    // subtitle: plugin name
    auto* sub = new QLabel(subtitle, this);
    sub->setStyleSheet(
        QString("color: %1; font-size: 11px;").arg(theme::kTextDim.name()));
    sub->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(sub);

    // mini visualisation
    auto* graph = new PaintWidget(this);
    graphHost_ = graph;
    graph->onPaint = [this](QPainter& p, const QRect& r) { drawMiniGraph(p, r); };
    root->addWidget(graph);

    // status (errors are text + icon, never colour only)
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setVisible(false);
    statusLabel_->setStyleSheet(
        QString("color: %1; font-size: 11px; background-color: %2; border-radius: 5px;"
                " padding: 4px;")
            .arg(theme::kWarn.name(), theme::kPanelDeep.name()));
    root->addWidget(statusLabel_);

    root->addStretch(1);
}

void RackCard::setParameters(const QVector<PluginParam>& params) {
    params_ = params;

    // Rebuild knobs for the first few continuous parameters (max 3, matching
    // the mockup: name above the knob, value below).
    // The knobs live in a dedicated host widget so a rebuild is a single
    // delete (a QWidget deletes its children).
    if (knobsHost_) {
        knobsHost_->deleteLater();
        knobsHost_ = nullptr;
    }
    knobs_.clear();

    auto* root = qobject_cast<QVBoxLayout*>(layout());
    knobsHost_ = new QWidget(this);
    auto* knobsRow = new QHBoxLayout(knobsHost_);
    knobsRow->setContentsMargins(0, 0, 0, 0);
    knobsRow->setSpacing(4);

    int shown = 0;
    for (const PluginParam& param : params_) {
        if (shown >= 3) break;
        if (param.isToggled) continue;
        const float span = param.max - param.min;
        if (span <= 0.0f) continue;

        auto* knob = new Knob(this);
        knob->setRange(param.min, param.max);
        knob->setValue(param.value);
        knob->setDefaultValue(param.def);
        knob->setUnit(QString::fromStdString(param.unit));
        knob->setBipolar(span > 0 && param.min < 0.0f && param.max > 0.0f);
        knob->setKnobColor(accent_);
        knob->setDigits(param.isInteger ? 0 : (span >= 20.0f ? 1 : 2));
        // Knob paints its own caption above the body and the value below it.
        QString caption = QString::fromStdString(param.name);
        if (caption.size() > 10) caption = caption.left(9) + QChar(0x2026);
        knob->setLabel(caption);
        knob->setToolTip(QString::fromStdString(param.name));

        const QString symbol = QString::fromStdString(param.symbol);
        const QString uid = uid_;
        connect(knob, &Knob::valueChanged, this, [this, symbol, uid](float v) {
            for (PluginParam& p : params_) {
                if (QString::fromStdString(p.symbol) == symbol) {
                    p.value = v;
                    break;
                }
            }
            emit paramChanged(uid, symbol, v);
            if (graphHost_) graphHost_->update();
        });

        knobsRow->addWidget(knob, 1);
        knobs_.push_back(knob);
        ++shown;
    }
    if (shown == 0) {
        auto* note = new QLabel(tr("No adjustable parameters"), this);
        note->setAlignment(Qt::AlignCenter);
        note->setStyleSheet(
            QString("color: %1; font-size: 11px;").arg(theme::kTextFaint.name()));
        knobsRow->addWidget(note);
    }
    knobsRow->addStretch(0);
    if (root) root->addWidget(knobsHost_);
}

void RackCard::setStatus(PluginLoadStatus status, const QString& error) {
    status_ = status;
    error_ = error;
    if (status == PluginLoadStatus::Loaded) {
        statusLabel_->setVisible(false);
        return;
    }
    QString prefix;
    switch (status) {
        case PluginLoadStatus::Missing:
            prefix = tr("Missing: ");
            break;
        case PluginLoadStatus::Unsupported:
            prefix = tr("Unsupported: ");
            break;
        case PluginLoadStatus::Failed:
            prefix = tr("Failed: ");
            break;
        default:
            prefix = tr("Not loaded: ");
            break;
    }
    const QString text = prefix + (error.isEmpty() ? tr("unknown reason") : error);
    statusLabel_->setText(QString::fromUtf8("\xe2\x9a\xa0 ") + elideMiddle(text, 120));
    statusLabel_->setToolTip(text);
    statusLabel_->setVisible(true);
    if (status == PluginLoadStatus::Failed || status == PluginLoadStatus::Missing) {
        statusLabel_->setStyleSheet(
            QString("color: %1; font-size: 11px; background-color: %2; border-radius: 5px;"
                    " padding: 4px;")
                .arg(theme::kError.name(), theme::kPanelDeep.name()));
    }
}

void RackCard::setCardEnabled(bool enabled) { enabled_ = enabled; }

bool RackCard::openNativeEditor(IPluginInstance* instance) {
    if (!instance || !instance->hasNativeUi()) return false;

    nativeHost_ = new QWidget(this);
    nativeHost_->setAttribute(Qt::WA_NativeWindow, true);
    nativeHost_->setMinimumSize(420, 260);
    nativeHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    nativeHost_->setStyleSheet(
        QString("background: %1; border: none;").arg(theme::kPanelDeep.name()));

    std::string error;
    if (!Lv2UiHost::instance().openEditor(instance, nativeHost_, error)) {
        nativeHost_->deleteLater();
        nativeHost_ = nullptr;
        if (statusLabel_) {
            statusLabel_->setText(tr("Native LSP UI unavailable; showing rack controls"));
            statusLabel_->setToolTip(QString::fromStdString(error));
            statusLabel_->setVisible(true);
        }
        return false;
    }

    nativeInstance_ = instance;
    rootLayout_->insertWidget(3, nativeHost_);
    if (graphHost_) graphHost_->hide();
    if (knobsHost_) knobsHost_->hide();
    return true;
}

void RackCard::closeNativeEditor() {
    if (nativeInstance_) {
        Lv2UiHost::instance().closeEditor(nativeInstance_);
        nativeInstance_ = nullptr;
    }
    if (nativeHost_) {
        nativeHost_->hide();
        nativeHost_->deleteLater();
        nativeHost_ = nullptr;
    }
}

void RackCard::showMenu() {
    QMenu menu(this);
    QAction* up = menu.addAction(tr("Move up"));
    QAction* down = menu.addAction(tr("Move down"));
    connect(up, &QAction::triggered, this, [this] { emit moveUpRequested(uid_); });
    connect(down, &QAction::triggered, this, [this] { emit moveDownRequested(uid_); });
    menu.addSeparator();
    QAction* rem = menu.addAction(tr("Remove from rack"));
    connect(rem, &QAction::triggered, this, [this] { emit removeRequested(uid_); });
    menu.exec(mapToGlobal(QPoint(8, 8)));
}

void RackCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QColor fill = theme::kPanelAlt;
    if (!enabled_) fill = fill.darker(125);
    p.setPen(QPen(enabled_ ? theme::kBorder : theme::kBorderSoft, 1.0));
    p.setBrush(fill);
    p.drawRoundedRect(r, 10, 10);

    // left accent bar marks the rack slot colour
    p.setPen(Qt::NoPen);
    p.setBrush(accent_);
    p.drawRoundedRect(QRectF(r.left() + 0.5, r.top() + 10, 3, r.height() - 20), 2, 2);
}

// ---------------------------------------------------------------------------
// mini visualisations
// ---------------------------------------------------------------------------

void RackCard::drawMiniGraph(QPainter& painter, const QRect& rect) {
    QPainter& p = painter;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // background well
    p.setPen(QPen(theme::kBorderSoft, 1));
    p.setBrush(theme::kPanelDeep);
    p.drawRoundedRect(QRectF(rect), 8, 8);

    const QRectF g = QRectF(rect).adjusted(8, 8, -8, -8);

    // grid
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int i = 1; i < 5; ++i) {
        const double x = g.left() + g.width() * i / 5.0;
        p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
    }
    for (int i = 1; i < 3; ++i) {
        const double y = g.top() + g.height() * i / 3.0;
        p.drawLine(QPointF(g.left(), y), QPointF(g.right(), y));
    }

    auto norm = [this](int index) -> float {
        if (index < 0 || index >= params_.size()) return 0.5f;
        const PluginParam& pm = params_[index];
        const float span = pm.max - pm.min;
        if (span <= 0.0f) return 0.5f;
        return std::clamp((pm.value - pm.min) / span, 0.0f, 1.0f);
    };

    QColor curve = accent_;
    QColor fill = QColor(accent_.red(), accent_.green(), accent_.blue(), 55);
    if (!enabled_) {
        curve = theme::kTextFaint;
        fill = QColor(120, 130, 145, 40);
    }

    QPolygonF line;
    const int steps = 64;
    switch (graphKind_) {
        case 1: {  // EQ: bell curve from (freq, gain, q)
            const float f = norm(0), gain = norm(1), q = norm(2);
            const double center = g.left() + g.width() * (0.15 + 0.7 * f);
            const double amp = (gain - 0.5) * 2.0 * (g.height() * 0.42);
            const double width = g.width() * (0.30 - 0.22 * q);
            const double base = g.center().y();
            for (int i = 0; i <= steps; ++i) {
                const double x = g.left() + g.width() * i / steps;
                const double d = (x - center) / std::max(1.0, width);
                const double y = base - amp * std::exp(-d * d);
                line << QPointF(x, y);
            }
            p.setPen(QPen(curve, 2));
            p.setBrush(fill);
            QPolygonF area = line;
            area << QPointF(g.right(), base) << QPointF(g.left(), base);
            p.drawPolygon(area);
            break;
        }
        case 2:   // compressor / gate: transfer curve with knee
        case 3: {  // limiter: same with a hard ceiling
            const float thr = norm(0);
            const float ratioN = norm(1);
            const double kneeX = g.left() + g.width() * (0.15 + 0.6 * thr);
            const double slope =
                graphKind_ == 3 ? 0.02 : std::max(0.08, (1.0 - 0.85 * ratioN));
            const double base = g.bottom();
            const double start = g.top() + g.height() * 0.05;
            for (int i = 0; i <= steps; ++i) {
                const double x = g.left() + g.width() * i / steps;
                double y;
                if (x <= kneeX) {
                    y = base - (x - g.left()) / (kneeX - g.left()) * (base - start);
                } else {
                    y = start - (x - kneeX) * slope;
                    y = std::max(g.top(), y);
                }
                line << QPointF(x, y);
            }
            p.setPen(QPen(curve, 2));
            p.drawPolyline(line);
            // reference diagonal
            p.setPen(QPen(QColor(255, 255, 255, 30), 1, Qt::DashLine));
            p.drawLine(QPointF(g.left(), base), QPointF(g.right(), g.top()));
            break;
        }
        case 4: {  // reverb: decaying impulse response
            const float decay = norm(1);
            const float mix = norm(0);
            const double base = g.center().y();
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                const double env = std::exp(-t * (6.0 - 5.0 * decay));
                const double y = base - env * std::sin(t * 44.0 + mix * 3.0) *
                                             (g.height() * 0.40);
                line << QPointF(g.left() + g.width() * t, y);
            }
            p.setPen(QPen(curve, 1.6));
            p.drawPolyline(line);
            break;
        }
        case 5: {  // delay: repeated echoes
            const float spacing = norm(0);
            const float fb = norm(1);
            const double base = g.bottom() - 6;
            const double period = g.width() * (0.08 + 0.28 * spacing);
            for (int k = 0; k < 8; ++k) {
                const double x = g.left() + 6 + k * period;
                if (x > g.right()) break;
                const double h = (g.height() * 0.65) * std::pow(fb, static_cast<double>(k));
                p.setPen(Qt::NoPen);
                p.setBrush(fill);
                p.drawRect(QRectF(x, base - h, 4, h));
            }
            p.setPen(QPen(curve, 2));
            p.drawLine(QPointF(g.left(), base), QPointF(g.right(), base));
            break;
        }
        case 6: {  // filter: magnitude response
            const float cutoff = norm(0);
            const double ny = g.left() + g.width() * (0.1 + 0.8 * cutoff);
            for (int i = 0; i <= steps; ++i) {
                const double x = g.left() + g.width() * i / steps;
                const double d = (x - ny) / (g.width() * 0.08);
                const double y = g.top() + g.height() * 0.18 + (g.height() * 0.62) *
                                                                   (1.0 / (1.0 + std::exp(-d)));
                line << QPointF(x, y);
            }
            p.setPen(QPen(curve, 2));
            p.setBrush(fill);
            QPolygonF area = line;
            area << QPointF(g.right(), g.bottom()) << QPointF(g.left(), g.bottom());
            p.drawPolygon(area);
            break;
        }
        case 7: {  // distortion: soft-clipped waveform
            const float drive = 0.3f + 0.7f * norm(0);
            const double base = g.center().y();
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                const double s = std::sin(t * 24.0) * drive * 2.4;
                const double y = base - std::tanh(s) * (g.height() * 0.38);
                line << QPointF(g.left() + g.width() * t, y);
            }
            p.setPen(QPen(curve, 2));
            p.drawPolyline(line);
            break;
        }
        default: {  // generic: gentle spectrum-ish curve
            const double base = g.center().y();
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                const double mod = 0.5 * norm(0) + 0.3 * norm(1) + 0.2 * norm(2);
                const double y = base - std::sin(t * 6.28 * (1.0 + mod)) * (g.height() * 0.28) -
                                 std::sin(t * 19.0) * (g.height() * 0.08);
                line << QPointF(g.left() + g.width() * t, y);
            }
            p.setPen(QPen(curve, 2));
            p.drawPolyline(line);
            break;
        }
    }

    p.restore();
}

}  // namespace lspa
