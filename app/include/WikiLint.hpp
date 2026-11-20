#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * @brief Lint for a folder of Markdown notes that link with [[wikilinks]]: required frontmatter, links that
 *        go nowhere, links that are not returned, orphans and ambiguous names. Works on text already read
 *        from disk, so it has no file-system dependency.
 */
namespace WikiLint {

/** @brief Fields every page must carry in the llm-wiki schema, in the order they are checked. */
const std::vector<std::string>& required_fields();

/** @brief The ten fields the Dewey validator requires on a record, so vault notes written by VaultExport pass. */
const std::vector<std::string>& dewey_required_fields();

struct Frontmatter {
    /** @brief False when there is no leading "---" block, or it is never closed. */
    bool present{false};
    /** @brief Lower-case key to value. List values are joined with ", ". Surrounding quotes are removed. */
    std::map<std::string, std::string> fields;
};

Frontmatter parse_frontmatter(const std::string& text);

/**
 * @brief Link targets in a page body, in order, without repeats. Handles [[a]], [[a|alias]], [[a#heading]]
 *        and [[a^block]]. Skips embeds (![[a]]), code fences, inline code and the frontmatter block.
 */
std::vector<std::string> extract_wikilinks(const std::string& text);

struct Page {
    /** @brief Path inside the vault with forward slashes, such as "notes/idea.md". */
    std::string path;
    std::string text;
};

enum class Severity { Error, Warning };

struct Issue {
    Severity severity{Severity::Warning};
    std::string page;
    std::string code;
    std::string message;
};

/**
 * @brief Lint the whole set.
 *
 * Errors: no-frontmatter, missing-field.
 * Warnings: broken-link, ambiguous-link (a bare name that matches several pages; a path-style link such as
 * [[folder/name]] is not ambiguous), non-reciprocal (reported on the page that should link back) and orphan.
 * Pages named index, readme or log are hubs: reciprocity is not asked of them in either direction, they are
 * never orphans, and they need no frontmatter. Self-links are ignored. Names match without regard to case or the .md
 * extension.
 */
std::vector<Issue> lint(const std::vector<Page>& pages, const std::vector<std::string>& required = required_fields());

std::size_t count_errors(const std::vector<Issue>& issues);

}  // namespace WikiLint
