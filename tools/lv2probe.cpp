// lv2probe.cpp - LV2 discovery / instancing / UI diagnostics (FR-05, FR-06).
//
// Modes:
//   lv2probe                       list every discovered plugin
//   lv2probe --filter SUBSTRING    list, filtered (name/label/category/URI)
//   lv2probe --selftest URI        instantiate + process a test signal and
//                                  verify the outputs (RT parameter writes)
//   lv2probe --uitest URI          open the native editor for ~3 s and report
//                                  embedded vs standalone (X11, no Qt)
//   lv2probe --stress URI [N]      N repeated instantiate/process/destroy
//                                  cycles (default N = 20)
//
// Exit status: 0 on success, non-zero on any failure. Plugin/host log output
// goes to stderr (core/Logger); this file only writes results to stdout.
#include <X11/Xlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "core/Logger.h"
#include "plugins/IPluginHost.h"
#include "plugins/Lv2Host.h"
#include "plugins/Lv2Instance.h"
#include "plugins/Lv2UiHost.h"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kSelftestBlocks = 128;
constexpr double kSineHz = 220.0;
constexpr float kSineAmp = 0.5f;

struct Args {
    std::string mode = "list";  // "list" | "selftest" | "uitest" | "stress"
    std::string uri;
    std::string filter;
    int stressCount = 20;
};

void printUsage() {
    std::printf(
        "usage: lv2probe [--filter SUBSTRING]\n"
        "       lv2probe --selftest URI\n"
        "       lv2probe --uitest URI\n"
        "       lv2probe --stress URI [COUNT]   (default COUNT = 20)\n");
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Resident set size in MiB (best effort, Linux); -1 when unavailable.
double readRssMiB() {
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) return -1.0;
    unsigned long total = 0;
    unsigned long resident = 0;
    const int n = std::fscanf(f, "%lu %lu", &total, &resident);
    std::fclose(f);
    if (n != 2) return -1.0;
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) /
           (1024.0 * 1024.0);
}

// Fill every input channel with a continuous 220 Hz sine (phase advances
// across blocks so the plugin sees a real signal, not a step).
void fillSine(std::vector<std::vector<float>>& bufs, double& phase) {
    const double inc = 2.0 * M_PI * kSineHz / kSampleRate;
    for (auto& b : bufs) {
        double p = phase;
        for (int i = 0; i < kBlock; ++i) {
            b[static_cast<size_t>(i)] = kSineAmp * static_cast<float>(std::sin(p));
            p += inc;
        }
    }
    phase += inc * kBlock;
}

lspa::PluginKey makeKey(const std::string& uri) {
    lspa::PluginKey k;
    k.format = lspa::PluginFormat::Lv2;
    k.id = uri;
    return k;
}

// ---------------------------------------------------------------------------
// Default mode: listing
// ---------------------------------------------------------------------------

int runList(lspa::Lv2Host& host, const std::string& filter) {
    const std::vector<lspa::PluginDescriptor> plugins = host.scan();
    size_t withUi = 0;
    size_t unsupported = 0;
    size_t shown = 0;
    size_t warningsShown = 0;
    const std::string needle = lower(filter);

    for (const auto& p : plugins) {
        if (p.hasUi) ++withUi;
        if (!p.warning.empty()) ++unsupported;
        if (!needle.empty()) {
            const std::string hay = lower(p.key.id + " " + p.name + " " + p.label + " " +
                                          p.category);
            if (hay.find(needle) == std::string::npos) continue;
        }
        if (shown == 0) {
            std::printf("%-44s %-12s %3s %3s  ui URI\n", "NAME", "CATEGORY", "IN", "OUT");
        }
        std::printf("%-44.44s %-12.12s %3d %3d  %d  %s\n", p.name.c_str(),
                    p.category.c_str(), p.audioInputs, p.audioOutputs, p.hasUi ? 1 : 0,
                    p.key.id.c_str());
        if (!p.warning.empty() && warningsShown < 20) {
            ++warningsShown;
            std::printf("    warning: %s\n", p.warning.c_str());
        }
        ++shown;
    }

    std::printf("Found %zu LV2 plugins (%zu with a native UI, %zu with scan warnings)\n",
                plugins.size(), withUi, unsupported);
    std::printf("shown=%zu\n", shown);
    return plugins.empty() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --selftest URI
// ---------------------------------------------------------------------------

int runSelftest(lspa::Lv2Host& host, const std::string& uri) {
    std::printf("selftest: %s (rate=%.0f, block=%d, %d blocks of %.0f Hz sine)\n", uri.c_str(),
                kSampleRate, kBlock, kSelftestBlocks, kSineHz);

    std::string err;
    std::unique_ptr<lspa::IPluginInstance> inst =
        host.instantiate(makeKey(uri), kSampleRate, kBlock, err);
    if (inst == nullptr) {
        std::printf("selftest: FAIL - instantiate: %s\n", err.c_str());
        return 1;
    }
    if (!inst->isOk()) {
        std::printf("selftest: FAIL - instance not ok: %s\n", inst->errorString().c_str());
        return 1;
    }

    int nIn = 0;
    int nOut = 0;
    if (const auto* lv2 = dynamic_cast<lspa::Lv2Instance*>(inst.get())) {
        nIn = lv2->numAudioInputs();
        nOut = lv2->numAudioOutputs();
    }
    std::printf("  name: %s\n  channels: %d in / %d out, latency=%d samples\n",
                inst->displayName().c_str(), nIn, nOut, inst->latencySamples());

    // Parameters: set every one to 1.5x default, clamped to its range, before
    // any processing (as required by the verification script).
    const std::vector<lspa::PluginParam> params = inst->parameters();
    int applied = 0;
    for (const lspa::PluginParam& p : params) {
        float v = p.def * 1.5f;
        if (v < p.min) v = p.min;
        if (v > p.max) v = p.max;
        inst->setParameter(p.symbol, v);
        ++applied;
    }
    std::printf("  parameters: %zu total, %d set to 1.5x default (clamped)\n", params.size(),
                applied);

    // Buffers: separate input/output arrays (in != out) like the engine does.
    std::vector<std::vector<float>> inBuf(static_cast<size_t>(std::max(nIn, 0)),
                                          std::vector<float>(kBlock, 0.0f));
    std::vector<std::vector<float>> outBuf(static_cast<size_t>(std::max(nOut, 0)),
                                           std::vector<float>(kBlock, 0.0f));
    std::vector<float*> inPtr;
    std::vector<float*> outPtr;
    for (auto& b : inBuf) inPtr.push_back(b.data());
    for (auto& b : outBuf) outPtr.push_back(b.data());

    double phase = 0.0;
    fillSine(inBuf, phase);

    struct Stat {
        float peak = 0.0f;
        double sumSq = 0.0;
        long nonFinite = 0;
        long finiteSamples = 0;
        bool differs = false;
    };
    std::vector<Stat> stats(static_cast<size_t>(std::max(nOut, 0)));

    inst->activate();

    long paramMismatches = 0;
    std::vector<std::string> mismatchDetail;
    for (int block = 0; block < kSelftestBlocks; ++block) {
        fillSine(inBuf, phase);
        inst->process(inPtr.empty() ? nullptr : inPtr.data(), nIn,
                      outPtr.empty() ? nullptr : outPtr.data(), nOut, kBlock);

        // Real-time parameter writes *during* processing, plus a read-back
        // check that the value stayed inside the advertised range.
        if (!params.empty()) {
            const lspa::PluginParam& p =
                params[static_cast<size_t>(block) % params.size()];
            const float v = (block & 1) != 0 ? p.max : p.min;
            inst->setParameter(p.symbol, v);
            const float got = inst->getParameter(p.symbol);
            const float eps = 1e-3f * std::max(1.0f, std::fabs(p.max - p.min));
            if (got < p.min - eps || got > p.max + eps) {
                ++paramMismatches;
                if (mismatchDetail.size() < 5) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf),
                                  "%s: got=%g range=[%g,%g]%s%s", p.symbol.c_str(),
                                  static_cast<double>(got), static_cast<double>(p.min),
                                  static_cast<double>(p.max), p.isInteger ? " int" : "",
                                  p.isToggled ? " toggle" : "");
                    mismatchDetail.emplace_back(buf);
                }
            }
        }

        for (int c = 0; c < nOut; ++c) {
            Stat& s = stats[static_cast<size_t>(c)];
            const float* o = outBuf[static_cast<size_t>(c)].data();
            const float* ref = (c < nIn) ? inBuf[static_cast<size_t>(c)].data() : nullptr;
            for (int i = 0; i < kBlock; ++i) {
                const float v = o[i];
                if (!std::isfinite(v)) {
                    ++s.nonFinite;
                    continue;
                }
                ++s.finiteSamples;
                s.peak = std::max(s.peak, std::fabs(v));
                s.sumSq += static_cast<double>(v) * static_cast<double>(v);
                const float r = ref != nullptr ? ref[i] : 0.0f;
                if (std::fabs(v - r) > 1e-6f) s.differs = true;
            }
        }
    }
    inst->deactivate();

    // --- Report ------------------------------------------------------------
    bool allFinite = true;
    bool anySignal = false;
    for (int c = 0; c < nOut; ++c) {
        const Stat& s = stats[static_cast<size_t>(c)];
        const double rms =
            s.finiteSamples > 0
                ? std::sqrt(s.sumSq / static_cast<double>(s.finiteSamples))
                : 0.0;
        const bool finite = s.nonFinite == 0;
        allFinite = allFinite && finite;
        anySignal = anySignal || s.peak > 0.0f;
        std::printf(
            "  out[%d]: peak=%+.5f rms=%.5f finite=%s differs=%s\n", c, static_cast<double>(s.peak),
            rms, finite ? "yes" : "NO", s.differs ? "yes" : "no");
    }
    if (nOut == 0) std::printf("  (plugin has no audio output ports)\n");

    std::printf("  rt setParameter/getParameter range violations: %ld\n", paramMismatches);
    for (const std::string& d : mismatchDetail) std::printf("    %s\n", d.c_str());
    if (inst->latencySamples() < 0) {
        std::printf("  warning: negative latency reported\n");
    }
    if (nOut > 0 && !anySignal) {
        std::printf(
            "  note: outputs are all silence (legitimate for a gated/muted plugin, "
            "inspect peak/rms above)\n");
    }
    if (paramMismatches > 0) {
        std::printf("selftest: FAIL - parameter read-back out of range\n");
        return 1;
    }
    if (!allFinite) {
        std::printf("selftest: FAIL - non-finite samples in the output\n");
        return 1;
    }
    if (!inst->isOk()) {
        std::printf("selftest: FAIL - instance error: %s\n", inst->errorString().c_str());
        return 1;
    }
    std::printf("selftest: PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// --stress URI N
// ---------------------------------------------------------------------------

int runStress(lspa::Lv2Host& host, const std::string& uri, int iterations) {
    std::printf("stress: %s x %d iterations (instantiate/activate/process/destroy)\n",
                uri.c_str(), iterations);
    const double rssBefore = readRssMiB();
    const auto t0 = std::chrono::steady_clock::now();

    int ok = 0;
    int failed = 0;
    std::vector<std::vector<float>> inBuf(2, std::vector<float>(kBlock, 0.0f));
    std::vector<std::vector<float>> outBuf(2, std::vector<float>(kBlock, 0.0f));
    std::vector<float*> inPtr = {inBuf[0].data(), inBuf[1].data()};
    std::vector<float*> outPtr = {outBuf[0].data(), outBuf[1].data()};
    double phase = 0.0;

    for (int i = 0; i < iterations; ++i) {
        std::string err;
        std::unique_ptr<lspa::IPluginInstance> inst =
            host.instantiate(makeKey(uri), kSampleRate, kBlock, err);
        if (inst == nullptr || !inst->isOk()) {
            ++failed;
            std::printf("  iter %3d: FAIL - %s\n", i + 1,
                        inst == nullptr ? err.c_str() : inst->errorString().c_str());
            continue;
        }
        const std::vector<lspa::PluginParam> params = inst->parameters();
        for (const lspa::PluginParam& p : params) {
            float v = p.def * 1.5f;
            if (v < p.min) v = p.min;
            if (v > p.max) v = p.max;
            inst->setParameter(p.symbol, v);
        }
        inst->activate();
        for (int b = 0; b < 16; ++b) {
            fillSine(inBuf, phase);
            inst->process(inPtr.data(), 2, outPtr.data(), 2, kBlock);
            if (!params.empty()) {
                const lspa::PluginParam& p =
                    params[static_cast<size_t>(b) % params.size()];
                inst->setParameter(p.symbol, (b & 1) != 0 ? p.max : p.min);
            }
        }
        inst->deactivate();
        inst.reset();
        ++ok;
        if ((i + 1) % 5 == 0) {
            std::printf("  iter %3d: ok (rss=%.1f MiB)\n", i + 1, readRssMiB());
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    const double rssAfter = readRssMiB();
    std::printf("stress: ok=%d failed=%d elapsed=%ld ms rss %.1f -> %.1f MiB\n", ok, failed,
                static_cast<long>(ms), rssBefore, rssAfter);
    if (failed > 0 || ok == 0) {
        std::printf("stress: FAIL\n");
        return 1;
    }
    std::printf("stress: PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// --uitest URI
// ---------------------------------------------------------------------------

// Currently mapped top-level windows on the server (for "did a new window
// appear?" comparisons).
std::set<Window> mappedTopLevels(Display* dpy) {
    std::set<Window> out;
    Window root = 0;
    Window parent = 0;
    Window* children = nullptr;
    unsigned n = 0;
    if (XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &n) == 0) return out;
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes attrs{};
        if (XGetWindowAttributes(dpy, children[i], &attrs) != 0 &&
            attrs.map_state == IsViewable) {
            out.insert(children[i]);
        }
    }
    if (children != nullptr) XFree(children);
    return out;
}

// Mapped children of `parent` (i.e. windows the plugin UI created inside our
// container).
std::vector<Window> mappedChildren(Display* dpy, Window parent) {
    std::vector<Window> out;
    Window root = 0;
    Window pc = 0;
    Window* children = nullptr;
    unsigned n = 0;
    if (XQueryTree(dpy, parent, &root, &pc, &children, &n) == 0) return out;
    for (unsigned i = 0; i < n; ++i) {
        XWindowAttributes attrs{};
        if (XGetWindowAttributes(dpy, children[i], &attrs) != 0 &&
            attrs.map_state == IsViewable) {
            out.push_back(children[i]);
        }
    }
    if (children != nullptr) XFree(children);
    return out;
}

int runUiTest(lspa::Lv2Host& host, const std::string& uri) {
    std::printf("uitest: %s (3 s under DISPLAY=%s)\n", uri.c_str(),
                std::getenv("DISPLAY") != nullptr ? std::getenv("DISPLAY") : "<unset>");
    if (std::getenv("DISPLAY") == nullptr) {
        // Sensible default for headless CI boxes running Xvfb on :1.
        setenv("DISPLAY", ":1", 0);
        std::printf("uitest: DISPLAY unset, defaulting to :1\n");
    }

    std::string err;
    std::unique_ptr<lspa::IPluginInstance> inst =
        host.instantiate(makeKey(uri), kSampleRate, kBlock, err);
    if (inst == nullptr || !inst->isOk()) {
        std::printf("uitest: FAIL - instantiate: %s\n",
                    inst == nullptr ? err.c_str() : inst->errorString().c_str());
        return 1;
    }
    if (!host.canOpenEditor(inst.get())) {
        std::printf("uitest: FAIL - host reports no openable native editor\n");
        return 1;
    }

    XInitThreads();
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        std::printf("uitest: FAIL - cannot open X display for the container window\n");
        return 1;
    }
    const Window container =
        XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 800, 600, 0,
                            BlackPixel(dpy, 0), WhitePixel(dpy, 0));
    XStoreName(dpy, container, "lv2probe uitest container");
    XMapRaised(dpy, container);
    XSync(dpy, False);

    const std::set<Window> before = mappedTopLevels(dpy);

    lspa::Lv2UiHost& uiHost = lspa::Lv2UiHost::instance();
    const bool opened = uiHost.openEditorInX11(inst.get(), container, err);
    if (!opened) {
        std::printf("uitest: FAIL - openEditor: %s\n", err.c_str());
        XDestroyWindow(dpy, container);
        XCloseDisplay(dpy);
        return 1;
    }

    // The host pumps the UI itself (dedicated thread, no Qt event loop here).
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    const std::string mode = uiHost.editorMode(inst.get());
    const std::vector<Window> kids = mappedChildren(dpy, container);
    const std::set<Window> after = mappedTopLevels(dpy);
    std::set<Window> appeared;
    std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                        std::inserter(appeared, appeared.end()));

    const bool embedded = (mode == "embedded" || mode == "reparented") && !kids.empty();
    const bool standalone = !appeared.empty();

    std::printf("uitest: mode=%s container-children=%zu new-toplevels=%zu\n", mode.c_str(),
                kids.size(), appeared.size());
    std::printf("uitest: result=%s (%s)\n", embedded ? "embedded"
                                                     : (standalone ? "standalone" : "not visible"),
                embedded ? "plugin window is inside the container"
                         : (standalone ? "plugin opened its own window"
                                       : "no visible plugin window found"));

    uiHost.closeEditor(inst.get());
    const int stillOpen = uiHost.openEditorCount();

    XDestroyWindow(dpy, container);
    XSync(dpy, False);
    XCloseDisplay(dpy);
    inst.reset();

    if (stillOpen != 0) {
        std::printf("uitest: FAIL - %d editor(s) still open after closeEditor\n", stillOpen);
        return 1;
    }
    if (!embedded && !standalone) {
        std::printf("uitest: FAIL - the editor opened but nothing became visible\n");
        return 1;
    }
    std::printf("uitest: PASS\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Host diagnostics (scan warnings etc.) belong on stderr so the listing
    // stays machine-readable.
    lspa::Logger::instance().setLevel(lspa::LogLevel::Warning);

    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            printUsage();
            return 0;
        } else if (a == "--filter" && i + 1 < argc) {
            args.filter = argv[++i];
        } else if (a == "--selftest" && i + 1 < argc) {
            args.mode = "selftest";
            args.uri = argv[++i];
        } else if (a == "--uitest" && i + 1 < argc) {
            args.mode = "uitest";
            args.uri = argv[++i];
        } else if (a == "--stress" && i + 1 < argc) {
            args.mode = "stress";
            args.uri = argv[++i];
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0]))) {
                args.stressCount = std::atoi(argv[++i]);
            }
        } else if (a == "-v") {
            lspa::Logger::instance().setLevel(lspa::LogLevel::Debug);
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            printUsage();
            return 2;
        }
    }
    if (args.uri.empty() && args.mode != "list") {
        printUsage();
        return 2;
    }
    if (args.stressCount <= 0) {
        std::fprintf(stderr, "stress count must be positive\n");
        return 2;
    }

    lspa::Lv2Host host;  // the Lilv world loads lazily on first use

    if (args.mode == "list") return runList(host, args.filter);
    if (args.mode == "selftest") return runSelftest(host, args.uri);
    if (args.mode == "uitest") return runUiTest(host, args.uri);
    if (args.mode == "stress") return runStress(host, args.uri, args.stressCount);
    printUsage();
    return 2;
}
