#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Planning rules for batch metadata edits: natural ordering, filename tokens, CSV round trips and
 *        the source requirement. This only builds a plan; reading and writing tags is done elsewhere.
 */
namespace MetadataPlan {

/** @brief Value that removes a tag instead of setting it. */
inline constexpr const char* kClearMarker = "__clear__";

/** @brief Natural order: digit runs compare as numbers, letters ignore case. "ch2" comes before "ch10". */
bool natural_less(const std::string& a, const std::string& b);

std::vector<std::string> natural_sort(std::vector<std::string> names);

/** @brief Values available to a template for one file. */
struct TokenContext {
    int n{1};
    int total{1};
    std::string stem;
    std::string dir;
};

/**
 * @brief Fill {n}, {n:2} (zero-padded to width 2), {total}, {stem} and {dir}.
 * @return nullopt for an unknown token, a bad width or an unclosed brace.
 */
std::optional<std::string> expand_tokens(const std::string& pattern, const TokenContext& context);

/** @brief Split "field=value" at the first '='. Nullopt when the field is empty or there is no '='. */
std::optional<std::pair<std::string, std::string>> parse_set(const std::string& text);

/** @brief One file's edits. */
struct Entry {
    std::string path;
    std::map<std::string, std::string> set;
    std::vector<std::string> clear;
};

struct Plan {
    std::vector<Entry> entries;
    std::vector<std::string> warnings;
    /** @brief "<path>: <reason>" for files left out of the plan. */
    std::vector<std::string> skipped;
    /** @brief Non-empty when the plan could not be built. */
    std::string error;

    bool ok() const { return error.empty(); }
};

struct Request {
    /** @brief Where the values came from. A plan is refused without one. */
    std::string source;
    std::vector<std::pair<std::string, std::string>> sets;
    /** @brief Extensions to work on, without dots. Empty means every taggable file except images. */
    std::vector<std::string> only_extensions;
};

/** @brief Build a plan from file paths. Paths are put in natural order before numbering. */
Plan build_plan(const std::vector<std::string>& paths, const Request& request);

/** @brief Parse CSV text: quoted fields, doubled quotes, CRLF, and newlines inside quotes. */
std::vector<std::vector<std::string>> parse_csv(const std::string& text);

/**
 * @brief Plan from a CSV whose first column is "path". An empty cell means no change.
 *        Refused without a source.
 */
Plan plan_from_csv(const std::string& csv_text, const std::string& source);

}  // namespace MetadataPlan
