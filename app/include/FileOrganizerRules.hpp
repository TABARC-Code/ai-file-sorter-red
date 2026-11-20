#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Deterministic folder-tidying rules: type groups, download-artefact cleanup, age, date folders
 *        and a plan that can be undone. A plan only ever moves files; it never deletes.
 */
namespace FileOrganizerRules {

enum class Category { Documents, Images, Videos, Archives, Installers, Code, Spreadsheets, Presentations, Other };

/** @brief Group for a file name, by extension (case-insensitive). Unknown or missing extensions are Other. */
Category category_for(const std::string& file_name);

/** @brief Folder name for a category. Other maps to "To-Sort". */
std::string category_folder(Category category);

/** @brief "document-final-v2 (1).pdf" becomes "document.pdf". Returns the input if nothing would be left. */
std::string clean_download_artifacts(const std::string& file_name);

/** @brief Replace runs of spaces with a single hyphen. */
std::string hyphenate_spaces(const std::string& name);

/** @brief True when the file has gone untouched for at least archive_after_days. */
bool is_archive_candidate(std::int64_t mtime_seconds, std::int64_t now_seconds, int archive_after_days = 180);

/** @brief "YYYY/MM-Month" in UTC, for example "2023/11-November". */
std::string date_folder(std::int64_t epoch_seconds);

/** @brief "YYYY" in UTC. */
std::string year_folder(std::int64_t epoch_seconds);

/** @brief The first free name: name, then name-2, name-3 and so on, keeping the extension. */
std::string unique_name(const std::string& desired, const std::function<bool(const std::string&)>& exists);

struct FileInfo {
    std::string name;
    std::int64_t mtime_seconds{0};
    bool is_directory{false};
};

struct Move {
    std::string from;
    std::string to;
};

struct Options {
    int archive_after_days{180};
    /** @brief Names (case-insensitive prefixes) that are never touched, such as a current project. */
    std::vector<std::string> protected_prefixes;
    /** @brief Clean "(1)", "copy" and "final" tails from names as files are moved. */
    bool clean_names{true};
};

struct Plan {
    std::vector<Move> moves;
    /** @brief Entries left alone, with the reason. */
    std::vector<std::string> skipped;
};

/**
 * @brief Plan the top level of one folder. Files go to "<Category>/<name>", or "Archive/<year>/<name>"
 *        when old. Folders, hidden files and protected names are skipped. Destinations never collide
 *        with each other or with names already present in existing_names.
 */
Plan plan_by_type(const std::vector<FileInfo>& files, std::int64_t now_seconds, const Options& options,
                  const std::vector<std::string>& existing_names = {});

/** @brief The plan that puts every file back, in reverse order. */
Plan undo_plan(const Plan& plan);

/** @brief One "from<TAB>to" line per move, with tab, newline and backslash escaped. */
std::string serialize_log(const Plan& plan);

/** @brief Inverse of serialize_log. Malformed lines are skipped. */
Plan parse_log(const std::string& text);

}  // namespace FileOrganizerRules
