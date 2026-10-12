// Paths.h - application data locations (Qt-free).

#pragma once

#include <string>

namespace lspa {

// ~/.config/lsp-audio-player (respects XDG_CONFIG_HOME)
std::string configDir();
// ~/.local/share/lsp-audio-player (respects XDG_DATA_HOME)
std::string dataDir();
// Default session file path.
std::string defaultSessionPath();
// Default log file path.
std::string defaultLogPath();
// Create directory (and parents) if missing. Returns true if it exists.
bool ensureDir(const std::string& path);
// Expand "~" and make absolute.
std::string expandPath(const std::string& path);
// Join with '/'.
std::string joinPath(const std::string& a, const std::string& b);
// File extension in lower case without dot ("" if none).
std::string fileExtension(const std::string& path);
// File name without directories.
std::string fileName(const std::string& path);
// File name without directories and extension.
std::string baseName(const std::string& path);

}  // namespace lspa
