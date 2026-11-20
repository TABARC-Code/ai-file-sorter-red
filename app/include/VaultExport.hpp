#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "DeweyEngine.hpp"

/**
 * @brief Turns catalogued records into a plain-Markdown vault that Obsidian and similar tools can open:
 *        one note per record, an index, a graph file, and a lint pass that must succeed before anything
 *        is written. Nothing is overwritten unless asked.
 */
namespace VaultExport {

struct Note {
    DeweyEngine::Record record;
    std::string summary;
    /** @brief Longer context, written only for notes that earn a full record. */
    std::string context;
    bool in_project_corpus{false};
};

/** @brief Record id to note file name, for every note that earns a note. */
using FileNames = std::map<std::string, std::string>;

/**
 * @brief Pick a unique "<anchor>-<kebab-title>.md" for each note. Residue and duplicate candidates get none.
 *        A title with nothing usable in it falls back to the record id.
 */
FileNames assign_file_names(const std::vector<Note>& notes);

/** @brief Markdown for one note: frontmatter, summary, relations as wikilinks, and context for full notes. */
std::string render_note(const Note& note, const FileNames& names, const std::map<std::string, std::string>& titles);

/** @brief README.md: notes grouped by anchor, as wikilinks. */
std::string render_index(const std::vector<Note>& notes, const FileNames& names);

/** @brief graph.json with "nodes" and "edges" for the notes that are written. */
std::string render_graph_json(const std::vector<Note>& notes, const FileNames& names);

struct LintReport {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    DeweyEngine::GraphReport graph;

    bool pass() const { return errors.empty(); }
};

/** @brief Check every note that will be written. Links to notes that are not written count as dangling. */
LintReport lint(const std::vector<Note>& notes);

struct WriteReport {
    LintReport lint;
    std::vector<std::string> written;
    std::vector<std::string> unchanged;
    /** @brief Files that already exist with different content and were left alone. */
    std::vector<std::string> conflicts;
    /** @brief Non-empty when nothing was written. */
    std::string error;
};

/** @brief Lint, then write notes, README.md and graph.json under root. Writes nothing if lint fails. */
WriteReport write_vault(const std::filesystem::path& root, const std::vector<Note>& notes, bool overwrite = false);

}  // namespace VaultExport
