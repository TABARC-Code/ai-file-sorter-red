#include "DeweyEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace DeweyEngine {

namespace {

constexpr std::size_t kRelationCap = 6;
// Filesystems like FAT store mtimes to two seconds, so "compatible" means within that.
constexpr std::int64_t kMtimeToleranceSeconds = 2;

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

const std::vector<std::pair<std::string, std::string>>& relation_pairs() {
    // Symmetric types map to themselves; each asymmetric pair appears in both directions.
    static const std::vector<std::pair<std::string, std::string>> pairs = {
        {"related-to", "related-to"},
        {"contrasts-with", "contrasts-with"},
        {"same-topic-different-lens", "same-topic-different-lens"},
        {"parallel-to", "parallel-to"},
        {"overlaps-with", "overlaps-with"},
        {"influenced-by", "influenced"},
        {"influenced", "influenced-by"},
        {"critique-of", "critiqued-by"},
        {"critiqued-by", "critique-of"},
        {"subset-of", "has-subset"},
        {"has-subset", "subset-of"},
        {"part-of", "has-part"},
        {"has-part", "part-of"},
        {"applies-to", "has-application-in"},
        {"has-application-in", "applies-to"},
        {"uses-methods-from", "methods-used-in"},
        {"methods-used-in", "uses-methods-from"},
        {"documents", "documented-by"},
        {"documented-by", "documents"},
        {"derived-from", "gives-rise-to"},
        {"gives-rise-to", "derived-from"},
        {"references", "referenced-by"},
        {"referenced-by", "references"},
    };
    return pairs;
}

std::string kebab(const std::string& text) {
    // Bytes above 0x7F are kept as they are so non-English titles survive.
    std::string out;
    bool pending_dash = false;
    for (unsigned char ch : text) {
        const bool keep = std::isalnum(ch) != 0 || ch >= 0x80;
        if (keep) {
            if (pending_dash && !out.empty()) {
                out.push_back('-');
            }
            pending_dash = false;
            out.push_back(ch < 0x80 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch));
        } else {
            pending_dash = true;
        }
    }
    return out;
}

std::string yaml_quote(const std::string& value) {
    std::string out = "\"";
    for (char ch : value) {
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
            default:
                out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

void append_list(std::ostringstream& out, const char* key, const std::vector<std::string>& values) {
    if (values.empty()) {
        out << key << ": []\n";
        return;
    }
    out << key << ":\n";
    for (const auto& value : values) {
        out << "  - " << yaml_quote(value) << "\n";
    }
}

void append_relations(std::ostringstream& out, const char* key, const std::vector<Relation>& relations) {
    if (relations.empty()) {
        out << key << ": []\n";
        return;
    }
    out << key << ":\n";
    for (const auto& relation : relations) {
        out << "  - target: " << yaml_quote(relation.target) << "\n"
            << "    type: " << yaml_quote(relation.type) << "\n"
            << "    tier: " << relation.tier << "\n";
    }
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return text;
}

std::string extension_of(const std::string& name) {
    const auto dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? std::string() : lower(name.substr(dot + 1));
}

// Lower-cased stem with copy, version and numbering suffixes removed, so "a (1)" and "a_v2" meet at "a".
std::string normalised_stem(const std::string& name) {
    const auto dot = name.find_last_of('.');
    std::string stem = lower((dot == std::string::npos || dot == 0) ? name : name.substr(0, dot));
    static const std::regex suffix(R"((\s\(\d+\)|[ _-]copy(\s\d+)?|[ _-]v\d+|[ _-]final)+$)");
    return std::regex_replace(stem, suffix, "");
}

void append_relation_problems(const std::vector<Relation>& relations, const std::set<std::string>& ids,
                              std::vector<std::string>& problems) {
    for (const auto& relation : relations) {
        if (!reverse_relation_type(relation.type)) {
            problems.push_back("relation type '" + relation.type + "' is not legal");
        }
        if (relation.tier < 1 || relation.tier > 3) {
            problems.push_back("relation tier " + std::to_string(relation.tier) + " must be 1, 2 or 3");
        }
        if (ids.count(relation.target) == 0) {
            problems.push_back("relation target '" + relation.target + "' does not resolve to a record");
        }
    }
}

bool similar_names(const std::string& a, const std::string& b) {
    return normalised_stem(a) == normalised_stem(b);
}

}  // namespace

const std::vector<AnchorEntry>& anchor_vocabulary() {
    static const std::vector<AnchorEntry> vocabulary = {
        {"000", "General works and unclassified"},
        {"001.CYB", "Cybernetics and systems theory"},
        {"005", "Software and programming"},
        {"006.3.AI", "Artificial intelligence and machine learning"},
        {"020", "Library and information organisation"},
        {"020.DGR", "DeweyGraph system itself"},
        {"030", "Reference documentation"},
        {"100", "Philosophy and psychology"},
        {"170.ETH", "Ethics and moral reasoning"},
        {"200", "Belief, ritual, mythology, spirituality"},
        {"260.RIT", "Ritual and practice"},
        {"291.MYTH", "Comparative mythology and symbolic systems"},
        {"300", "Society, politics, institutions, culture"},
        {"320.POL", "Politics and governance"},
        {"330.ECON", "Economics and trade"},
        {"400", "Language, communication, symbols"},
        {"410.LING", "Linguistics and terminology"},
        {"500", "Natural science, maths, formal systems"},
        {"510.MATH", "Mathematics and formal systems"},
        {"519.STAT", "Statistics and probability"},
        {"570.BIO", "Biology and ecology"},
        {"600", "Applied science, engineering, medicine, making"},
        {"621.3.CIRC", "Electronics and circuits"},
        {"621.9.FAB", "Fabrication, machining, 3D printing"},
        {"629.AUTO", "Automation and control systems"},
        {"640.CRAFT", "Craft, making, domestic technology"},
        {"700", "Art, design, image, object, performance"},
        {"700.STUDIO", "Studio practice and project archives"},
        {"740.1.TYPO", "Typography and lettering"},
        {"741.SKETCH", "Drawing and sketchbooks"},
        {"760.PRINT", "Printmaking and reproduction"},
        {"770.PHOTO", "Photography"},
        {"780.AUD", "Music and audio"},
        {"791.VID", "Film, video, moving image"},
        {"793.GAME", "Games, rules, and recreation"},
        {"800", "Literature, rhetoric, writing, narrative"},
        {"808.02.ESS", "Essays and non-fiction craft"},
        {"809.CRIT", "Criticism and commentary"},
        {"813.FIC", "Fiction and narrative prose"},
        {"821.POET", "Poetry"},
        {"822.DRAM", "Drama and script"},
        {"900", "History, geography, biography, memory"},
        {"910.GEO", "Geography, places, gazetteers"},
        {"909.TIME", "Chronology and timelines"},
        {"920.MEM", "Biography, memoir, personal journals"},
    };
    return vocabulary;
}

std::optional<std::string> anchor_label(const std::string& code) {
    for (const auto& entry : anchor_vocabulary()) {
        if (entry.code == code) {
            return entry.label;
        }
    }
    return std::nullopt;
}

bool is_legal_format(const std::string& value) {
    static const std::vector<std::string> legal = {"BK",  "ESS", "ART", "PDF",    "NOTE", "NB",
                                                   "IMG", "AUD", "VID", "OBJ",    "ZINE", "PROJ",
                                                   "SK",  "MAP", "ARC", "FOLDER", "CODE", "DATA"};
    return contains(legal, value);
}

bool is_legal_lens(const std::string& value) {
    static const std::vector<std::string> legal = {
        "aesthetic",    "autobiographical", "critical",    "documentary", "ecological",  "economic",
        "ethnographic", "experimental",     "historical",  "material",    "pedagogical", "philosophical",
        "poetic",       "political",        "speculative", "symbolic",    "technical",   "theological"};
    return contains(legal, value);
}

bool is_legal_role(const std::string& value) {
    static const std::vector<std::string> legal = {"source", "process", "reference",
                                                   "output", "residue", "duplicate-candidate"};
    return contains(legal, value);
}

bool is_legal_worthiness(const std::string& value) {
    static const std::vector<std::string> legal = {"archival",         "reference", "process",
                                                   "temporary/intake", "residue",   "duplicate-candidate"};
    return contains(legal, value);
}

bool is_legal_confidence(const std::string& value) {
    static const std::vector<std::string> legal = {"high", "medium", "low", "provisional"};
    return contains(legal, value);
}

std::optional<std::string> reverse_relation_type(const std::string& type) {
    for (const auto& [forward, reverse] : relation_pairs()) {
        if (forward == type) {
            return reverse;
        }
    }
    return std::nullopt;
}

std::string GraphReport::summary() const {
    std::ostringstream out;
    out << (pass ? "PASS" : "FAIL") << ": " << matched << "/" << total << " matched, " << missing << " missing, "
        << dangling << " dangling";
    return out.str();
}

GraphReport verify_graph(const std::vector<Record>& records) {
    using Triple = std::tuple<std::string, std::string, std::string>;  // source, type, target

    std::set<std::string> ids;
    for (const auto& record : records) {
        ids.insert(record.id);
    }

    std::vector<Triple> triples;
    for (const auto& record : records) {
        for (const auto* list : {&record.relations, &record.reverse_relations}) {
            for (const auto& relation : *list) {
                triples.emplace_back(record.id, relation.type, relation.target);
            }
        }
    }
    const std::set<Triple> lookup(triples.begin(), triples.end());

    GraphReport report;
    report.total = triples.size();
    for (const auto& [source, type, target] : triples) {
        if (ids.count(target) == 0) {
            ++report.dangling;
            report.problems.push_back("dangling: " + source + " " + type + " " + target + " (no such record)");
            continue;
        }
        const auto reverse = reverse_relation_type(type);
        if (reverse && lookup.count({target, *reverse, source}) > 0) {
            ++report.matched;
        } else {
            ++report.missing;
            report.problems.push_back("missing reverse: " + source + " " + type + " " + target + " needs " + target +
                                      " " + reverse.value_or("(illegal type)") + " " + source);
        }
    }
    report.pass = report.missing == 0 && report.dangling == 0;
    return report;
}

std::vector<std::string> validate_record(const Record& record, const std::vector<Record>& all_records) {
    std::vector<std::string> problems;

    const std::vector<std::pair<const char*, const std::string*>> required = {
        {"id", &record.id},
        {"title", &record.title},
        {"source_path", &record.source_path},
        {"format", &record.format},
        {"anchor_code", &record.anchor_code},
        {"anchor_label", &record.anchor_label},
        {"material_role", &record.material_role},
        {"classification_confidence", &record.classification_confidence},
        {"worthiness_class", &record.worthiness_class},
        {"status", &record.status}};
    for (const auto& [name, value] : required) {
        if (value->empty()) {
            problems.push_back(std::string("missing required field: ") + name);
        }
    }

    if (!record.anchor_code.empty()) {
        const auto label = anchor_label(record.anchor_code);
        if (!label) {
            problems.push_back("anchor_code '" + record.anchor_code + "' is not in the vocabulary");
        } else if (!record.anchor_label.empty() && *label != record.anchor_label) {
            problems.push_back("anchor_label '" + record.anchor_label + "' does not match '" + *label + "'");
        }
    }
    if (!record.format.empty() && !is_legal_format(record.format)) {
        problems.push_back("format '" + record.format + "' is not a legal marker");
    }
    if (!record.material_role.empty() && !is_legal_role(record.material_role)) {
        problems.push_back("material_role '" + record.material_role + "' is not one of the six roles");
    }
    if (!record.worthiness_class.empty() && !is_legal_worthiness(record.worthiness_class)) {
        problems.push_back("worthiness_class '" + record.worthiness_class + "' is not one of the six classes");
    }
    if (!record.classification_confidence.empty() && !is_legal_confidence(record.classification_confidence)) {
        problems.push_back("classification_confidence '" + record.classification_confidence + "' is not on the scale");
    }
    for (const auto& lens : record.lens_tags) {
        if (!is_legal_lens(lens)) {
            problems.push_back("lens_tags contains illegal tag '" + lens + "'");
        }
    }

    std::set<std::string> ids;
    for (const auto& other : all_records) {
        ids.insert(other.id);
    }
    append_relation_problems(record.relations, ids, problems);
    append_relation_problems(record.reverse_relations, ids, problems);
    if (!record.relations.empty() && record.reverse_relations.empty()) {
        problems.push_back("reverse_relations is empty although relations is not");
    }
    return problems;
}

bool over_relation_cap(const Record& record) {
    return record.relations.size() > kRelationCap;
}

NoteDepth note_depth(const std::string& role, bool in_project_corpus) {
    if (role == "source") {
        return NoteDepth::Full;
    }
    if (role == "process") {
        return in_project_corpus ? NoteDepth::Full : NoteDepth::Short;
    }
    if (role == "reference" || role == "output") {
        return NoteDepth::Short;
    }
    return NoteDepth::None;
}

std::string folder_name(const std::string& three_digit_class, const std::string& title) {
    return three_digit_class + "-" + kebab(title);
}

std::string note_file_name(const std::string& anchor_code, const std::string& title) {
    return anchor_code + "-" + kebab(title) + ".md";
}

std::string to_frontmatter(const Record& record) {
    std::ostringstream out;
    out << "---\n"
        << "id: " << yaml_quote(record.id) << "\n"
        << "title: " << yaml_quote(record.title) << "\n"
        << "source_path: " << yaml_quote(record.source_path) << "\n"
        << "format: " << yaml_quote(record.format) << "\n"
        << "anchor_code: " << yaml_quote(record.anchor_code) << "\n"
        << "anchor_label: " << yaml_quote(record.anchor_label) << "\n"
        << "material_role: " << yaml_quote(record.material_role) << "\n"
        << "primary_function: " << yaml_quote(record.primary_function) << "\n";
    append_list(out, "secondary_domains", record.secondary_domains);
    append_list(out, "lens_tags", record.lens_tags);
    append_list(out, "context_tags", record.context_tags);
    out << "classification_confidence: " << yaml_quote(record.classification_confidence) << "\n";
    append_relations(out, "relations", record.relations);
    append_relations(out, "reverse_relations", record.reverse_relations);
    out << "decision_note: " << yaml_quote(record.decision_note) << "\n"
        << "worthiness_class: " << yaml_quote(record.worthiness_class) << "\n"
        << "status: " << yaml_quote(record.status) << "\n"
        << "---\n";
    return out.str();
}

EvidenceTier duplicate_tier(const FileEvidence& a, const FileEvidence& b) {
    if (a.hash && b.hash) {
        return *a.hash == *b.hash ? EvidenceTier::Confirmed : EvidenceTier::None;
    }
    if (a.size && b.size) {
        if (*a.size == *b.size) {
            const bool same_extension = extension_of(a.name) == extension_of(b.name);
            const bool compatible_times = a.mtime_seconds && b.mtime_seconds &&
                                          std::llabs(*a.mtime_seconds - *b.mtime_seconds) <= kMtimeToleranceSeconds;
            return (same_extension && compatible_times) ? EvidenceTier::Strong : EvidenceTier::Probable;
        }
        return similar_names(a.name, b.name) ? EvidenceTier::Speculative : EvidenceTier::None;
    }
    return similar_names(a.name, b.name) ? EvidenceTier::Advisory : EvidenceTier::None;
}

bool can_nominate_keeper(EvidenceTier tier) {
    return tier == EvidenceTier::Probable || tier == EvidenceTier::Strong || tier == EvidenceTier::Confirmed;
}

DuplicateAction recommend_action(EvidenceTier tier, bool explicit_user_approval) {
    switch (tier) {
        case EvidenceTier::Confirmed:
            return explicit_user_approval ? DuplicateAction::Delete : DuplicateAction::Archive;
        case EvidenceTier::Strong:
        case EvidenceTier::Probable:
            return DuplicateAction::Archive;
        case EvidenceTier::Advisory:
        case EvidenceTier::Speculative:
            return DuplicateAction::Review;
        case EvidenceTier::None:
            break;
    }
    return DuplicateAction::Keep;
}

}  // namespace DeweyEngine
