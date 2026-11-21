#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief The safety layer around tag writes: backup first, verify after, undo on request.
 */
namespace MetadataApply {

inline constexpr const char* kBackupSuffix = ".mm-bak";
inline constexpr const char* kCreatedSuffix = ".mm-new";

/** @brief Trim; track and disc compare as numbers ("03/012" == "3/12", "7/0" == "7"). */
std::string normalise(const std::string& value, const std::string& key);

struct Fingerprint {
    std::uintmax_t size{0};
    long long mtime_ns{0};
    bool operator==(const Fingerprint& other) const { return size == other.size && mtime_ns == other.mtime_ns; }
};

std::optional<Fingerprint> fingerprint(const std::string& path);

struct Edit {
    std::string key;
    /** @brief Empty removes the tag. */
    std::string to;
};

struct Dropped {
    std::string key;
    std::string wanted;
    /** @brief What the file holds now; nullopt when the tag is absent. */
    std::optional<std::string> got;
};

struct Verdict {
    std::vector<std::string> applied;
    std::vector<Dropped> dropped;
};

/** @brief Compare what was asked for with what was read back. A key missing from `got` is absent. */
Verdict verify_edits(const std::vector<Edit>& edits, const std::map<std::string, std::string>& got);

struct Outcome {
    bool ok{false};
    std::string error;
    Verdict verdict;
};

/**
 * @brief Write one file. Refuses when the file changed since the plan. A backup that matches the plan is our
 *        own from a crashed run: roll back to it and redo. Any other backup stops the job. A failed write
 *        puts the original back. The result is read back and checked, so silent drops are reported.
 */
Outcome apply_entry(const std::string& path, const Fingerprint& planned, const std::vector<Edit>& edits,
                    bool no_backup);

struct Marks {
    std::vector<std::string> paths;
    std::string error;
};

/** @brief Backup and created-file markers under the targets, sorted and unique. A missing target is an error. */
Marks find_marks(const std::vector<std::string>& targets);

struct RestoreResult {
    std::vector<std::string> restored;
    std::vector<std::string> removed_created;
    std::string error;
};

/** @brief Put backups back; delete files the job created. */
RestoreResult restore(const std::vector<std::string>& targets);

struct CleanResult {
    std::vector<std::string> would_delete;
    std::vector<std::string> deleted;
    std::string error;
};

/** @brief Dry run unless `yes`: lists the markers, deletes them only when told to. */
CleanResult clean(const std::vector<std::string>& targets, bool yes);

}  // namespace MetadataApply
