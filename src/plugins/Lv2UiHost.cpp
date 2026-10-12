// Lv2UiHost.cpp - native LV2 editor lifecycle via Suil (FR-06).
//
// Toolkit honesty note: most LV2 X11UI plugins (including the whole LSP
// suite) manage their own X11 connection and windows; Qt6 never routes X
// events for foreign windows. Embedding therefore works as follows:
//   1. We hand the container's X window to the UI as ui:parent and let the
//      UI create a child window itself (best case, mode "embedded").
//   2. If the UI created a top-level window anyway, we XReparentWindow() it
//      into the container (mode "reparented").
//   3. If the plugin UI type cannot be hosted in an X11 container at all, or
//      instantiation fails, we fall back to a standalone Xlib window
//      (mode "standalone") and, as a last resort, report a clear error so
//      the app can show its generic parameter panel.
// The UI's ui:idleInterface (declared by LSP and others) is pumped at ~60 Hz
// so the plugin can process its own X events. This is best effort by design:
// PRD section 4 explicitly does not promise universal UI embedding.
//
// This file is the only place in the adapter that includes Qt headers; the
// public header stays Qt-free (QWidget* enters as void*).

#include "plugins/Lv2UiHost.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Qt headers must come before X11: Xlib defines Bool/Status/None macros that
// break Qt's headers (#error "qmetatype.h must be included before ... Bool").
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <X11/Xlib.h>
#include <lilv/lilv.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/ui/ui.h>
#include <suil/suil.h>

#include "core/Logger.h"
#include "plugins/LilvLock.h"
#include "plugins/Lv2Instance.h"
#include "plugins/UridMap.h"

namespace lspa {
namespace {

// XInitThreads() must run before any Xlib call in the process. Qt6 talks to
// X11 through xcb (not Xlib), so we are effectively the only Xlib user, but
// we still guard our own Display with Editor::xLock because openEditor(),
// the pump and closeEditor() may come from different threads.
std::once_flag g_xOnce;
void ensureXThreads() { std::call_once(g_xOnce, [] { XInitThreads(); }); }

// ---------------------------------------------------------------------------
// Suil host callbacks (controller is the Lv2Instance)
// ---------------------------------------------------------------------------

void suilWrite(SuilController controller, uint32_t portIndex, uint32_t bufferSize,
               uint32_t protocol, const void* buffer) {
    // Protocol 0 == ui:floatProtocol == lv2:ControlPort input. Anything else
    // (atom transfer etc.) is ignored, as the LV2 UI spec requires hosts to
    // silently drop protocols they do not understand.
    if (protocol != 0 || bufferSize != sizeof(float) || buffer == nullptr) return;
    auto* inst = static_cast<Lv2Instance*>(controller);
    inst->setPortValueFromUi(static_cast<int>(portIndex),
                             *static_cast<const float*>(buffer));
}

uint32_t suilPortIndex(SuilController controller, const char* symbol) {
    auto* inst = static_cast<Lv2Instance*>(controller);
    const int idx = inst->portIndexBySymbol(symbol);
    return idx < 0 ? LV2UI_INVALID_PORT_INDEX : static_cast<uint32_t>(idx);
}

uint32_t suilSubscribe(SuilController, uint32_t, uint32_t, const LV2_Feature* const*) {
    return 0;  // success: we forward all control-port changes anyway
}

uint32_t suilUnsubscribe(SuilController, uint32_t, uint32_t, const LV2_Feature* const*) {
    return 0;
}

// ---------------------------------------------------------------------------
// Editor bookkeeping
// ---------------------------------------------------------------------------

struct Editor {
    IPluginInstance* instance = nullptr;
    Lv2Instance* lv2 = nullptr;

    Display* dpy = nullptr;
    std::mutex xLock;  // serializes all use of `dpy`

    QWidget* container = nullptr;  // caller-owned Qt container (not deleted)
    Window parentXid = 0;          // X window handed to the plugin as ui:parent
    Window standaloneXid = 0;      // our own standalone window (destroyed here)
    Window widgetXid = 0;          // suil_instance_get_widget() reinterpreted
    bool standaloneRequested = false;
    std::string mode;  // "embedded" | "reparented" | "standalone"

    SuilHost* suilHost = nullptr;
    SuilInstance* suil = nullptr;

    // Feature storage; addresses must stay valid while `suil` lives.
    std::string windowTitle;
    LV2UI_Resize resizeFeature{};
    LV2_Feature fMap{};
    LV2_Feature fUnmap{};
    LV2_Feature fParent{};
    LV2_Feature fResize{};
    LV2_Feature fIdle{};
    LV2_Feature fTitle{};
    LV2_Feature fInstanceAccess{};
    const LV2_Feature* featureList[10] = {};

    const LV2UI_Idle_Interface* idle = nullptr;
    const LV2UI_Show_Interface* showIface = nullptr;

    QTimer* timer = nullptr;       // pump when a Qt event loop exists
    std::thread pumpThread;        // pump without Qt (lv2probe)
    std::atomic<bool> pumpRun{false};

    // ui:resize request from the plugin (called on the pump thread).
    static int resizeRequest(LV2UI_Feature_Handle handle, int width, int height);
    void applyResize(int width, int height);
    void pumpOnce();
};

struct HostState {
    std::mutex mutex;
    std::unordered_map<const IPluginInstance*, Editor*> editors;
};

HostState& hostState() {
    static HostState s;
    return s;
}

int Editor::resizeRequest(LV2UI_Feature_Handle handle, int width, int height) {
    static_cast<Editor*>(handle)->applyResize(width, height);
    return 0;
}

void Editor::applyResize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (container != nullptr) {
        // Queued so the resize always runs on the Qt GUI thread, no matter
        // which thread the plugin UI calls us from.
        QWidget* c = container;
        QMetaObject::invokeMethod(
            c, [c, width, height] { c->resize(width, height); }, Qt::QueuedConnection);
    } else if (dpy != nullptr) {
        std::lock_guard<std::mutex> lock(xLock);
        XResizeWindow(dpy, parentXid != 0 ? parentXid : standaloneXid,
                      static_cast<unsigned>(width), static_cast<unsigned>(height));
    }
}

void Editor::pumpOnce() {
    // Drive the plugin's ui:idleInterface (required by LSP plugin UIs to
    // process their X events) and forward queued port updates.
    if (idle != nullptr && idle->idle != nullptr && suil != nullptr) {
        const int rc = idle->idle(suil_instance_get_handle(suil));
        if (rc != 0) {
            // UI says it has been closed: stop pumping, keep the instance
            // for closeEditor() (LV2 allows either destroying or re-showing).
            LSPA_LOG_INFO("Lv2UiHost: plugin UI requested close (idle returned %d)", rc);
            pumpRun.store(false, std::memory_order_release);
            if (timer != nullptr) timer->stop();
        }
    }
    if (suil == nullptr || lv2 == nullptr) return;
    int idx = 0;
    float value = 0.0f;
    while (lv2->popUiEvent(idx, value)) {
        suil_instance_port_event(suil, static_cast<uint32_t>(idx), sizeof(float), 0, &value);
    }
}

// ---------------------------------------------------------------------------
// UI discovery (shared with Lv2Host::canOpenEditor)
// ---------------------------------------------------------------------------

}  // namespace

namespace lv2ui {

bool pickUiForX11(void* worldV, const void* pluginV, UiChoice& out) {
    // lilv_plugin_get_uis() parses bundle data lazily; serialize with
    // Lv2Host::scan()/instantiate() (recursive: scan calls this too).
    LilvLock lock;
    auto* world = static_cast<LilvWorld*>(worldV);
    const auto* plugin = static_cast<const LilvPlugin*>(pluginV);
    if (world == nullptr || plugin == nullptr) return false;

    LilvUIs* uis = lilv_plugin_get_uis(plugin);
    if (uis == nullptr || lilv_uis_size(uis) == 0) {
        lilv_uis_free(uis);
        return false;
    }

    LilvNode* containerType = lilv_new_uri(world, LV2_UI__X11UI);
    bool found = false;
    for (LilvIter* it = lilv_uis_begin(uis); !lilv_uis_is_end(uis, it);
         it = lilv_uis_next(uis, it)) {
        const LilvUI* ui = lilv_uis_get(uis, it);
        const LilvNode* type = nullptr;
        if (lilv_ui_is_supported(ui, suil_ui_supported, containerType, &type) == 0) {
            continue;
        }
        const LilvNode* uiUri = lilv_ui_get_uri(ui);
        const LilvNode* bundle = lilv_ui_get_bundle_uri(ui);
        const LilvNode* binary = lilv_ui_get_binary_uri(ui);
        if (uiUri == nullptr || bundle == nullptr || binary == nullptr) continue;

        out.uiUri = lilv_node_as_uri(uiUri);
        out.typeUri = (type != nullptr) ? lilv_node_as_uri(type) : LV2_UI__X11UI;
        if (char* bp = lilv_file_uri_parse(lilv_node_as_uri(bundle), nullptr)) {
            out.bundlePath = bp;
            lilv_free(bp);
        }
        if (char* bnp = lilv_file_uri_parse(lilv_node_as_uri(binary), nullptr)) {
            out.binaryPath = bnp;
            lilv_free(bnp);
        }
        found = !out.bundlePath.empty() && !out.binaryPath.empty();
        if (found) break;
    }
    lilv_node_free(containerType);
    lilv_uis_free(uis);
    if (!found) out = UiChoice{};
    return found;
}

}  // namespace lv2ui

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

namespace {

// Resolve which X window to hand to the UI. Returns false + errorOut when no
// usable X display exists at all.
bool resolveParentWindow(Editor* e, void* parentQWidget, unsigned long parentXId,
                         std::string& errorOut) {
    ensureXThreads();
    e->dpy = XOpenDisplay(nullptr);
    if (e->dpy == nullptr) {
        const char* disp = std::getenv("DISPLAY");
        errorOut = std::string("cannot open X display (DISPLAY=") +
                   (disp != nullptr ? disp : "<unset>") + ")";
        return false;
    }

    if (parentQWidget != nullptr && QCoreApplication::instance() != nullptr) {
        QWidget* w = static_cast<QWidget*>(parentQWidget);
        const bool xcb = QGuiApplication::platformName() == QStringLiteral("xcb");
        if (xcb) {
            w->setAttribute(Qt::WA_NativeWindow);
            e->container = w;
            e->parentXid = static_cast<Window>(w->winId());
            return true;
        }
        LSPA_LOG_WARN("Lv2UiHost: Qt platform is not X11, cannot embed; using standalone window");
    } else if (parentXId != 0) {
        e->parentXid = static_cast<Window>(parentXId);
        return true;
    }

    // Standalone: create our own top-level X window as the embedding target.
    Window root = DefaultRootWindow(e->dpy);
    e->standaloneXid = XCreateSimpleWindow(e->dpy, root, 0, 0, 640, 480, 0,
                                           BlackPixel(e->dpy, 0), WhitePixel(e->dpy, 0));
    if (e->standaloneXid == 0) {
        errorOut = "failed to create standalone X window";
        return false;
    }
    e->standaloneRequested = true;
    e->parentXid = e->standaloneXid;
    XStoreName(e->dpy, e->standaloneXid, "Plugin editor");
    XMapRaised(e->dpy, e->standaloneXid);
    XFlush(e->dpy);
    return true;
}

// Children of `parent` (0 on X error). Locks Editor::xLock itself.
std::vector<Window> queryChildren(Editor* e, Window parent) {
    std::lock_guard<std::mutex> lock(e->xLock);
    Window rootRet = 0;
    Window parentRet = 0;
    Window* children = nullptr;
    unsigned n = 0;
    std::vector<Window> out;
    if (XQueryTree(e->dpy, parent, &rootRet, &parentRet, &children, &n) != 0) {
        if (children != nullptr) {
            out.assign(children, children + n);
            XFree(children);
        }
    }
    return out;
}

// Resize the embedding container (or our standalone window) to the size the
// plugin's X window reports. Locks Editor::xLock itself.
void resizeToWidget(Editor* e) {
    if (e->dpy == nullptr || e->widgetXid == 0) return;
    std::lock_guard<std::mutex> lock(e->xLock);
    XWindowAttributes attrs{};
    if (XGetWindowAttributes(e->dpy, e->widgetXid, &attrs) == 0) return;
    if (attrs.width <= 0 || attrs.height <= 0) return;
    if (e->container != nullptr) {
        QWidget* c = e->container;
        const int w = attrs.width;
        const int h = attrs.height;
        // Queued: may be called from the pump thread.
        QMetaObject::invokeMethod(
            c, [c, w, h] { c->resize(w, h); }, Qt::QueuedConnection);
    } else if (e->standaloneXid != 0) {
        XResizeWindow(e->dpy, e->standaloneXid, static_cast<unsigned>(attrs.width),
                      static_cast<unsigned>(attrs.height));
        XFlush(e->dpy);
    }
}

// Free every resource of a (possibly half-open) editor. Never throws.
void destroyEditor(Editor* e) {
    if (e->timer != nullptr) {
        QTimer* t = e->timer;
        e->timer = nullptr;
        if (t->thread() == QThread::currentThread()) {
            t->stop();
            delete t;
        } else {
            // closeEditor() came from another thread: let the timer's own
            // (GUI) thread stop and delete it. Deferred via the event loop
            // so we never block the audio or caller thread.
            QMetaObject::invokeMethod(
                t, [t] { t->stop(); delete t; }, Qt::QueuedConnection);
        }
    }
    e->pumpRun.store(false, std::memory_order_release);
    if (e->pumpThread.joinable()) {
        if (e->pumpThread.get_id() == std::this_thread::get_id()) {
            e->pumpThread.detach();  // defensive: closed from the pump itself
        } else {
            e->pumpThread.join();
        }
    }
    if (e->suil != nullptr) {
        suil_instance_free(e->suil);
        e->suil = nullptr;
    }
    if (e->suilHost != nullptr) {
        suil_host_free(e->suilHost);
        e->suilHost = nullptr;
    }
    if (e->dpy != nullptr) {
        if (e->standaloneXid != 0) {
            std::lock_guard<std::mutex> lock(e->xLock);
            XDestroyWindow(e->dpy, e->standaloneXid);
            XSync(e->dpy, False);
        }
        XCloseDisplay(e->dpy);
        e->dpy = nullptr;
    }
    delete e;
}

// Start the ~60 Hz pump: Qt timer when an event loop exists, otherwise a
// dedicated thread (lv2probe has no Qt application).
void startPump(Editor* e) {
    e->pumpRun.store(true, std::memory_order_release);
    if (QCoreApplication::instance() != nullptr) {
        e->timer = new QTimer();
        e->timer->setInterval(16);
        QObject::connect(e->timer, &QTimer::timeout, [e] { e->pumpOnce(); });
        if (e->timer->thread() != QCoreApplication::instance()->thread()) {
            e->timer->moveToThread(QCoreApplication::instance()->thread());
        }
        QMetaObject::invokeMethod(e->timer, static_cast<void (QTimer::*)()>(&QTimer::start),
                                  Qt::QueuedConnection);
    } else {
        e->pumpThread = std::thread([e] {
            while (e->pumpRun.load(std::memory_order_acquire)) {
                e->pumpOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
    }
}

// Instantiate the chosen UI into `parent`. Falls back from "embed with
// ui:parent" to "no parent" when the plugin refuses the container.
bool createSuilInstance(Editor* e, const lv2ui::UiChoice& ui, std::string& errorOut) {
    e->windowTitle = e->lv2->displayName();
    e->resizeFeature.handle = e;
    e->resizeFeature.ui_resize = &Editor::resizeRequest;

    LilvInstance* lilvInst = static_cast<LilvInstance*>(e->lv2->lilvInstance());
    (void)lilvInst;  // no data-access feature: see header note in this file

    const UridMap* urid = e->lv2->uridMap();
    e->fMap.URI = LV2_URID__map;
    e->fMap.data = const_cast<LV2_URID_Map*>(urid->mapFeature());
    e->fUnmap.URI = LV2_URID__unmap;
    e->fUnmap.data = const_cast<LV2_URID_Unmap*>(urid->unmapFeature());
    e->fParent.URI = LV2_UI__parent;
    // Spec: parent data has the same format as LV2UI_Widget, i.e. for X11UI
    // the Window ID *as the data value itself*, not a pointer to it.
    e->fParent.data = reinterpret_cast<void*>(static_cast<uintptr_t>(e->parentXid));
    e->fResize.URI = LV2_UI__resize;
    e->fResize.data = &e->resizeFeature;
    e->fIdle.URI = LV2_UI__idleInterface;
    e->fIdle.data = nullptr;  // presence signals support, as jalv does
    e->fTitle.URI = LV2_UI__windowTitle;
    e->fTitle.data = const_cast<char*>(e->windowTitle.c_str());

    int n = 0;
    e->featureList[n++] = &e->fMap;
    e->featureList[n++] = &e->fUnmap;
    e->featureList[n++] = &e->fParent;
    e->featureList[n++] = &e->fResize;
    e->featureList[n++] = &e->fIdle;
    e->featureList[n++] = &e->fTitle;
    if (e->lv2->lv2Handle() != nullptr) {
        e->fInstanceAccess.URI = LV2_INSTANCE_ACCESS_URI;
        e->fInstanceAccess.data = e->lv2->lv2Handle();
        e->featureList[n++] = &e->fInstanceAccess;
    }
    // Note: lv2:data-access is deliberately not offered. Its callback
    // signature carries no context with which to route back to the owning
    // instance, and the only plugins requiring it (calf) are rejected at
    // scan time as unsupported anyway.
    e->featureList[n] = nullptr;

    e->suilHost = suil_host_new(&suilWrite, &suilPortIndex, &suilSubscribe, &suilUnsubscribe);
    if (e->suilHost == nullptr) {
        errorOut = "suil_host_new failed";
        return false;
    }

    const std::string pluginUri = e->lv2->key().id;
    e->suil = suil_instance_new(
        e->suilHost, e->lv2, LV2_UI__X11UI, pluginUri.c_str(), ui.uiUri.c_str(),
        ui.typeUri.c_str(), ui.bundlePath.c_str(), ui.binaryPath.c_str(), e->featureList);
    if (e->suil == nullptr && e->container != nullptr) {
        // Embedding attempt failed; retry without ui:parent so the UI opens
        // its own top-level window (standalone fallback).
        LSPA_LOG_WARN("Lv2UiHost: embedding failed for %s, retrying standalone",
                      pluginUri.c_str());
        std::vector<const LV2_Feature*> feats;
        for (int i = 0; e->featureList[i] != nullptr; ++i) {
            if (e->featureList[i] != &e->fParent) feats.push_back(e->featureList[i]);
        }
        feats.push_back(nullptr);
        e->suil = suil_instance_new(e->suilHost, e->lv2, LV2_UI__X11UI, pluginUri.c_str(),
                                    ui.uiUri.c_str(), ui.typeUri.c_str(), ui.bundlePath.c_str(),
                                    ui.binaryPath.c_str(), feats.data());
    }
    if (e->suil == nullptr) {
        errorOut = "failed to instantiate the native plugin UI (Suil returned no instance)";
        return false;
    }

    e->idle = static_cast<const LV2UI_Idle_Interface*>(
        suil_instance_extension_data(e->suil, LV2_UI__idleInterface));
    e->showIface = static_cast<const LV2UI_Show_Interface*>(
        suil_instance_extension_data(e->suil, LV2_UI__showInterface));
    return true;
}

// Decide how (or whether) the UI actually landed on screen. May pump the UI
// briefly because some plugins only create their window on first idle().
bool settleWidget(Editor* e, std::string& errorOut) {
    auto widgetAsXid = [e]() -> Window {
        void* w = suil_instance_get_widget(e->suil);
        return static_cast<Window>(reinterpret_cast<uintptr_t>(w));
    };

    // Give the UI a few pump rounds to materialize its window if needed.
    for (int attempt = 0; attempt < 50; ++attempt) {
        e->widgetXid = widgetAsXid();
        std::vector<Window> kids = queryChildren(e, e->parentXid);
        if (e->widgetXid != 0 || !kids.empty()) break;
        e->pumpOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (e->widgetXid == 0) {
        if (e->showIface != nullptr && e->showIface->show != nullptr) {
            const int rc = e->showIface->show(suil_instance_get_handle(e->suil));
            if (rc == 0) {
                e->mode = "standalone";
                return true;
            }
            errorOut = "plugin UI show() failed";
            return false;
        }
        std::vector<Window> kids = queryChildren(e, e->parentXid);
        if (!kids.empty()) {  // window exists but no widget pointer was exposed
            e->mode = e->standaloneRequested ? "standalone" : "reparented";
            return true;
        }
        errorOut = "plugin UI created no visible window";
        return false;
    }

    if (e->standaloneRequested) {
        // We created the top-level window ourselves: always report standalone,
        // nest the UI inside it when it did not already do so.
        if (e->widgetXid != e->parentXid) {
            std::lock_guard<std::mutex> lock(e->xLock);
            Window rootRet = 0;
            Window parentRet = 0;
            Window* kids = nullptr;
            unsigned nKids = 0;
            if (XQueryTree(e->dpy, e->widgetXid, &rootRet, &parentRet, &kids, &nKids) != 0) {
                if (kids != nullptr) XFree(kids);
                if (parentRet != e->parentXid) {
                    XReparentWindow(e->dpy, e->widgetXid, e->parentXid, 0, 0);
                    XMapWindow(e->dpy, e->widgetXid);
                    XFlush(e->dpy);
                }
            }
        }
        e->mode = "standalone";
        return true;
    }

    // Embedding was requested. Is the widget already inside our container?
    if (e->widgetXid == e->parentXid) {
        e->mode = "embedded";
        return true;
    }
    bool needResize = false;
    {
        std::lock_guard<std::mutex> lock(e->xLock);
        Window rootRet = 0;
        Window parentRet = 0;
        Window* kids = nullptr;
        unsigned nKids = 0;
        if (XQueryTree(e->dpy, e->widgetXid, &rootRet, &parentRet, &kids, &nKids) != 0) {
            if (kids != nullptr) XFree(kids);
            if (parentRet == e->parentXid) {
                e->mode = "embedded";  // UI honored ui:parent and made a child
                XMapWindow(e->dpy, e->widgetXid);
                XFlush(e->dpy);
                needResize = true;
            } else {
                // UI ignored ui:parent (its window is a top-level): reparent it.
                XReparentWindow(e->dpy, e->widgetXid, e->parentXid, 0, 0);
                XMapWindow(e->dpy, e->widgetXid);
                XFlush(e->dpy);
                e->mode = "reparented";
                needResize = true;
            }
        } else {
            e->mode = "reparented";
        }
    }

    // Size the Qt container to the plugin's window when we know it.
    if (needResize) resizeToWidget(e);
    return true;
}

}  // namespace

Lv2UiHost& Lv2UiHost::instance() {
    static Lv2UiHost g;
    return g;
}

bool Lv2UiHost::openEditor(IPluginInstance* instance, void* parentQWidget,
                           std::string& errorOut) {
    return openInternal(instance, parentQWidget, 0, errorOut);
}

bool Lv2UiHost::openEditorInX11(IPluginInstance* instance, unsigned long parentXId,
                                std::string& errorOut) {
    return openInternal(instance, nullptr, parentXId, errorOut);
}

bool Lv2UiHost::openInternal(IPluginInstance* instance, void* parentQWidget,
                             unsigned long parentXId, std::string& errorOut) {
    errorOut.clear();
    if (instance == nullptr) {
        errorOut = "no plugin instance";
        return false;
    }
    if (!instance->isOk()) {
        errorOut = "plugin instance is not in a usable state";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(hostState().mutex);
        if (hostState().editors.count(instance) != 0) {
            errorOut = "an editor is already open for this plugin";
            return false;
        }
    }

    auto* lv2 = dynamic_cast<Lv2Instance*>(instance);
    if (lv2 == nullptr) {
        errorOut = "instance is not an LV2 plugin";
        return false;
    }

    Editor* e = new Editor();
    e->instance = instance;
    e->lv2 = lv2;

    bool ok = false;
    try {
        lv2ui::UiChoice ui;
        if (!lv2ui::pickUiForX11(lv2->lilvWorld(), lv2->lilvPlugin(), ui)) {
            errorOut = "plugin has no UI that this host can open (only X11UI types are supported)";
        } else if (!resolveParentWindow(e, parentQWidget, parentXId, errorOut)) {
            // errorOut already set
        } else if (!createSuilInstance(e, ui, errorOut)) {
            // errorOut already set
        } else if (!settleWidget(e, errorOut)) {
            // errorOut already set
        } else {
            // Publish before pumping so other threads can close it safely.
            {
                std::lock_guard<std::mutex> lock(hostState().mutex);
                hostState().editors[instance] = e;
            }
            // Initial sync: push every current control value to the editor.
            for (const auto& cv : lv2->controlSnapshot()) {
                const float v = cv.second;
                suil_instance_port_event(e->suil, static_cast<uint32_t>(cv.first),
                                         sizeof(float), 0, &v);
            }
            startPump(e);
            ok = true;
            LSPA_LOG_INFO("Lv2UiHost: opened editor for %s (%s)", lv2->key().id.c_str(),
                          e->mode.c_str());
        }
    } catch (const std::exception& ex) {
        errorOut = std::string("exception while opening editor: ") + ex.what();
        ok = false;
    } catch (...) {
        errorOut = "unknown exception while opening editor";
        ok = false;
    }

    if (!ok) {
        destroyEditor(e);  // never leave a half-open editor behind
        return false;
    }
    return true;
}

void Lv2UiHost::closeEditor(IPluginInstance* instance) {
    if (instance == nullptr) return;
    Editor* e = nullptr;
    {
        std::lock_guard<std::mutex> lock(hostState().mutex);
        auto it = hostState().editors.find(instance);
        if (it == hostState().editors.end()) return;
        e = it->second;
        hostState().editors.erase(it);  // publish removal first: no new users
    }
    LSPA_LOG_INFO("Lv2UiHost: closing editor for %s", e->lv2->key().id.c_str());
    destroyEditor(e);
}

bool Lv2UiHost::isEditorOpen(const IPluginInstance* instance) const {
    std::lock_guard<std::mutex> lock(hostState().mutex);
    return hostState().editors.count(instance) != 0;
}

bool Lv2UiHost::hasEditor(const IPluginInstance* instance) const {
    return instance != nullptr && instance->hasNativeUi();
}

int Lv2UiHost::openEditorCount() const {
    std::lock_guard<std::mutex> lock(hostState().mutex);
    return static_cast<int>(hostState().editors.size());
}

std::string Lv2UiHost::editorMode(const IPluginInstance* instance) const {
    std::lock_guard<std::mutex> lock(hostState().mutex);
    auto it = hostState().editors.find(instance);
    return it != hostState().editors.end() ? it->second->mode : std::string();
}

}  // namespace lspa
