#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "MetadataApply.hpp"
#include "MetadataBatch.hpp"
#include "MetadataFolder.hpp"

/**
 * @brief The saved plan: what a run will change, written as JSON so it can be read, reviewed and applied later.
 *        Parsing checks the shape and the version before anything is trusted.
 */
namespace MetadataPlanFile {

inline constexpr const char* kTool = "metadata-maestro";
inline constexpr const char* kVersion = "4.0.0";

struct Change {
    std::string key;
    std::optional<std::string> from;
    std::string to;
    std::string source;
};

struct Entry {
    std::string file;
    /** @brief "id3", "mp4", "vorbis", "image" or "remux". */
    std::string family;
    std::string type;
    MetadataApply::Fingerprint fingerprint;
    std::vector<Change> changes;
    /** @brief Cover instructions as compact JSON, kept as written. Empty for none. */
    std::string cover;
    std::vector<std::string> warnings;
};

struct FolderChange {
    std::string target;
    /** @brief Manifest key. Empty for the text sidecars. */
    std::string key;
    std::optional<std::string> from;
    std::string to;
    std::string source;
};

struct Folder {
    std::string dir;
    std::vector<FolderChange> changes;
};

struct Summary {
    std::size_t files_scanned{0};
    std::size_t files_changing{0};
    std::size_t folders_changing{0};
};

struct Plan {
    std::string tool{kTool};
    std::string version{kVersion};
    std::string created;
    std::string source;
    Summary summary;
    std::vector<std::string> warnings;
    std::vector<Entry> entries;
    std::vector<Folder> folders;
};

/** @brief Indented JSON with a closing newline. */
std::string serialise(const Plan& plan);

struct Parsed {
    Plan plan;
    /** @brief Empty when the plan is usable. */
    std::string error;

    bool ok() const { return error.empty(); }
};

/**
 * @brief Read a plan. A plan from another major version, a missing entries or folders list, or an entry without
 *        a file, family, fingerprint or well-formed changes is refused with the entry number.
 */
Parsed parse(const std::string& text);

struct BatchInput {
    std::vector<MetadataBatch::PlannedEntry> entries;
    /** @brief "<file>: <reason>" for entries no tag writer handles. */
    std::vector<std::string> unsupported;
};

/** @brief Entries the tag writers can do, as batch input. The rest are listed, not dropped silently. */
BatchInput batch_input(const Plan& plan);

/** @brief A folder's changes in the form the folder writer takes. */
std::vector<MetadataFolder::Change> folder_changes(const Folder& folder);

}  // namespace MetadataPlanFile
