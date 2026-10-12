// Lv2UiHost.h - native LV2 editor lifecycle (Suil), FR-06.
//
// Editors may be embedded into the given parent widget (X11) or opened in a
// standalone window; embedding failure must fall back, never crash.
//
// This header stays free of Qt and lilv includes: QWidget* is passed as
// void*, LilvWorld/LilvPlugin/LilvUI as void*/const void*.
#pragma once

#include <string>

#include "plugins/IPluginHost.h"

namespace lspa {

// Helpers shared between Lv2Host (canOpenEditor) and Lv2UiHost (openEditor).
namespace lv2ui {

// Everything openEditor needs to hand a UI to Suil (plain strings so no
// lilv-owned pointers escape this function).
struct UiChoice {
    std::string uiUri;       // the chosen UI's URI
    std::string typeUri;     // its RDF class, e.g. lv2:...ui#X11UI
    std::string bundlePath;  // filesystem path of the UI bundle
    std::string binaryPath;  // filesystem path of the UI binary
};

// Pick the first UI of `plugin` that Suil can host inside an X11 container
// (suil_ui_supported(LV2_UI__X11UI, uiType) > 0).
//  world:  LilvWorld* (borrowed)
//  plugin: const LilvPlugin* (borrowed)
// Returns true and fills `out` on success; false when no UI is compatible.
bool pickUiForX11(void* world, const void* plugin, UiChoice& out);

}  // namespace lv2ui

class Lv2UiHost {
public:
    static Lv2UiHost& instance();

    // parentQWidget: QWidget* of the embedding container (may be nullptr to
    // request a standalone window). Returns true if an editor is visible.
    // errorOut receives a user-readable reason on failure.
    //
    // Must be called from the Qt main thread when a QWidget is given (the
    // editor is pumped by a QTimer on that thread).
    bool openEditor(IPluginInstance* instance, void* parentQWidget, std::string& errorOut);

    // X11-native variant used by tools without Qt (lv2probe): embed into a
    // raw X window id. parentXId == 0 requests a standalone window. Falls
    // back to a standalone Xlib window when embedding fails.
    bool openEditorInX11(IPluginInstance* instance, unsigned long parentXId,
                         std::string& errorOut);

    void closeEditor(IPluginInstance* instance);
    bool isEditorOpen(const IPluginInstance* instance) const;
    bool hasEditor(const IPluginInstance* instance) const;

    // Number of currently open editors (used by leak/stability tests).
    int openEditorCount() const;

    // Diagnostics: "embedded", "standalone" or "reparented" while an editor
    // for `instance` is open, empty otherwise.
    std::string editorMode(const IPluginInstance* instance) const;

private:
    Lv2UiHost() = default;

    // Shared body of openEditor()/openEditorInX11().
    bool openInternal(IPluginInstance* instance, void* parentQWidget,
                      unsigned long parentXId, std::string& errorOut);
};

}  // namespace lspa
