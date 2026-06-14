#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>
#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// CC0 asset providers — minimal static catalog of the three sources called
// out in the Gear development plan. Each provider exposes a search endpoint
// and a download URL pattern.
//
// Provider "polyhaven":   https://api.polyhaven.com  (search: /assets?c=Textures&q=...)
// Provider "ambientcg":   https://ambientcg.com      (no public search API; the
//                         catalog is served as a static JSON dump that we
//                         attempt to fetch from the CDN)
// Provider "kenney":      https://kenney.nl/           (HTML pages only; we
//                         expose the well-known mirror at assets.kenney.nl)
//
// All three are CC0 (public domain).
// ---------------------------------------------------------------------------

namespace {

struct ProviderInfo {
    const char *id;
    const char *name;
    const char *base_url;
    const char *description;
    const char *license;
};

const std::vector<ProviderInfo> &providers() {
    static const std::vector<ProviderInfo> kProviders = {
        {"polyhaven", "Poly Haven",
         "https://api.polyhaven.com",
         "Poly Haven Textures, Models, and HDRIs (CC0)",
         "CC0"},
        {"ambientcg", "AmbientCG",
         "https://ambientcg.com",
         "AmbientCG seamless textures and materials (CC0)",
         "CC0"},
        {"kenney", "Kenney",
         "https://kenney.nl",
         "Kenney game assets (CC0)",
         "CC0"}
    };
    return kProviders;
}

const ProviderInfo *find_provider(const std::string &p_id) {
    for (const auto &p : providers()) {
        if (p.id == p_id) return &p;
    }
    return nullptr;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Parse scheme://host:port from a base URL like "https://api.polyhaven.com".
// Returns {"https", "api.polyhaven.com", ""} etc.
struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
};

ParsedUrl parse_base_url(const std::string &p_url) {
    ParsedUrl out;
    out.scheme = "https";
    std::string rest = p_url;
    size_t pos = rest.find("://");
    if (pos != std::string::npos) {
        out.scheme = rest.substr(0, pos);
        rest = rest.substr(pos + 3);
    }
    size_t slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);
    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        out.host = rest.substr(0, colon);
        out.port = rest.substr(colon + 1);
    } else {
        out.host = rest;
    }
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// list_asset_providers — return all configured CC0 providers
// ---------------------------------------------------------------------------

static const char *LIST_ASSET_PROVIDERS_SCHEMA = R"schema({
    "type": "object",
    "properties": {}
})schema";

static void handle_list_asset_providers(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)p_params_json; (void)r_error;
    json arr = json::array();
    for (const auto &p : providers()) {
        arr.push_back({
            {"id", p.id},
            {"name", p.name},
            {"base_url", p.base_url},
            {"description", p.description},
            {"license", p.license}
        });
    }
    json result = {{"providers", arr}, {"count", (int)arr.size()}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// search_assets — search a provider's catalog (best-effort; network required)
//
// Poly Haven has a real JSON search endpoint. AmbientCG and Kenney do not
// expose a public JSON search API, so for them we return an empty result with
// a hint pointing the user to a manual browser search URL.
// ---------------------------------------------------------------------------

static const char *SEARCH_ASSETS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "provider": {"type": "string", "description": "Provider id: 'polyhaven', 'ambientcg', 'kenney'"},
        "query": {"type": "string", "description": "Search query (e.g., 'wood', 'grass')"},
        "category": {"type": "string", "description": "Optional category filter (e.g., 'Textures', 'Models', 'HDRIs')", "default": ""},
        "max_results": {"type": "integer", "description": "Maximum number of results to return", "default": 20}
    },
    "required": ["provider", "query"]
})schema";

static void handle_search_assets(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string provider_id = to_lower(params.value("provider", ""));
    std::string query = params.value("query", "");
    std::string category = params.value("category", "");
    int max_results = params.value("max_results", 20);
    if (max_results < 1) max_results = 1;
    if (max_results > 200) max_results = 200;

    if (provider_id.empty()) { r_error = "Missing required parameter: provider"; return; }
    if (query.empty()) { r_error = "Missing required parameter: query"; return; }

    const ProviderInfo *provider = find_provider(provider_id);
    if (!provider) { r_error = "Unknown provider: " + provider_id; return; }

    // Poly Haven is the only one with a real JSON search API
    if (provider_id == "polyhaven") {
        ParsedUrl url = parse_base_url(provider->base_url);
        int port = url.port.empty() ? 0 : std::atoi(url.port.c_str());

        httplib::Client cli(url.host, port == 0 ? 443 : port);
        cli.set_follow_location(true);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(15, 0);

        std::string path = "/assets?type=";
        if (!category.empty()) path += category;
        else path += "all";

        // Poly Haven supports ?q= for substring match
        path += "&q=" + query;

        auto res = cli.Get(path.c_str());
        if (!res) {
            std::string err = httplib::to_string(res.error());
            r_error = "HTTP request to Poly Haven failed: " + err;
            return;
        }
        if (res->status < 200 || res->status >= 300) {
            r_error = "Poly Haven search returned HTTP " + std::to_string(res->status);
            return;
        }

        json hits;
        try {
            hits = json::parse(res->body);
        } catch (...) {
            r_error = "Poly Haven search returned invalid JSON";
            return;
        }

        // Poly Haven returns a map: { "asset-slug": { "name": "...", "categories": [...], "tags": [...], "thumbnail": "..." } }
        json results = json::array();
        for (auto it = hits.begin(); it != hits.end() && (int)results.size() < max_results; ++it) {
            const auto &val = it.value();
            json entry = {
                {"id", it.key()},
                {"name", val.value("name", it.key())},
                {"provider", "polyhaven"},
                {"license", "CC0"},
                {"download_url", "https://polyhaven.com/a/" + it.key()}
            };
            if (val.contains("categories")) entry["categories"] = val["categories"];
            if (val.contains("tags")) entry["tags"] = val["tags"];
            if (val.contains("thumbnail")) entry["thumbnail"] = val["thumbnail"];
            results.push_back(entry);
        }

        json result = {
            {"success", true},
            {"provider", "polyhaven"},
            {"query", query},
            {"count", (int)results.size()},
            {"results", results}
        };
        r_result = result.dump();
        return;
    }

    // AmbientCG: no public search API — return a manual search URL hint
    if (provider_id == "ambientcg") {
        json result = {
            {"success", true},
            {"provider", "ambientcg"},
            {"query", query},
            {"count", 0},
            {"results", json::array()},
            {"note", "AmbientCG does not expose a public search API. "
                     "Use the manual search URL below to browse in a browser, "
                     "or call fetch_asset with a known asset id."},
            {"manual_search_url", "https://ambientcg.com/find?q=" + query}
        };
        r_result = result.dump();
        return;
    }

    // Kenney: no public search API either
    if (provider_id == "kenney") {
        json result = {
            {"success", true},
            {"provider", "kenney"},
            {"query", query},
            {"count", 0},
            {"results", json::array()},
            {"note", "Kenney does not expose a public search API. "
                     "Use the manual search URL below to browse in a browser, "
                     "or call fetch_asset with a known asset id."},
            {"manual_search_url", "https://kenney.nl/assets?q=" + query}
        };
        r_result = result.dump();
        return;
    }

    r_error = "Provider not implemented: " + provider_id;
}

// ---------------------------------------------------------------------------
// fetch_asset — download a known asset URL into the project's assets folder,
// then ask the editor to refresh the filesystem so the new file gets imported.
//
// `asset_url` must be a fully-qualified https:// URL to a single file.
// `dest_subdir` is relative to the project root (default: "assets/external/").
// ---------------------------------------------------------------------------

static const char *FETCH_ASSET_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "asset_url": {"type": "string", "description": "Fully-qualified https:// URL of the file to download"},
        "dest_subdir": {"type": "string", "description": "Subdirectory under the project root (default: assets/external/)", "default": "assets/external/"},
        "filename": {"type": "string", "description": "Override the output filename; default = name from URL"},
        "refresh_filesystem": {"type": "boolean", "description": "If true, call EditorFileSystem.scan() after the download", "default": true}
    },
    "required": ["asset_url"]
})schema";

static std::string filename_from_url(const std::string &p_url) {
    size_t qm = p_url.find('?');
    std::string clean = (qm == std::string::npos) ? p_url : p_url.substr(0, qm);
    size_t slash = clean.find_last_of('/');
    std::string name = (slash == std::string::npos) ? clean : clean.substr(slash + 1);
    if (name.empty()) name = "asset.bin";
    return name;
}

static void handle_fetch_asset(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string asset_url = params.value("asset_url", "");
    if (asset_url.empty()) { r_error = "Missing required parameter: asset_url"; return; }
    if (asset_url.substr(0, 7) != "http://" && asset_url.substr(0, 8) != "https://") {
        r_error = "asset_url must be an http(s):// URL";
        return;
    }
    std::string dest_subdir = params.value("dest_subdir", "assets/external/");
    std::string filename = params.value("filename", "");
    bool refresh_fs = params.value("refresh_filesystem", true);

    ParsedUrl url = parse_base_url(asset_url);
    std::string path = asset_url;
    size_t pos = path.find("://");
    if (pos != std::string::npos) {
        path = path.substr(pos + 3);
    }
    pos = path.find('/');
    if (pos == std::string::npos) {
        r_error = "Could not parse asset_url: missing path";
        return;
    }
    path = path.substr(pos);

    // Default filename = last path component
    if (filename.empty()) filename = filename_from_url(asset_url);

    // Sanitize filename (no path traversal, no slashes)
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) filename = filename.substr(last_slash + 1);
    if (filename.empty() || filename == "." || filename == "..") {
        r_error = "Refusing to use a suspicious filename";
        return;
    }
    // Strip a leading drive-letter on Windows
    if (filename.size() >= 2 && filename[1] == ':') filename = filename.substr(2);

    std::string project = GodotAPI::get_project_path();
    if (project.empty()) { r_error = "No active project detected"; return; }

    // Normalize dest_subdir: ensure forward slashes and no leading slash
    std::string sub = dest_subdir;
    std::replace(sub.begin(), sub.end(), '\\', '/');
    while (!sub.empty() && sub.front() == '/') sub.erase(sub.begin());
    while (!sub.empty() && sub.back() != '/') {} // not used; we add a slash below
    if (!sub.empty() && sub.back() != '/') sub.push_back('/');

    // Path traversal protection
    if (sub.find("..") != std::string::npos) {
        r_error = "dest_subdir may not contain '..'";
        return;
    }

    std::string full_dir = project + "/" + sub;
    std::string full_path = full_dir + filename;

    std::error_code ec;
    fs::create_directories(full_dir, ec);
    if (ec) {
        r_error = "Failed to create destination directory: " + ec.message();
        return;
    }

    int fetch_port = url.port.empty() ? 443 : std::atoi(url.port.c_str());
    httplib::Client cli(url.host, fetch_port);
    cli.set_follow_location(true);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(60, 0);

    auto t_start = std::chrono::steady_clock::now();
    auto res = cli.Get(path.c_str());
    auto t_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    if (!res) {
        std::string err = httplib::to_string(res.error());
        r_error = "HTTP request failed: " + err;
        return;
    }
    if (res->status < 200 || res->status >= 300) {
        r_error = "Asset server returned HTTP " + std::to_string(res->status);
        return;
    }

    // Write to a temp file then move into place
    std::string tmp = full_path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) { r_error = "Cannot open output file: " + tmp; return; }
    out.write(res->body.data(), (std::streamsize)res->body.size());
    out.close();

    std::error_code rename_ec;
    fs::rename(tmp, full_path, rename_ec);
    if (rename_ec) {
        // Fallback: copy + remove
        std::ifstream src(tmp, std::ios::binary);
        std::ofstream dst(full_path, std::ios::binary | std::ios::trunc);
        dst << src.rdbuf();
        src.close();
        dst.close();
        fs::remove(tmp, ec);
    }

    int64_t size = fs::exists(full_path, ec) ? (int64_t)fs::file_size(full_path, ec) : 0;

    // Refresh the editor filesystem so the new file gets imported
    bool refreshed = false;
    if (refresh_fs) {
        GDExtensionObjectPtr efs = GodotAPI::get_global_singleton("EditorFileSystem");
        if (efs) {
            GodotAPI::call_method_void(efs, "scan", nullptr, 0);
            refreshed = true;
        }
    }

    // Build res:// path
    std::string res_path = "res://" + sub + filename;

    json result = {
        {"success", true},
        {"asset_url", asset_url},
        {"dest_path", full_path},
        {"res_path", res_path},
        {"size_bytes", size},
        {"elapsed_ms", elapsed_ms},
        {"refreshed_filesystem", refreshed}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_assets_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("list_asset_providers",
        "List configured CC0 asset providers (polyhaven, ambientcg, kenney)",
        gear_mcp::LIST_ASSET_PROVIDERS_SCHEMA, gear_mcp::handle_list_asset_providers, /*main_thread=*/false);

    p_registry->register_tool("search_assets",
        "Search a CC0 provider's catalog (Poly Haven supports real search; others return manual-search hints)",
        gear_mcp::SEARCH_ASSETS_SCHEMA, gear_mcp::handle_search_assets, /*main_thread=*/false);

    p_registry->register_tool("fetch_asset",
        "Download a file from an https URL into the project and refresh the editor filesystem",
        gear_mcp::FETCH_ASSET_SCHEMA, gear_mcp::handle_fetch_asset, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered assets tools: list_asset_providers, search_assets, fetch_asset\n");
}
