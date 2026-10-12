// PluginBrowser.h - searchable catalog of installed plugins.

#pragma once

#include <QWidget>

#include "plugins/IPluginHost.h"

class QLineEdit;
class QListWidget;
class QLabel;

namespace lspa {

class SessionController;

class PluginBrowser : public QWidget {
    Q_OBJECT
public:
    explicit PluginBrowser(SessionController* controller, QWidget* parent = nullptr);

    void refresh();
    void focusSearch();

signals:
    void pluginAddRequested(const lspa::PluginKey& key);

private slots:
    void onCatalogUpdated();

private:
    void rebuildList();
    bool matchesFilter(const PluginDescriptor& d) const;

    SessionController* controller_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
};

}  // namespace lspa
