#include "ui/PluginEditor.h"

#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "plugins/Lv2UiHost.h"
#include "ui/SessionController.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"
#include "ui/widgets/Knob.h"

namespace lspa {

PluginEditor::PluginEditor(SessionController* controller, RackTarget target, const QString& uid,
                           const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window),
      controller_(controller),
      engine_(&controller->engine()),
      target_(target),
      uid_(uid),
      title_(title) {
    setWindowTitle(tr("Edit: %1").arg(title));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(460, 360);

    auto* root = new QVBoxLayout(this);
    root_ = root;
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    auto* header = new QHBoxLayout();
    auto* icon = new QLabel(this);
    icon->setPixmap(icons::pixmap(icons::Icon::Plugin, theme::kTextDim, 18));
    header->addWidget(icon);
    auto* titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: 700;");
    header->addWidget(titleLabel, 1);
    modeLabel_ = new QLabel(this);
    modeLabel_->setStyleSheet(
        QString("color: %1; font-size: 11px;").arg(theme::kTextFaint.name()));
    header->addWidget(modeLabel_);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    header->addWidget(closeBtn);
    root->addLayout(header);

    if (!tryNativeUi()) {
        buildGeneric();
    }

    timer_ = new QTimer(this);
    timer_->setInterval(120);
    connect(timer_, &QTimer::timeout, this, &PluginEditor::onTick);
    timer_->start();
}

PluginEditor::~PluginEditor() {
    if (nativeOpen_) {
        IPluginInstance* inst = engine_->slotInstance(target_, uid_.toStdString());
        if (inst) Lv2UiHost::instance().closeEditor(inst);
        nativeOpen_ = false;
    }
}

void PluginEditor::closeEvent(QCloseEvent* event) {
    if (nativeOpen_) {
        IPluginInstance* inst = engine_->slotInstance(target_, uid_.toStdString());
        if (inst) Lv2UiHost::instance().closeEditor(inst);
        nativeOpen_ = false;
    }
    QWidget::closeEvent(event);
}

bool PluginEditor::tryNativeUi() {
    IPluginInstance* inst = engine_->slotInstance(target_, uid_.toStdString());
    if (!inst || !inst->hasNativeUi()) return false;

    auto* container = new QWidget(this);
    container->setAttribute(Qt::WA_NativeWindow, true);
    container->setMinimumSize(360, 260);
    container->setStyleSheet(
        QString("background-color: %1; border: 1px solid %2; border-radius: 8px;")
            .arg(theme::kPanelDeep.name(), theme::kBorder.name()));

    std::string err;
    if (Lv2UiHost::instance().openEditor(inst, container, err)) {
        nativeHost_ = container;
        nativeOpen_ = true;
        modeLabel_->setText(tr("Native plugin UI"));
        root_->addWidget(container, 1);
        return true;
    }

    // Native UI unavailable: explain and fall back to the generic panel.
    modeLabel_->setText(tr("Generic parameters (native UI unavailable)"));
    auto* note = new QLabel(QString::fromUtf8("\xe2\x9a\xa0 ")
                                + tr("The plugin's own interface could not be opened: %1")
                                      .arg(QString::fromStdString(err)),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(
        QString("color: %1; font-size: 11px; background-color: %2; border-radius: 6px;"
                " padding: 6px;")
            .arg(theme::kWarn.name(), theme::kPanelDeep.name()));
    root_->addWidget(note);
    container->deleteLater();
    return false;
}

void PluginEditor::buildGeneric() {
    if (modeLabel_->text().isEmpty()) modeLabel_->setText(tr("Generic parameters"));

    genericPanel_ = new QWidget(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(genericPanel_);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");

    auto* grid = new QGridLayout(genericPanel_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(14);

    const auto liveParams = engine_->slotParameters(target_, uid_.toStdString());
    const QVector<PluginParam> params(liveParams.begin(), liveParams.end());

    int row = 0, col = 0;
    for (const PluginParam& pm : params) {
        auto* box = new QVBoxLayout();
        box->setSpacing(4);
        auto* name = new QLabel(QString::fromStdString(pm.name), genericPanel_);
        name->setAlignment(Qt::AlignCenter);
        name->setStyleSheet(
            QString("color: %1; font-size: 11px;").arg(theme::kTextDim.name()));

        auto* knob = new Knob(genericPanel_);
        knob->setRange(pm.min, pm.max);
        knob->setValue(pm.value);
        knob->setDefaultValue(pm.def);
        knob->setUnit(QString::fromStdString(pm.unit));
        knob->setBipolar(pm.min < 0.0f && pm.max > 0.0f);
        const float span = pm.max - pm.min;
        knob->setDigits(span >= 50.0f ? 1 : 2);

        const QString symbol = QString::fromStdString(pm.symbol);
        connect(knob, &Knob::valueChanged, this, [this, symbol](float v) {
            controller_->setParameter(target_, uid_, symbol, v);
        });
        connect(knob, &Knob::dragStarted, this, [this] { timer_->stop(); });
        connect(knob, &Knob::dragFinished, this, [this] { timer_->start(); });

        box->addWidget(knob, 0, Qt::AlignHCenter);
        box->addWidget(name);
        grid->addLayout(box, row, col);
        knobs_.push_back(knob);
        if (++col >= 4) {
            col = 0;
            ++row;
        }
    }
    if (params.isEmpty()) {
        auto* note = new QLabel(tr("This plugin exposes no adjustable parameters."), genericPanel_);
        note->setStyleSheet(
            QString("color: %1; font-size: 12px;").arg(theme::kTextFaint.name()));
        grid->addWidget(note, 0, 0, 1, 4);
    }

    root_->addWidget(scroll, 1);
}

void PluginEditor::onTick() {
    if (!genericPanel_) return;
    // Refresh in port order (the same order used when building the knobs).
    const auto live = engine_->slotParameters(target_, uid_.toStdString());
    for (size_t i = 0; i < live.size() && i < static_cast<size_t>(knobs_.size()); ++i) {
        Knob* knob = knobs_[static_cast<int>(i)];
        const float v = live[i].value;
        if (std::fabs(knob->value() - v) > 1e-4f) knob->setValue(v);
    }
}

}  // namespace lspa
