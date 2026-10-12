// Session.h - save/load session configuration (FR-08).

#pragma once

#include <string>

#include "core/Types.h"

namespace lspa {

// Serialize session to JSON text.
std::string sessionToJson(const SessionConfig& session);

// Parse JSON text. Returns false and fills errorOut on malformed data or a
// schema version newer than this build understands.
bool sessionFromJson(const std::string& json, SessionConfig& out, std::string& errorOut);

// File helpers. Create parent directory as needed.
bool saveSession(const SessionConfig& session, const std::string& path, std::string& errorOut);
bool loadSession(const std::string& path, SessionConfig& out, std::string& errorOut);

// Rack preset helpers (FR-10).
std::string rackToJsonString(const std::vector<RackEntry>& rack);
bool rackFromJsonString(const std::string& json, std::vector<RackEntry>& out,
                        std::string& errorOut);

}  // namespace lspa
