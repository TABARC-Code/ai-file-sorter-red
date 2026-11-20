#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * @brief The archive's decision register: numbered rules for edge cases that have come up before,
 *        kept as Markdown so a person can read and edit it. Consulted before deciding, written after.
 */
namespace DecisionRegister {

/** @brief An edge case that has come up this many times earns a register entry. */
inline constexpr int kWriteAfterOccurrences = 2;

struct Entry {
    int number{0};
    std::string rule;
    std::string reason;
    std::string trigger_pattern;
    std::string anchor_guidance;
    std::string link_guidance;
    std::string organisation_guidance;
    std::string example;
    /** @brief Entry numbers this one replaces. Empty is written as "nothing". */
    std::vector<int> supersedes;
    /** @brief active, superseded or retired. */
    std::string status{"active"};
    /** @brief Date and what triggered the review. */
    std::string reviewed;
};

bool should_write_entry(int occurrences);

/** @brief Next free number: one more than the highest, starting at 1. */
int next_number(const std::vector<Entry>& entries);

/** @brief One entry in the register's Markdown template. */
std::string render_entry(const Entry& entry);

/** @brief The whole register: a title, then every entry in number order. */
std::string render_register(const std::vector<Entry>& entries);

/** @brief Inverse of render_register. Unknown sections are ignored; entries without a number are dropped. */
std::vector<Entry> parse_register(const std::string& text);

/**
 * @brief Problems that stop an entry being added: missing rule, reason or trigger, a multi-line rule,
 *        an illegal status, a number already used, text that would break the template, or a
 *        supersedes reference to an entry that does not exist.
 */
std::vector<std::string> validate_entry(const Entry& entry, const std::vector<Entry>& existing);

/**
 * @brief Automatic parts of the consistency pass: entries sharing a trigger, and anchor codes missing from the
 *        vocabulary. Whether an entry contradicts the sub-skills still needs a person.
 */
std::vector<std::string> consistency_pass(const std::vector<Entry>& entries);

/**
 * @brief Active entries whose trigger has not fired for threshold_days. Entries with no usage data are not
 *        listed, since unknown is not the same as unused. These are for review, never for deletion.
 */
std::vector<int> orphan_candidates(const std::vector<Entry>& entries, const std::map<int, int>& days_since_fired,
                                   int threshold_days);

/**
 * @brief Add a replacement and mark the old entry superseded. Fails, with a message in error, when the old
 *        entry is missing or already retired or superseded, or when the replacement is invalid.
 */
bool supersede(std::vector<Entry>& entries, int old_number, Entry replacement, std::string& error);

}  // namespace DecisionRegister
