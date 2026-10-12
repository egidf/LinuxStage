#include "ui/PluginBrowser.h"

#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "ui/SessionController.h"
#include "ui/theme/Theme.h"
#include "ui/widgets/Icons.h"

namespace lspa {

namespace {

// Small bordered tag chip used inside catalog rows.
QLabel* makeTagChip(const QString& text, const QColor& color, QWidget* parent) {
    auto* chip = new QLabel(text, parent);
    chip->setStyleSheet(
        QString("QLabel { color: %1; border: 1px solid %2; border-radius: 4px;"
                " padding: 1px 6px; font-size: 9px; background: transparent; }")
            .arg(color.name(), color.name()));
    return chip;
}

// One catalog row: colored icon, name, tag chips and a bordered "add" button.
class PluginRow : public QWidget {
public:
    PluginRow(const PluginDescriptor& d, std::function<void()> addCallback, QWidget* parent = nullptr)
        : QWidget(parent) {
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(10, 6, 8, 6);
        lay->setSpacing(10);

        // colored rounded-square icon keyed on the category (mockup style)
        const QColor catColor =
            theme::rackColor(qHash(QString::fromStdString(d.category)) % 64);
        auto* icon = new QLabel(this);
        icon->setFixedSize(30, 30);
        auto iconPix = QPixmap(30, 30);
        iconPix.fill(Qt::transparent);
        {
            QPainter p(&iconPix);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(catColor.red(), catColor.green(), catColor.blue(), 46));
            p.drawRoundedRect(QRectF(0, 0, 30, 30), 8, 8);
            p.setPen(catColor);
            const QRectF inner(4, 4, 22, 22);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(inner, 6, 6);
            p.setPen(QPen(catColor, 2));
            p.drawLine(QPointF(9, 20), QPointF(9, 11));
            p.drawLine(QPointF(15, 20), QPointF(15, 8));
            p.drawLine(QPointF(21, 20), QPointF(21, 13));
        }
        icon->setPixmap(iconPix);
        lay->addWidget(icon);

        auto* col = new QVBoxLayout();
        col->setSpacing(3);
        auto* name = new QLabel(QString::fromStdString(d.name), this);
        name->setStyleSheet(
            QString("color: %1; font-size: 12px; font-weight: 600; background: transparent;")
                .arg(theme::kText.name()));
        col->addWidget(name);

        auto* tagRow = new QHBoxLayout();
        tagRow->setSpacing(4);
        const QString category = QString::fromStdString(d.category);
        if (!category.isEmpty()) tagRow->addWidget(makeTagChip(category, catColor, this));
        tagRow->addWidget(
            makeTagChip(pluginFormatName(d.key.format), theme::kAccent, this));
        if (d.hasUi) {
            tagRow->addWidget(makeTagChip(tr("UI"), theme::kAccent2, this));
        }
        tagRow->addStretch(1);
        col->addLayout(tagRow);
        lay->addLayout(col, 1);

        auto* add = new QToolButton(this);
        add->setAutoRaise(true);
        add->setFixedSize(26, 26);
        add->setToolTip(tr("Add to rack"));
        add->setIcon(QIcon(icons::pixmap(icons::Icon::Plus, theme::kTextDim, 15)));
        add->setStyleSheet(
            QString("QToolButton { border: 1px solid %1; border-radius: 6px;"
                    " background: transparent; }"
                    "QToolButton:hover { border-color: %2; background-color: %3; }")
                .arg(theme::kBorder.name(), catColor.name(),
                     QColor(255, 255, 255, 18).name(QColor::HexArgb)));
        lay->addWidget(add);

        connect(add, &QToolButton::clicked, this, [addCallback] {
            if (addCallback) addCallback();
        });
    }
};

}  // namespace

PluginBrowser::PluginBrowser(SessionController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("pluginBrowser");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    auto* headerRow = new QHBoxLayout();
    auto* title = new QLabel(tr("Plugin Browser"), this);
    title->setStyleSheet("font-size: 14px; font-weight: 700;");
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    auto* refreshBtn = new QToolButton(this);
    refreshBtn->setAutoRaise(true);
    refreshBtn->setToolTip(tr("Rescan installed plugins"));
    refreshBtn->setIcon(QIcon(icons::pixmap(icons::Icon::Load, theme::kTextDim, 16)));
    connect(refreshBtn, &QToolButton::clicked, this,
            [this] { controller_->startCatalogScan(true); });
    headerRow->addWidget(refreshBtn);
    root->addLayout(headerRow);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search plugin..."));
    search_->setClearButtonEnabled(true);
    search_->addAction(QIcon(icons::pixmap(icons::Icon::Search, theme::kTextDim, 14)),
                       QLineEdit::LeadingPosition);
    search_->setStyleSheet(
        QString("QLineEdit { background-color: %1; border: 1px solid %2; border-radius: 6px;"
                " padding: 6px 10px; color: %3; }")
            .arg(theme::kPanelDeep.name(), theme::kBorder.name(), theme::kText.name()));
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { rebuildList(); });
    root->addWidget(search_);

    // format chips
    auto* chips = new QHBoxLayout();
    chips->setSpacing(6);
    auto makeChip = [&](const QString& text, bool active, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setCheckable(active);
        b->setChecked(active);
        b->setEnabled(active);
        b->setToolTip(tip);
        b->setCursor(active ? Qt::PointingHandCursor : Qt::ArrowCursor);
        b->setStyleSheet(
            QString("QPushButton { background-color: %1; color: %2; border-radius: 6px;"
                    " padding: 5px 12px; font-size: 11px; border: 1px solid %3; }"
                    "QPushButton:disabled { color: %4; }")
                .arg(active ? theme::kAccent.name() : theme::kPanelAlt.name(),
                     active ? QColor(255, 255, 255).name() : theme::kTextDim.name(),
                     active ? theme::kAccent.name() : theme::kBorder.name(),
                     theme::kTextFaint.name()));
        return b;
    };
    auto* lv2Chip = makeChip(tr("LV2 (Linux)"), true, tr("Show LV2 plugins discovered via Lilv"));
    auto* vst3Chip = makeChip(tr("VST3 (Windows)"), false,
                              tr("VST3 hosting arrives with the Windows milestone (FR-12)"));
    chips->addWidget(lv2Chip);
    chips->addWidget(vst3Chip);
    chips->addStretch(1);
    root->addLayout(chips);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(
        QString("color: %1; font-size: 10px;").arg(theme::kTextFaint.name()));
    root->addWidget(countLabel_);

    list_ = new QListWidget(this);
    list_->setObjectName("pluginList");
    list_->setStyleSheet(
        QString("QListWidget { background-color: %1; border: 1px solid %2; border-radius: 8px;"
                " }"
                "QListWidget::item { border-bottom: 1px solid %3; }"
                "QListWidget::item:selected { background: %4; }")
            .arg(theme::kPanelDeep.name(), theme::kBorder.name(), theme::kBorderSoft.name(),
                 theme::kAccent.darker(160).name()));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(list_, 1);

    emptyLabel_ = new QLabel(tr("Scanning plugins..."), this);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextFaint.name()));
    root->addWidget(emptyLabel_, 1);

    connect(controller_, &SessionController::catalogUpdated, this,
            &PluginBrowser::onCatalogUpdated);
    refresh();
}

void PluginBrowser::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

void PluginBrowser::onCatalogUpdated() { rebuildList(); }

void PluginBrowser::refresh() { rebuildList(); }

bool PluginBrowser::matchesFilter(const PluginDescriptor& d) const {
    const QString text = search_->text().trimmed();
    if (text.isEmpty()) return true;
    return QString::fromStdString(d.name).contains(text, Qt::CaseInsensitive) ||
           QString::fromStdString(d.uri).contains(text, Qt::CaseInsensitive) ||
           QString::fromStdString(d.category).contains(text, Qt::CaseInsensitive) ||
           QString::fromStdString(d.author).contains(text, Qt::CaseInsensitive);
}

void PluginBrowser::rebuildList() {
    list_->setUpdatesEnabled(false);
    list_->clear();

    const QVector<PluginDescriptor> all = controller_->catalog();
    int shown = 0;
    for (const PluginDescriptor& d : all) {
        if (d.key.format != PluginFormat::Lv2) continue;
        if (!matchesFilter(d)) continue;

        auto* item = new QListWidgetItem(list_);
        item->setSizeHint(QSize(0, 46));
        const PluginKey key = d.key;
        auto* row = new PluginRow(d, [this, key] { emit pluginAddRequested(key); });
        list_->setItemWidget(item, row);
        item->setToolTip(QString::fromStdString(d.uri) +
                         (d.warning.empty()
                              ? QString()
                              : QString("\n") + QString::fromStdString(d.warning)));
        ++shown;
    }
    list_->setUpdatesEnabled(true);

    const bool scanning = controller_->catalogBusy();
    emptyLabel_->setVisible(shown == 0);
    if (shown == 0) {
        emptyLabel_->setText(scanning ? tr("Scanning plugins...")
                                      : tr("No plugin matches your search."));
    }
    countLabel_->setText(scanning ? tr("Scanning...") : tr("%1 plugin(s)").arg(shown));
}

}  // namespace lspa
