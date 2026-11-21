#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "MetadataBatch.hpp"

/**
 * @brief The Metadata Maestro ledger: one JSON object per line recording what each apply, restore and verdict
 *        did, and the lessons read back from it. Building a line is pure; only append_line and read_ledger touch the
 * file system.
 */
namespace MetadataLedger {

/** @brief Where the ledger lives unless told otherwise: ".metadata-maestro/ledger.jsonl" under the folder. */
std::string default_path(const std::string& base_folder);

struct ApplyRecord {
    std::string plan;
    std::size_t ok{0};
    std::size_t already_done{0};
    std::size_t folders_written{0};
    std::vector<MetadataBatch::Failure> failures;
    /** @brief "name.ext: key" lines, as MetadataBatch reports them. The extension becomes the drop's container. */
    std::vector<std::string> dropped;
};

/** @brief One "apply" line. `at` is a timestamp from the caller. */
std::string apply_line(const std::string& at, const ApplyRecord& record);

/** @brief One "restore" line listing every file put back or removed. */
std::string restore_line(const std::string& at, const std::vector<std::string>& files);

struct VerdictRecord {
    std::string plan;
    /** @brief "accept" or "reject". */
    std::string verdict;
    /** @brief Keys the plan touched. Sorted and made unique on the way in. */
    std::vector<std::string> keys;
    std::string note;
};

/** @brief One "verdict" line, or empty when the verdict is neither accept nor reject. */
std::string verdict_line(const std::string& at, const VerdictRecord& record);

/** @brief Add one line, making the folder first. Empty on success, otherwise the problem. */
std::string append_line(const std::string& path, const std::string& line);

/** @brief The ledger's text, or empty when there is none. */
std::string read_ledger(const std::string& path);

struct Lessons {
    std::string ledger;
    std::size_t entries{0};
    /** @brief "ext:key" to how many times that key was dropped. */
    std::map<std::string, int> container_drops;
    /** @brief "key:verdict" to how many times. */
    std::map<std::string, int> verdicts;
    std::size_t restores{0};
    /** @brief Pairs dropped three or more times: worth a proposed edit to the field map. */
    std::vector<std::string> propose_edit_for;
    /** @brief The last ten verdict notes, oldest first. */
    std::vector<std::string> recent_notes;
};

/**
 * @brief Read the lessons out of ledger text. Lines that are not JSON objects are ignored; an entry missing a
 *        field only loses that field.
 */
Lessons lessons(const std::string& ledger_text, const std::string& ledger_path = "");

}  // namespace MetadataLedger
