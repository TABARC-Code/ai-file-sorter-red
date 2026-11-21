#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Folder-level sidecars: the Audiobookshelf reader.txt and desc.txt files, and the metadata-maestro.json
 *        manifest. Ported from read_folder, the sidecar planner and apply_folder in mm.py. Reading and planning
 *        are pure; apply_folder is the only part that writes.
 */
namespace MetadataFolder {

inline constexpr const char* kManifest = "metadata-maestro.json";
inline constexpr const char* kReaderFile = "reader.txt";
inline constexpr const char* kDescFile = "desc.txt";

/** @brief A manifest value as text. Anything that is not a JSON string is kept as compact JSON, `is_string` false. */
struct ManifestValue {
    std::string text;
    bool is_string = true;
};

struct State {
    std::string dir;
    std::optional<std::string> reader;
    std::optional<std::string> desc;
    std::vector<std::string> cover_files;
    std::string manifest_error;  // empty when the manifest is fine or missing
    std::map<std::string, ManifestValue> manifest;
};

/** @brief Trimmed reader.txt and desc.txt, cover files (cover|folder .jpg|.jpeg|.png), and the parsed manifest. */
State read_folder(const std::string& dir);

/** @brief One folder edit. `key` is empty for the text sidecars; `from` is empty when there was nothing before. */
struct Change {
    std::string target;
    std::string key;
    std::optional<std::string> from;
    std::string to;
};

struct SidecarPlan {
    std::optional<Change> change;
    std::string warning;
};

/**
 * @brief Decide the content of one sidecar. One planned value wins; several are a warning. With none planned, a
 *        single existing tag value is used; none or several are a warning. Nothing is planned when the (trimmed)
 *        value already matches the file, or when it is empty and there is no file.
 */
SidecarPlan plan_sidecar(const std::string& folder_name, const std::string& file, const std::string& key,
                         const std::vector<std::string>& planned, const std::vector<std::string>& existing,
                         const std::optional<std::string>& current);

struct ManifestPlan {
    std::vector<Change> changes;
    std::string error;
};

/** @brief Manifest edits for key=value pairs. An unreadable manifest is an error; unchanged values are skipped. */
ManifestPlan plan_manifest(const State& state, const std::vector<std::pair<std::string, std::string>>& sets);

struct ApplyResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> written;
};

/**
 * @brief Write folder changes. Backups follow the tag rules: `.mm-bak` copy of a file that existed, `.mm-new`
 *        marker for one that did not. Every target is checked before the first write, so a refusal leaves the
 *        folder untouched. Empty text deletes a sidecar; an empty manifest value removes the key. The manifest is
 *        merged into what is on disk and stamped with `_updated` and `_tool`.
 */
ApplyResult apply_folder(const std::string& dir, const std::vector<Change>& changes, bool no_backup,
                         const std::string& stamp, const std::string& tool);

}  // namespace MetadataFolder
