#include "ui/RackView.h"

#include <QComboBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "ui/RackCard.h"
#include "ui/SessionController.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"

namespace lspa {

RackView::RackView(SessionController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    reload();

    connect(controller_, &SessionController::rackChanged, this,
            [this](RackTarget target) {
                if (target == target_) reload();
            });
}

QColor RackView::accent() const {
    return target_ == RackTarget::MainPlayer ? theme::kAccent : theme::kAccent2;
}

void RackView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 14);
    root->setSpacing(10);

    // ------------------------------------------------------------- header row
    auto* header = new QHBoxLayout();
    header->setSpacing(10);

    auto* linkIcon = new QLabel(this);
    linkIcon->setPixmap(icons::pixmap(icons::Icon::Link, theme::kTextDim, 18));
    header->addWidget(linkIcon);

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet("font-size: 15px; font-weight: 700;");
    header->addWidget(titleLabel_);

    targetCombo_ = new QComboBox(this);
    targetCombo_->addItem(tr("Main Player"));
    targetCombo_->addItem(tr("BGM Player"));
    targetCombo_->setToolTip(tr("Choose which player's effects rack is shown"));
    targetCombo_->setFixedWidth(140);
    connect(targetCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        setTarget(index == 0 ? RackTarget::MainPlayer : RackTarget::BgmPlayer);
        emit targetSelected(target_);
    });
    header->addWidget(targetCombo_);
    header->addStretch(1);

    auto* presetLabel = new QLabel(tr("Preset"), this);
    presetLabel->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextDim.name()));
    header->addWidget(presetLabel);

    presetCombo_ = new QComboBox(this);
    presetCombo_->setMinimumWidth(150);
    presetCombo_->setToolTip(tr("Rack presets (FR-10)"));
    header->addWidget(presetCombo_);

    auto makeHeaderBtn = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            QString("QPushButton { background-color: %1; border: 1px solid %2; border-radius:"
                    " 6px; padding: 4px 12px; }"
                    "QPushButton:hover { background-color: %3; }")
                .arg(theme::kPanelAlt.name(), theme::kBorder.name(),
                     theme::kPanelAlt.darker(120).name()));
        return b;
    };
    auto* saveBtn = makeHeaderBtn(tr("Save"), tr("Save this rack as a preset"));
    auto* loadBtn = makeHeaderBtn(tr("Load"), tr("Load the selected preset"));
    connect(saveBtn, &QPushButton::clicked, this, &RackView::onSavePreset);
    connect(loadBtn, &QPushButton::clicked, this, &RackView::onLoadPreset);
    header->addWidget(saveBtn);
    header->addWidget(loadBtn);

    auto* menuBtn = new QToolButton(this);
    menuBtn->setAutoRaise(true);
    menuBtn->setIcon(QIcon(icons::pixmap(icons::Icon::Kebab, theme::kTextDim, 18)));
    menuBtn->setToolTip(tr("Rack options"));
    connect(menuBtn, &QToolButton::clicked, this, [this, menuBtn] {
        QMenu menu(this);
        menu.addAction(tr("Reload rack (reinstantiate plugins)"), this,
                       [this] { controller_->rebuildRack(target_); });
        menu.addAction(tr("Refresh plugin catalog"), this,
                       [this] { controller_->startCatalogScan(true); });
        menu.addSeparator();
        QAction* clear = menu.addAction(tr("Clear rack"));
        connect(clear, &QAction::triggered, this, [this] {
            const auto info = controller_->rackInfo(target_);
            for (const auto& s : info) {
                controller_->removePlugin(target_, QString::fromStdString(s.uid));
            }
        });
        menu.exec(menuBtn->mapToGlobal(QPoint(0, menuBtn->height())));
    });
    header->addWidget(menuBtn);

    root->addLayout(header);

    // ------------------------------------------------------------ card flow
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet("QScrollArea { background: transparent; }");

    flow_ = new QWidget(scroll_);
    flow_->setAttribute(Qt::WA_StyledBackground, true);
    flow_->installEventFilter(this);
    flowLayout_ = new QHBoxLayout(flow_);
    flowLayout_->setContentsMargins(0, 0, 0, 4);
    flowLayout_->setSpacing(10);
    flowLayout_->addStretch(1);
    scroll_->setWidget(flow_);
    root->addWidget(scroll_, 1);

    auto* hint = new QLabel(
        tr("The rack order is the signal order: audio flows left to right. "
           "Use a card's menu to reorder, edit or remove it."),
        this);
    hint->setStyleSheet(
        QString("color: %1; font-size: 11px;").arg(theme::kTextFaint.name()));
    root->addWidget(hint);
}

void RackView::setTarget(RackTarget target) {
    if (target_ == target) return;
    target_ = target;
    targetCombo_->setCurrentIndex(target == RackTarget::MainPlayer ? 0 : 1);
    reload();
}

void RackView::reload() {
    const QString name = QString::fromUtf8(rackTargetName(target_));
    titleLabel_->setText(tr("Effects Rack (%1)").arg(name));
    presetCombo_->clear();
    presetCombo_->addItems(controller_->presets());
    presetCombo_->setCurrentIndex(-1);
    rebuildCards();
}

void RackView::rebuildCards() {
    // Remove old cards and arrows entirely; the trailing "Add Plugin" card
    // is recreated at the end below (keeping stale items across rebuilds had
    // accumulated duplicates at both ends).
    while (flowLayout_->count() > 0) {
        QLayoutItem* item = flowLayout_->takeAt(0);
        if (item) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    arrowCards_.clear();

    const QVector<RackSlotInfo> info = controller_->rackInfo(target_);
    const QColor color = accent();
    int number = 1;
    QLabel* pendingArrow = nullptr;

    for (const RackSlotInfo& slot : info) {
        if (number > 1) {
            auto* arrow = new QLabel(this);
            arrow->setPixmap(icons::pixmap(icons::Icon::ArrowRight, theme::kTextFaint, 20));
            arrow->setFixedWidth(20);
            // AlignTop (not AlignVCenter): flow_ stretches to the full
            // viewport height on the Rack tab, which would drag a VCentered
            // arrow to mid-panel. Height is synced to the following card in
            // syncArrowHeights() so the pixmap sits at the card's center.
            arrow->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            flowLayout_->addWidget(arrow, 0, Qt::AlignTop);
            pendingArrow = arrow;
        }

        // Card header: short category as the title (like the mockup's
        // "1. EQ"), the full plugin name as the subtitle.
        QString title = slot.category;
        if (title.isEmpty() || title == QLatin1String("Other")) {
            title = slot.displayName.isEmpty() ? tr("Effect") : slot.displayName;
        }
        auto* card = new RackCard(QString::fromStdString(slot.uid), target_, number, title,
                                  slot.pluginName.isEmpty() ? QString::fromStdString(slot.key.id)
                                                            : slot.pluginName,
                                  color, this);
        card->setMiniGraphKind(graphKindForCategory(slot.category, slot.pluginName));
        card->setStatus(slot.status, slot.error);
        card->setCardEnabled(slot.enabled);

        // Live parameter values from the engine, falling back to the saved
        // session values for slots that failed to load.
        QVector<PluginParam> params;
        const auto live = controller_->engine().slotParameters(target_, slot.uid);
        if (!live.empty()) {
            params = QVector<PluginParam>(live.begin(), live.end());
        } else {
            for (const RackEntry& entry : controller_->rackEntries(target_)) {
                if (entry.uid != slot.uid) continue;
                for (const auto& [symbol, value] : entry.parameters) {
                    PluginParam pm;
                    pm.symbol = symbol;
                    pm.name = symbol;
                    pm.min = 0.0f;
                    pm.max = 1.0f;
                    pm.value = value;
                    params.push_back(pm);
                }
                break;
            }
        }
        card->setParameters(params);

        const QString uid = QString::fromStdString(slot.uid);
        connect(card, &RackCard::toggled, controller_,
                [this](const QString& uid, bool on) { controller_->togglePlugin(target_, uid, on); });
        connect(card, &RackCard::paramChanged, controller_,
                [this](const QString& uid, const QString& symbol, float value) {
                    controller_->setParameter(target_, uid, symbol, value);
                });
        connect(card, &RackCard::removeRequested, controller_,
                [this](const QString& uid) { controller_->removePlugin(target_, uid); });
        connect(card, &RackCard::moveUpRequested, controller_,
                [this](const QString& uid) { controller_->movePlugin(target_, uid, -1); });
        connect(card, &RackCard::moveDownRequested, controller_,
                [this](const QString& uid) { controller_->movePlugin(target_, uid, +1); });
        connect(card, &RackCard::editRequested, this,
                [this](const QString& uid) { emit editPluginRequested(target_, uid); });

        flowLayout_->addWidget(card, 0, Qt::AlignTop);
        if (pendingArrow) {
            arrowCards_.append(qMakePair(pendingArrow, static_cast<QWidget*>(card)));
            pendingArrow = nullptr;
        }
        ++number;
    }

    // Add-plugin placeholder card: custom widget so the dashed border and the
    // circled "+" render exactly like the mockup (a QPushButton with a layout
    // gets squeezed by the style sheet box model).
    class AddCard : public QWidget {
    public:
        AddCard(const QColor& accent, std::function<void()> onClick, QWidget* parent = nullptr)
            : QWidget(parent), accent_(accent), onClick_(std::move(onClick)) {
            setObjectName("addPluginCard");
            setCursor(Qt::PointingHandCursor);
            setToolTip(tr("Add a plugin to this rack"));
            setFocusPolicy(Qt::StrongFocus);
            setFixedWidth(150);
            // Match the plugin cards' height (the flow stretches cards to the
            // viewport; AlignTop keeps this pinned to the top).
            setMinimumHeight(300);
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

            auto* lay = new QVBoxLayout(this);
            lay->addStretch(1);
            circle_ = new QLabel(this);
            circle_->setFixedSize(52, 52);
            circle_->setAlignment(Qt::AlignCenter);
            circle_->setText(QChar(0x2B));
            lay->addWidget(circle_, 0, Qt::AlignHCenter);
            lay->addSpacing(10);
            caption_ = new QLabel(tr("Add Plugin"), this);
            caption_->setAlignment(Qt::AlignCenter);
            lay->addWidget(caption_);
            lay->addStretch(1);
            restyle(false);
        }

        void restyle(bool hover) {
            const QColor c = hover ? accent_ : theme::kBorder;
            setStyleSheet("QWidget#addPluginCard { background: transparent; }");
            update();
            circle_->setStyleSheet(
                QString("color: %1; background-color: %2; border-radius: 26px; font-size: 26px;"
                        " border: none;")
                    .arg(c.name(), theme::kPanelAlt.name()));
            caption_->setStyleSheet(
                QString("color: %1; font-size: 12px; background: transparent; border: none;")
                    .arg((hover ? accent_ : theme::kTextDim).name()));
        }

    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(hover_ ? accent_ : theme::kBorder, 2, Qt::DashLine);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
        }
        void enterEvent(QEnterEvent* e) override {
            hover_ = true;
            restyle(true);
            QWidget::enterEvent(e);
        }
        void leaveEvent(QEvent* e) override {
            hover_ = false;
            restyle(false);
            QWidget::leaveEvent(e);
        }
        void mouseReleaseEvent(QMouseEvent* e) override {
            if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
                if (onClick_) onClick_();
            }
        }
        void keyPressEvent(QKeyEvent* e) override {
            if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
                if (onClick_) onClick_();
            } else {
                QWidget::keyPressEvent(e);
            }
        }

    private:
        QColor accent_;
        bool hover_ = false;
        std::function<void()> onClick_;
        QLabel* circle_ = nullptr;
        QLabel* caption_ = nullptr;
    };

    auto* addCard = new AddCard(color, [this] { emit addPluginRequested(); }, this);
    flowLayout_->addWidget(addCard, 0, Qt::AlignTop);

    // Match each arrow's height to its card once layout has run, so the
    // pixmap lands at the card's vertical center regardless of viewport.
    QTimer::singleShot(0, this, &RackView::syncArrowHeights);
}

void RackView::syncArrowHeights() {
    for (const auto& [arrow, card] : arrowCards_) {
        if (!arrow || !card) continue;
        arrow->setFixedHeight(card->height() > 0 ? card->height() : card->sizeHint().height());
    }
}

bool RackView::eventFilter(QObject* watched, QEvent* event) {
    // Card heights follow the viewport (Rack tab is much taller than the
    // Player tab), so re-sync the arrows whenever the flow is re-laid out.
    if (watched == flow_ && (event->type() == QEvent::Resize ||
                             event->type() == QEvent::LayoutRequest)) {
        QTimer::singleShot(0, this, &RackView::syncArrowHeights);
    }
    return QWidget::eventFilter(watched, event);
}

void RackView::onSavePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save rack preset"),
                                               tr("Preset name:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString err;
    if (!controller_->savePreset(target_, name, &err)) {
        QMessageBox::warning(this, tr("Preset not saved"), err);
        return;
    }
    presetCombo_->clear();
    presetCombo_->addItems(controller_->presets());
    presetCombo_->setCurrentText(name.trimmed());
}

void RackView::onLoadPreset() {
    const QString name = presetCombo_->currentText();
    if (name.isEmpty()) return;
    QString err;
    if (!controller_->loadPreset(target_, name, &err)) {
        QMessageBox::warning(this, tr("Preset not loaded"), err);
    }
}

}  // namespace lspa
