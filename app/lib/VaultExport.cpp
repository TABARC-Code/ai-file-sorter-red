#include "VaultExport.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

#include "FileOrganizerRules.hpp"

namespace VaultExport {

namespace {

using DeweyEngine::NoteDepth;

NoteDepth depth_of(const Note& note) {
    return DeweyEngine::note_depth(note.record.material_role, note.in_project_corpus);
}

std::vector<const Note*> written_notes(const std::vector<Note>& notes) {
    std::vector<const Note*> kept;
    for (const auto& note : notes) {
        if (depth_of(note) != NoteDepth::None) {
            kept.push_back(&note);
        }
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const Note* a, const Note* b) { return a->record.id < b->record.id; });
    return kept;
}

std::string stem_of(const std::string& file_name) {
    return file_name.size() > 3 && file_name.compare(file_name.size() - 3, 3, ".md") == 0
               ? file_name.substr(0, file_name.size() - 3)
               : file_name;
}

std::string safe_display(std::string text) {
    for (char& ch : text) {
        if (ch == '|') {
            ch = '/';
        } else if (ch == ']') {
            ch = ')';
        } else if (ch == '[') {
            ch = '(';
        }
    }
    return text;
}

std::string title_for(const std::string& id, const std::map<std::string, std::string>& titles) {
    const auto found = titles.find(id);
    return found == titles.end() ? id : found->second;
}

std::string link_to(const std::string& id, const FileNames& names, const std::map<std::string, std::string>& titles) {
    const auto found = names.find(id);
    if (found == names.end()) {
        return id + " (not in the vault)";
    }
    return "[[" + stem_of(found->second) + "|" + safe_display(title_for(id, titles)) + "]]";
}

std::string json_escape(const std::string& text) {
    std::string out;
    for (unsigned char ch : text) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", ch);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    return out;
}

std::string json_string(const std::string& text) {
    return "\"" + json_escape(text) + "\"";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_file(const std::filesystem::path& path, const std::string& content) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << content;
        if (!out) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::map<std::string, std::string> title_map(const std::vector<Note>& notes) {
    std::map<std::string, std::string> titles;
    for (const auto& note : notes) {
        titles[note.record.id] = note.record.title;
    }
    return titles;
}

// Writes one file under the conflict rules and records the outcome in the report.
void put_file(const std::filesystem::path& root, const std::string& name, const std::string& content, bool overwrite,
              WriteReport& report) {
    const auto path = root / name;
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        if (read_file(path) == content) {
            report.unchanged.push_back(name);
            return;
        }
        if (!overwrite) {
            report.conflicts.push_back(name);
            return;
        }
    }
    if (write_file(path, content)) {
        report.written.push_back(name);
    } else {
        report.conflicts.push_back(name + " (could not be written)");
    }
}

}  // namespace

FileNames assign_file_names(const std::vector<Note>& notes) {
    FileNames names;
    std::set<std::string> taken;
    auto is_taken = [&](const std::string& name) { return taken.count(name) > 0; };

    for (const Note* note : written_notes(notes)) {
        std::string file = DeweyEngine::note_file_name(note->record.anchor_code, note->record.title);
        if (file == note->record.anchor_code + "-.md") {
            file = DeweyEngine::note_file_name(note->record.anchor_code, note->record.id);
        }
        file = FileOrganizerRules::unique_name(file, is_taken);
        taken.insert(file);
        names[note->record.id] = file;
    }
    return names;
}

std::string render_note(const Note& note, const FileNames& names, const std::map<std::string, std::string>& titles) {
    std::ostringstream out;
    out << DeweyEngine::to_frontmatter(note.record) << "\n# " << note.record.title << "\n\n" << note.summary << "\n";

    if (!note.record.relations.empty()) {
        out << "\n## Relations\n\n";
        for (const auto& relation : note.record.relations) {
            out << "- " << relation.type << ": " << link_to(relation.target, names, titles) << "\n";
        }
    }
    if (depth_of(note) == NoteDepth::Full) {
        if (!note.context.empty()) {
            out << "\n## Context\n\n" << note.context << "\n";
        }
        if (!note.record.decision_note.empty()) {
            out << "\n## Decision note\n\n" << note.record.decision_note << "\n";
        }
    }
    return out.str();
}

std::string render_index(const std::vector<Note>& notes, const FileNames& names) {
    std::vector<const Note*> kept = written_notes(notes);
    std::stable_sort(kept.begin(), kept.end(), [](const Note* a, const Note* b) {
        return std::tie(a->record.anchor_code, a->record.title) < std::tie(b->record.anchor_code, b->record.title);
    });

    std::ostringstream out;
    out << "# Archive index\n\n" << kept.size() << " notes\n";
    std::string current_anchor;
    for (const Note* note : kept) {
        if (note->record.anchor_code != current_anchor) {
            current_anchor = note->record.anchor_code;
            out << "\n## " << current_anchor << " - " << note->record.anchor_label << "\n\n";
        }
        out << "- [[" << stem_of(names.at(note->record.id)) << "|" << safe_display(note->record.title) << "]] ("
            << note->record.material_role << ", " << note->record.classification_confidence << ")\n";
    }
    return out.str();
}

std::string render_graph_json(const std::vector<Note>& notes, const FileNames& names) {
    const auto kept = written_notes(notes);
    std::string nodes;
    std::string edges;
    for (const Note* note : kept) {
        const auto& record = note->record;
        nodes += std::string(nodes.empty() ? "" : ",") + "{\"id\":" + json_string(record.id) +
                 ",\"title\":" + json_string(record.title) + ",\"anchor\":" + json_string(record.anchor_code) +
                 ",\"role\":" + json_string(record.material_role) + "}";
        for (const auto& relation : record.relations) {
            if (names.count(relation.target) == 0) {
                continue;
            }
            edges += std::string(edges.empty() ? "" : ",") + "{\"source\":" + json_string(record.id) +
                     ",\"target\":" + json_string(relation.target) + ",\"type\":" + json_string(relation.type) +
                     ",\"tier\":" + std::to_string(relation.tier) + "}";
        }
    }
    return "{\"nodes\":[" + nodes + "],\"edges\":[" + edges + "]}";
}

LintReport lint(const std::vector<Note>& notes) {
    LintReport report;
    const auto kept = written_notes(notes);

    std::vector<DeweyEngine::Record> records;
    std::set<std::string> seen;
    for (const Note* note : kept) {
        records.push_back(note->record);
        if (!seen.insert(note->record.id).second) {
            report.errors.push_back(note->record.id + ": id appears more than once");
        }
    }
    for (const Note* note : kept) {
        const auto& id = note->record.id;
        if (note->summary.empty()) {
            report.errors.push_back(id + ": summary is required");
        }
        for (const auto& problem : DeweyEngine::validate_record(note->record, records)) {
            report.errors.push_back(id + ": " + problem);
        }
        if (DeweyEngine::over_relation_cap(note->record)) {
            report.warnings.push_back(id + ": more than six relations, this may be a collection");
        }
    }

    report.graph = DeweyEngine::verify_graph(records);
    if (!report.graph.pass) {
        report.errors.push_back("link graph " + report.graph.summary());
        for (const auto& problem : report.graph.problems) {
            report.errors.push_back(problem);
        }
    }
    return report;
}

WriteReport write_vault(const std::filesystem::path& root, const std::vector<Note>& notes, bool overwrite) {
    WriteReport report;
    report.lint = lint(notes);
    if (!report.lint.pass()) {
        report.error = "lint failed with " + std::to_string(report.lint.errors.size()) + " error(s); nothing written";
        return report;
    }

    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        report.error = "could not create " + root.string();
        return report;
    }

    const FileNames names = assign_file_names(notes);
    const auto titles = title_map(notes);
    for (const Note* note : written_notes(notes)) {
        put_file(root, names.at(note->record.id), render_note(*note, names, titles), overwrite, report);
    }
    put_file(root, "README.md", render_index(notes, names), overwrite, report);
    put_file(root, "graph.json", render_graph_json(notes, names), overwrite, report);
    return report;
}

}  // namespace VaultExport
