#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// Renderer-independent, project-authored package contracts. Numeric handles are
// session-local; package, save and network boundaries use namespaced identities.
namespace cc::content {

using Json = nlohmann::json;
namespace fs = std::filesystem;

struct Diagnostic {
    std::string code, message, package, content, field, other_owner;
    Json json() const;
};

class Error : public std::runtime_error {
  public:
    explicit Error(Diagnostic diagnostic);
    Diagnostic diagnostic;
};

[[noreturn]] void fail(const std::string &code, const std::string &message,
                       const std::string &package = {}, const std::string &content = {},
                       const std::string &field = {}, const std::string &other_owner = {});
Json parse_json(const std::string &text, const std::string &source = {});
Json read_json(const fs::path &path);
void write_json_atomic(const fs::path &path, const Json &value);
std::string sha256(const std::string &bytes);
std::string file_sha256(const fs::path &path);
bool valid_id(const std::string &id);
bool valid_content_id(const std::string &id);
fs::path confined_path(const fs::path &root, const std::string &relative);
struct ResourceReference {
    std::string owner, path;
};
ResourceReference resource_reference(const std::string &owner, const std::string &value);

struct Definition {
    std::string kind, id, owner, behavior;
    Json data = Json::object();
    int32_t handle = -1;
};

struct Package {
    fs::path root;
    Json manifest;
    std::string id, version, digest;
    std::vector<Definition> definitions;
};

struct ResolvedContentSet {
    std::vector<Definition> definitions;
    std::vector<Package> packages;
    Json lock = Json::object();
    std::string gameplay_digest, session_digest;
    const Definition *find(const std::string &kind, const std::string &id) const;
    const Definition *by_handle(const std::string &kind, int32_t handle) const;
    std::vector<const Definition *> of_kind(const std::string &kind) const;
    fs::path resource(const std::string &owner, const std::string &relative) const;
};

using Capabilities = std::map<std::string, int>;
// Stock definitions have fixed handles and owner "stock". Each runtime adapter
// validates its supported behavior parameters before publishing the result.
Package load_package(const fs::path &root, bool verify_files = true);
ResolvedContentSet resolve(const Json &profile, const fs::path &profile_root, const fs::path &store,
                           const std::vector<Definition> &stock, const Capabilities &capabilities);
Json inspect(const ResolvedContentSet &resolved);
Json installed_packages(const fs::path &store);
fs::path install_package(const fs::path &source, const fs::path &store);
void package_directory(const fs::path &source, const fs::path &output);

} // namespace cc::content
