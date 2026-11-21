#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "MetadataApply.hpp"

/**
 * @brief Runs a plan of tag edits in batches, with a resumable progress file and a free-space check first. Ported
 *        from the apply loop in mm.py. Files are done one at a time, in plan order.
 */
namespace MetadataBatch {

struct PlannedEntry {
    std::string path;
    MetadataApply::Fingerprint fingerprint;
    std::vector<MetadataApply::Edit> edits;
};

/** @brief Progress lines are "ok<TAB>path" or "failed<TAB>path<TAB>error", with tab, newline and backslash escaped. */
std::string encode_progress(const std::string& status, const std::string& path, const std::string& detail = "");

/** @brief Paths recorded as ok. Failed, blank and unreadable lines are ignored. */
std::set<std::string> parse_progress(const std::string& text);

using FreeSpace = std::function<std::uintmax_t(const std::string& folder)>;

/**
 * @brief Empty when there is room. Each folder needs the size of its files for backups, plus 10 percent.
 *        Nothing is needed when backups are off.
 */
std::string space_problem(const std::vector<PlannedEntry>& entries, bool no_backup, const FreeSpace& free_space);

/** @brief "name: key" for each dropped tag in one file. */
std::vector<std::string> describe_drops(const std::string& path, const MetadataApply::Verdict& verdict);

struct BatchSummary {
    /** @brief 1-based. */
    std::size_t index{0};
    std::size_t count{0};
    std::size_t ok{0};
    std::size_t failed{0};
};

struct Options {
    std::size_t batch_size{50};
    bool no_backup{false};
    /** @brief Forget earlier progress and do every file again. */
    bool restart{false};
    /** @brief Where progress is kept. Empty keeps none. */
    std::string progress_file;
    /** @brief For tests. Empty asks the file system. */
    FreeSpace free_space;
    /** @brief Called after each batch, for progress reporting. */
    std::function<void(const BatchSummary&)> on_batch;
};

struct Failure {
    std::string path;
    std::string error;
};

struct Report {
    /** @brief Non-empty when nothing was attempted. */
    std::string error;
    std::size_t ok{0};
    std::size_t already_done{0};
    std::size_t batches{0};
    std::vector<Failure> failures;
    std::vector<std::string> dropped;
};

Report run(const std::vector<PlannedEntry>& entries, const Options& options);

}  // namespace MetadataBatch
