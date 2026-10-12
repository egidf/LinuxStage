#include "core/Paths.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lspa {

namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string homeDir() {
    return envOr("HOME", "/tmp");
}

}  // namespace

std::string configDir() {
    return joinPath(envOr("XDG_CONFIG_HOME", joinPath(homeDir(), ".config")),
                    "lsp-audio-player");
}

std::string dataDir() {
    return joinPath(envOr("XDG_DATA_HOME", joinPath(homeDir(), ".local/share")),
                    "lsp-audio-player");
}

std::string defaultSessionPath() {
    return joinPath(configDir(), "session.json");
}

std::string defaultLogPath() {
    return joinPath(dataDir(), "lsp-audio-player.log");
}

bool ensureDir(const std::string& path) {
    if (path.empty()) return false;
    std::string acc;
    size_t i = 0;
    if (path[0] == '/') {
        acc = "/";
        i = 1;
    }
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        if (j > i) {
            acc += path.substr(i, j - i);
            if (::mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) {
                struct stat st{};
                if (::stat(acc.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
            }
        }
        if (j >= path.size()) break;
        acc += '/';
        i = j + 1;
    }
    return true;
}

std::string expandPath(const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '~') return joinPath(homeDir(), path.substr(1));
    if (path[0] == '/') return path;
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
        return joinPath(std::string(cwd), path);
    }
    return path;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (b[0] == '/') return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string fileExtension(const std::string& path) {
    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    if (slash != std::string::npos && dot < slash) return std::string();
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string baseName(const std::string& path) {
    std::string f = fileName(path);
    size_t dot = f.find_last_of('.');
    return dot == std::string::npos ? f : f.substr(0, dot);
}

std::string makeUid() {
    static std::atomic<uint64_t> counter{0};
    uint64_t n = counter.fetch_add(1) + 1;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t t = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%08llx-%04llx",
                  static_cast<unsigned long long>((t ^ (n * 0x9e3779b97f4a7c15ull)) & 0xffffffffffffffffull),
                  static_cast<unsigned long long>(n & 0xffff));
    return std::string(buf);
}

}  // namespace lspa
