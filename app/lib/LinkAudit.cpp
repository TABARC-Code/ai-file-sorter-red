#include "LinkAudit.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <sstream>
#include <tuple>

namespace LinkAudit {
namespace {

// ---- text helpers --------------------------------------------------------------------------------------------

struct Line {
    int number{0};
    std::string text;
};

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

/** Trimmed, with trailing sentence punctuation removed. */
std::string strip_end_punct(const std::string& s) {
    std::string out = trim(s);
    while (!out.empty() && std::string(".,;:!").find(out.back()) != std::string::npos) {
        out.pop_back();
    }
    return out;
}

std::vector<std::string> words_of(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string word;
    while (in >> word) {
        out.push_back(word);
    }
    return out;
}

bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
}

/** Whole-name match: "b" is not found in "b-extra" or "xb". */
bool contains_name(const std::string& text, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (std::size_t pos = text.find(name); pos != std::string::npos; pos = text.find(name, pos + 1)) {
        const bool before = pos == 0 || !is_name_char(text[pos - 1]);
        const std::size_t end = pos + name.size();
        const bool after = end >= text.size() || !is_name_char(text[end]);
        if (before && after) {
            return true;
        }
    }
    return false;
}

std::string fence_marker(const std::string& trimmed) {
    if (trimmed.rfind("```", 0) == 0) {
        return "```";
    }
    return trimmed.rfind("~~~", 0) == 0 ? "~~~" : "";
}

/** Lines outside code fences, numbered from 1. Fence lines themselves are dropped. */
std::vector<Line> visible_lines(const std::string& text) {
    std::vector<Line> out;
    std::string open;
    int number = 0;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        ++number;
        const std::string marker = fence_marker(trim(raw));
        if (!marker.empty()) {
            open = open.empty() ? marker : (open == marker ? std::string() : open);
            continue;
        }
        if (open.empty()) {
            out.push_back({number, raw});
        }
    }
    return out;
}

bool is_heading(const std::string& text) {
    const std::size_t hashes = text.find_first_not_of('#');
    return hashes != 0 && hashes != std::string::npos && text[hashes] == ' ';
}

// ---- parsing -------------------------------------------------------------------------------------------------

struct Heading {
    Direction direction{Direction::Outbound};
    std::string name;
};

std::optional<Heading> parse_heading(const std::string& line) {
    if (line.rfind("### ", 0) != 0 || line.size() < 5 || (line[4] != '>' && line[4] != '<')) {
        return std::nullopt;
    }
    if (line.size() > 5 && line[5] != ' ') {
        return std::nullopt;
    }
    std::string name = line.substr(5);
    name.erase(std::remove(name.begin(), name.end(), '`'), name.end());
    name = trim(name);
    if (name.empty()) {
        return std::nullopt;
    }
    return Heading{line[4] == '>' ? Direction::Outbound : Direction::Inbound, name};
}

const char* data_label(Direction d) {
    return d == Direction::Outbound ? "Sends" : "Receives";
}

const char* action_label(Direction d) {
    return d == Direction::Outbound ? "Action for target" : "Action here";
}

enum class Field { None, Data, Action, Feedback };

struct Labelled {
    Field field{Field::None};
    std::string rest;
};

Field field_named(const std::string& label, Direction d) {
    const std::string name = lower(label);
    if (name == lower(data_label(d))) {
        return Field::Data;
    }
    if (name == lower(action_label(d))) {
        return Field::Action;
    }
    return name == "feedback loop" ? Field::Feedback : Field::None;
}

/** A line of the form "**Label:** value". Labels for the wrong direction come back with Field::None. */
std::optional<Labelled> parse_label(const std::string& trimmed, Direction d) {
    if (trimmed.rfind("**", 0) != 0) {
        return std::nullopt;
    }
    const std::size_t close = trimmed.find(":**", 2);
    if (close == std::string::npos) {
        return std::nullopt;
    }
    return Labelled{field_named(trimmed.substr(2, close - 2), d), trim(trimmed.substr(close + 3))};
}

std::string& slot(Contract& c, Field field) {
    switch (field) {
        case Field::Data:
            return c.data;
        case Field::Action:
            return c.action;
        default:
            return c.feedback;
    }
}

void append(std::string& target, const std::string& piece) {
    if (piece.empty()) {
        return;
    }
    target += target.empty() ? piece : " " + piece;
}

/** Take one line of a block into the contract; returns the field that is still open. */
Field absorb(const std::string& text, Contract& c, Field current) {
    const std::string t = trim(text);
    if (t.empty()) {
        return Field::None;
    }
    if (const auto label = parse_label(t, c.direction)) {
        if (label->field != Field::None) {
            append(slot(c, label->field), label->rest);
        }
        return label->field;
    }
    if (current != Field::None) {
        append(slot(c, current), t);
    }
    return current;
}

Contract read_block(const std::vector<Line>& lines, std::size_t start, const Heading& heading, const SkillFile& file) {
    Contract c;
    c.direction = heading.direction;
    c.skill = file.skill;
    c.file = file.file;
    c.line = lines[start].number;
    c.other = heading.name;
    Field current = Field::None;
    for (std::size_t i = start + 1; i < lines.size() && !is_heading(lines[i].text); ++i) {
        current = absorb(lines[i].text, c, current);
    }
    return c;
}

// ---- audit ---------------------------------------------------------------------------------------------------

using Triple = std::tuple<std::string, std::string, Direction>;

struct Context {
    std::vector<Contract> contracts;
    std::set<std::string> scope;
    /** (skill, other skill, direction) for every contract written. */
    std::set<Triple> index;
    /** Pairs joined by a contract at either end, both ways round. */
    std::set<std::pair<std::string, std::string>> linked;
    std::map<std::string, std::string> first_file;
};

Context build_context(const std::vector<SkillFile>& files) {
    Context ctx;
    for (const auto& f : files) {
        ctx.scope.insert(f.skill);
        ctx.first_file.emplace(f.skill, f.file);
        for (auto& c : parse_contracts(f)) {
            const std::string other = other_skill(c);
            ctx.index.insert(Triple{c.skill, other, c.direction});
            ctx.linked.insert({c.skill, other});
            ctx.linked.insert({other, c.skill});
            ctx.contracts.push_back(std::move(c));
        }
    }
    return ctx;
}

std::string describe(const Contract& c) {
    return c.skill + (c.direction == Direction::Outbound ? " > " : " < ") + c.other;
}

Finding on_block(Severity severity, const Contract& c, const std::string& code, const std::string& message) {
    return Finding{severity, c.file, c.line, code, message, true};
}

void check_link(const Context& ctx, const Contract& c, std::vector<Finding>& out) {
    const std::string target = other_skill(c);
    const bool outbound = c.direction == Direction::Outbound;
    if (ctx.scope.count(target) == 0) {
        out.push_back(on_block(Severity::Critical, c, outbound ? "missing-target" : "missing-source",
                               describe(c) + ": " + target + " is not among the audited skills"));
        return;
    }
    const Direction wanted = outbound ? Direction::Inbound : Direction::Outbound;
    if (ctx.index.count(Triple{target, c.skill, wanted}) != 0) {
        return;
    }
    out.push_back(on_block(Severity::Critical, c, outbound ? "missing-backlink" : "missing-forward",
                           describe(c) + ": " + target + (outbound ? " has no < " : " has no > ") + c.skill));
}

enum class Kind { Data, Action, Feedback };

struct FieldRef {
    const char* label;
    const std::string* value;
    Kind kind;
};

/** The finding code for a field that has content but not good content, or nullptr. */
const char* field_problem(Kind kind, const std::string& value, const std::string& target) {
    switch (kind) {
        case Kind::Data:
            return is_vague_data(value) ? "vague-field" : nullptr;
        case Kind::Action:
            return is_vague_action(value) ? "vague-field" : nullptr;
        default:
            return feedback_is_valid(value, target) ? nullptr : "bad-feedback";
    }
}

void check_fields(const Contract& c, std::vector<Finding>& out) {
    const std::string target = other_skill(c);
    const std::array<FieldRef, 3> fields{{{data_label(c.direction), &c.data, Kind::Data},
                                          {action_label(c.direction), &c.action, Kind::Action},
                                          {"Feedback loop", &c.feedback, Kind::Feedback}}};
    for (const auto& f : fields) {
        if (is_placeholder(*f.value)) {
            out.push_back(on_block(Severity::High, c, "empty-field",
                                   std::string("empty ") + f.label + " field in " + describe(c)));
            continue;
        }
        if (const char* code = field_problem(f.kind, *f.value, target)) {
            out.push_back(on_block(Severity::Medium, c, code,
                                   std::string("vague ") + f.label + " field in " + describe(c) + ": " + *f.value));
        }
    }
}

struct Seen {
    int see_also{0};
    int prose{0};
};

Seen scan_mentions(const std::vector<Line>& lines, const std::string& name) {
    Seen seen;
    for (const auto& line : lines) {
        if (!contains_name(line.text, name)) {
            continue;
        }
        if (lower(line.text).find("see also") != std::string::npos) {
            seen.see_also = seen.see_also == 0 ? line.number : seen.see_also;
        } else {
            seen.prose = seen.prose == 0 ? line.number : seen.prose;
        }
    }
    return seen;
}

void check_mentions(const std::vector<SkillFile>& files, const Context& ctx, std::vector<Finding>& out) {
    for (const auto& f : files) {
        const auto lines = visible_lines(f.text);
        for (const auto& name : ctx.scope) {
            if (name == f.skill || ctx.linked.count({f.skill, name}) != 0) {
                continue;
            }
            const Seen seen = scan_mentions(lines, name);
            if (seen.see_also != 0) {
                out.push_back({Severity::Medium, f.file, seen.see_also, "see-also",
                               f.skill + " points at " + name + " with a See also line instead of a contract", false});
            } else if (seen.prose != 0) {
                out.push_back({Severity::Low, f.file, seen.prose, "prose-mention",
                               f.skill + " mentions " + name + " in prose with no contract", false});
            }
        }
    }
}

void check_orphans(const Context& ctx, std::vector<Finding>& out) {
    for (const auto& name : ctx.scope) {
        const bool wired = std::any_of(ctx.linked.begin(), ctx.linked.end(),
                                       [&](const std::pair<std::string, std::string>& p) { return p.first == name; });
        if (!wired) {
            out.push_back({Severity::Low, ctx.first_file.at(name), 0, "orphan-skill",
                           name + " has no contracts in either direction", false});
        }
    }
}

using Blocks = std::map<std::pair<std::string, std::string>, std::vector<const Contract*>>;

/** Outbound contracts grouped by (this skill, target skill). */
Blocks outbound_blocks(const std::vector<Contract>& contracts) {
    Blocks blocks;
    for (const auto& c : contracts) {
        if (c.direction == Direction::Outbound) {
            blocks[{c.skill, other_skill(c)}].push_back(&c);
        }
    }
    return blocks;
}

bool says_origin(const std::vector<const Contract*>& carrying, const std::string& origin) {
    return std::any_of(carrying.begin(), carrying.end(), [&](const Contract* c) {
        return contains_name(c->data + " " + c->action + " " + c->feedback, origin);
    });
}

/**
 * Skills that feed `middle` but are not named in what `middle` sends on to `end`, and have no contract of their own
 * with `end`. Chain rule 4: the middle of a chain should say where its data originated.
 */
std::vector<std::string> unnamed_origins(const Blocks& blocks, const std::vector<const Contract*>& carrying,
                                         const std::string& middle, const std::string& end) {
    std::vector<std::string> origins;
    for (const auto& entry : blocks) {
        const std::string& origin = entry.first.first;
        if (entry.first.second != middle || origin == end) {
            continue;
        }
        if (blocks.count({origin, end}) == 0 && !says_origin(carrying, origin)) {
            origins.push_back(origin);
        }
    }
    return origins;
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (const auto& part : parts) {
        out += (out.empty() ? "" : separator) + part;
    }
    return out;
}

void check_chain_annotations(const Context& ctx, std::vector<Finding>& out) {
    const Blocks blocks = outbound_blocks(ctx.contracts);
    for (const auto& entry : blocks) {
        const std::string& middle = entry.first.first;
        const std::string& end = entry.first.second;
        const auto origins = unnamed_origins(blocks, entry.second, middle, end);
        if (!origins.empty()) {
            const Contract& first = *entry.second.front();
            out.push_back({Severity::Low, first.file, first.line, "missing-chain-annotation",
                           middle + " > " + end + " is in a chain from " + join(origins, ", ") +
                               " but does not say where its data originated",
                           false});
        }
    }
}

void sort_findings(std::vector<Finding>& findings) {
    std::stable_sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        return std::make_tuple(static_cast<int>(a.severity), a.file, a.line) <
               std::make_tuple(static_cast<int>(b.severity), b.file, b.line);
    });
}

void tally(Report& report, std::size_t checked) {
    std::set<std::pair<std::string, int>> failed;
    for (const auto& f : report.findings) {
        if (f.on_contract) {
            failed.insert({f.file, f.line});
        }
    }
    report.checked = checked;
    report.failed = failed.size();
    report.passed = checked - report.failed;
}

// ---- report --------------------------------------------------------------------------------------------------

std::string pass_rate(const Report& report) {
    if (report.checked == 0) {
        return "n/a";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f%%",
                  100.0 * static_cast<double>(report.passed) / static_cast<double>(report.checked));
    return buffer;
}

std::string section(const Report& report, Severity severity, const char* title) {
    std::string out = std::string("### ") + title + "\n";
    bool any = false;
    for (const auto& f : report.findings) {
        if (f.severity != severity) {
            continue;
        }
        any = true;
        out += "[" + f.file + (f.line > 0 ? ":" + std::to_string(f.line) : std::string()) + "] " + f.code + ": " +
               f.message + "\n";
    }
    if (!any) {
        out += "none\n";
    }
    return out + "\n";
}

// ---- chains --------------------------------------------------------------------------------------------------

using Graph = std::map<std::string, std::set<std::string>>;

Graph build_graph(const std::vector<Contract>& contracts) {
    Graph graph;
    for (const auto& c : contracts) {
        if (c.direction == Direction::Outbound) {
            graph[c.skill].insert(other_skill(c));
        }
    }
    return graph;
}

/** Depth-first walk over simple paths. A chain is emitted at a dead end, or where the path feeds its own start. */
struct Walker {
    const Graph& graph;
    std::size_t limit;
    std::vector<Chain>& out;
    std::vector<std::string> path;
    std::set<std::string> on_path;

    void step() {
        bool extended = false;
        bool closes = false;
        const auto edges = graph.find(path.back());
        if (edges != graph.end()) {
            for (const auto& next : edges->second) {
                if (out.size() >= limit) {
                    return;
                }
                if (next == path.front()) {
                    closes = true;
                } else if (on_path.count(next) == 0) {
                    extended = true;
                    descend(next);
                }
            }
        }
        if (path.size() >= 3 && (closes || !extended)) {
            out.push_back({path, closes});
        }
    }

    void descend(const std::string& next) {
        path.push_back(next);
        on_path.insert(next);
        step();
        path.pop_back();
        on_path.erase(next);
    }
};

void rotate_loop_to_smallest(Chain& chain) {
    if (chain.loops_back) {
        std::rotate(chain.skills.begin(), std::min_element(chain.skills.begin(), chain.skills.end()),
                    chain.skills.end());
    }
}

bool is_subpath(const std::vector<std::string>& inner, const std::vector<std::string>& outer) {
    return std::search(outer.begin(), outer.end(), inner.begin(), inner.end()) != outer.end();
}

/** The same cycle found again from another start: an earlier chain already has exactly these skills. */
bool repeats_earlier_loop(const std::vector<Chain>& all, std::size_t i) {
    for (std::size_t j = 0; j < i; ++j) {
        if (all[j].skills == all[i].skills) {
            return true;
        }
    }
    return false;
}

/** A chain is dropped if it sits inside a longer one, or is a copy of an earlier one. */
bool inside_another(const std::vector<Chain>& all, std::size_t i) {
    for (std::size_t j = 0; j < all.size(); ++j) {
        if (!is_subpath(all[i].skills, all[j].skills)) {
            continue;
        }
        if (all[j].skills.size() > all[i].skills.size() || j < i) {
            return true;
        }
    }
    return false;
}

// ---- taxonomy ------------------------------------------------------------------------------------------------

struct TypeInfo {
    const char* name;
    int weight;
};

// Same order as the LinkType enum.
constexpr std::array<TypeInfo, 8> kTypes{{{"technical-dependency", 10},
                                          {"supporting-workflow", 8},
                                          {"quality-control", 8},
                                          {"shared-domain", 6},
                                          {"output-style", 6},
                                          {"similar-function", 4},
                                          {"creative-influence", 3},
                                          {"user-defined", 0}}};

const TypeInfo& info(LinkType type) {
    return kTypes[static_cast<std::size_t>(type)];
}

// ---- writing -------------------------------------------------------------------------------------------------

bool fields_writable(const Contract& c, const std::string& target) {
    for (const std::string* value : {&c.data, &c.action, &c.feedback}) {
        if (is_placeholder(*value) || value->find('\n') != std::string::npos) {
            return false;
        }
    }
    return !is_vague_data(c.data) && !is_vague_action(c.action) && feedback_is_valid(c.feedback, target);
}

}  // namespace

// ---- public: parsing and field rules -------------------------------------------------------------------------

std::string other_skill(const Contract& contract) {
    return contract.other.substr(0, contract.other.find('/'));
}

std::vector<Contract> parse_contracts(const SkillFile& file) {
    const auto lines = visible_lines(file.text);
    std::vector<Contract> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (const auto heading = parse_heading(lines[i].text)) {
            out.push_back(read_block(lines, i, *heading, file));
        }
    }
    return out;
}

bool is_placeholder(const std::string& value) {
    const std::string v = lower(strip_end_punct(value));
    if (v.empty() || v == "tbd" || v == "todo") {
        return true;
    }
    return v.front() == '[' && v.back() == ']' && std::count(v.begin(), v.end(), ']') == 1;
}

bool is_vague_data(const std::string& value) {
    static const std::set<std::string> generic = {"data",     "information", "info", "output",
                                                  "analysis", "results",     "stuff"};
    const auto words = words_of(lower(strip_end_punct(value)));
    return !words.empty() && words.size() <= 2 && generic.count(words.back()) != 0;
}

bool is_vague_action(const std::string& value) {
    static const std::set<std::string> stock = {"use as needed",        "as needed",
                                                "integrate as needed",  "use when relevant",
                                                "apply as appropriate", "use this to inform the analysis"};
    const std::string v = lower(strip_end_punct(value));
    return words_of(v).size() < 3 || stock.count(v) != 0;
}

bool feedback_is_valid(const std::string& value, const std::string& other_skill_name) {
    const std::string v = lower(strip_end_punct(value));
    if (v == "none") {
        return true;
    }
    if (words_of(v).size() < 4) {
        return false;
    }
    return value.find('`') != std::string::npos || value.find('/') != std::string::npos ||
           contains_name(value, other_skill_name);
}

// ---- public: audit -------------------------------------------------------------------------------------------

Report audit(const std::vector<SkillFile>& files) {
    const Context ctx = build_context(files);
    Report report;
    for (const auto& c : ctx.contracts) {
        check_link(ctx, c, report.findings);
        check_fields(c, report.findings);
    }
    check_mentions(files, ctx, report.findings);
    check_orphans(ctx, report.findings);
    check_chain_annotations(ctx, report.findings);
    sort_findings(report.findings);
    tally(report, ctx.contracts.size());
    return report;
}

std::string render_report(const Report& report, const std::string& scope, const std::string& date) {
    std::string out = "## Bidirectional Link Audit Report\nScope: " + scope + "\nMethod: Link-matching\nDate: " + date +
                      "\n\n### Summary\n- Total contracts checked: " + std::to_string(report.checked) +
                      "\n- Passed: " + std::to_string(report.passed) + "\n- Failed: " + std::to_string(report.failed) +
                      "\n- Pass rate: " + pass_rate(report) + "\n\n";
    out += section(report, Severity::Critical, "Critical failures");
    out += section(report, Severity::High, "High failures");
    out += section(report, Severity::Medium, "Medium failures");
    out += section(report, Severity::Low, "Low findings");
    return out;
}

bool must_escalate(int failed_cycles) {
    return failed_cycles >= kMaxAuditCycles;
}

// ---- public: graph -------------------------------------------------------------------------------------------

std::map<std::pair<std::string, std::string>, Cell> adjacency(const std::vector<Contract>& contracts) {
    std::map<std::pair<std::string, std::string>, Cell> cells;
    for (const auto& c : contracts) {
        const std::string other = other_skill(c);
        if (c.direction == Direction::Outbound) {
            cells[{c.skill, other}].forward = true;
        } else {
            cells[{other, c.skill}].backlink = true;
        }
    }
    return cells;
}

std::vector<Chain> find_chains(const std::vector<Contract>& contracts, std::size_t max_chains) {
    const Graph graph = build_graph(contracts);
    std::vector<Chain> found;
    for (const auto& entry : graph) {
        Walker walker{graph, max_chains, found, {entry.first}, {entry.first}};
        walker.step();
    }
    for (auto& chain : found) {
        rotate_loop_to_smallest(chain);
    }
    std::vector<Chain> kept;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const bool drop = found[i].loops_back ? repeats_earlier_loop(found, i) : inside_another(found, i);
        if (!drop) {
            kept.push_back(found[i]);
        }
    }
    return kept;
}

// ---- public: unwiring ----------------------------------------------------------------------------------------

std::vector<Mention> BlastRadius::in_update_order() const {
    std::vector<Mention> out;
    for (const auto* group : {&receivers, &senders, &chain_annotations, &prose}) {
        out.insert(out.end(), group->begin(), group->end());
    }
    return out;
}

namespace {

bool talks_about_a_chain(const std::string& text) {
    const std::string t = lower(text);
    return t.find("chain") != std::string::npos || t.find("originat") != std::string::npos;
}

void collect_mentions(const SkillFile& file, const std::string& departing, BlastRadius& radius) {
    std::map<int, Direction> headings;
    for (const auto& c : parse_contracts(file)) {
        if (other_skill(c) == departing) {
            headings[c.line] = c.direction;
        }
    }
    for (const auto& line : visible_lines(file.text)) {
        const Mention mention{file.skill, file.file, line.number, trim(line.text)};
        const auto heading = headings.find(line.number);
        if (heading != headings.end()) {
            (heading->second == Direction::Inbound ? radius.receivers : radius.senders).push_back(mention);
        } else if (contains_name(line.text, departing)) {
            (talks_about_a_chain(line.text) ? radius.chain_annotations : radius.prose).push_back(mention);
        }
    }
}

}  // namespace

BlastRadius blast_radius(const std::vector<SkillFile>& files, const std::string& departing) {
    BlastRadius radius;
    for (const auto& file : files) {
        if (file.skill != departing) {
            collect_mentions(file, departing, radius);
        }
    }
    return radius;
}

bool replacement_is_wired(const std::vector<SkillFile>& files, const std::string& replacement) {
    std::set<std::string> own_files;
    bool has_contract = false;
    for (const auto& file : files) {
        if (file.skill == replacement) {
            own_files.insert(file.file);
            has_contract = has_contract || !parse_contracts(file).empty();
        }
    }
    if (!has_contract) {
        return false;
    }
    for (const auto& finding : audit(files).findings) {
        const bool serious = finding.severity == Severity::Critical || finding.severity == Severity::High;
        if (serious && own_files.count(finding.file) != 0) {
            return false;
        }
    }
    return true;
}

// ---- public: taxonomy ----------------------------------------------------------------------------------------

int weight(LinkType type, int user_weight) {
    return type == LinkType::UserDefined ? user_weight : info(type).weight;
}

std::string type_name(LinkType type) {
    return info(type).name;
}

std::optional<LinkType> parse_type(const std::string& name) {
    const std::string wanted = lower(trim(name));
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
        if (wanted == kTypes[i].name) {
            return static_cast<LinkType>(i);
        }
    }
    return std::nullopt;
}

std::optional<LinkType> heavier(LinkType a, LinkType b, int user_weight_a, int user_weight_b) {
    const int wa = weight(a, user_weight_a);
    const int wb = weight(b, user_weight_b);
    if (wa == wb) {
        return std::nullopt;
    }
    return wa > wb ? a : b;
}

// ---- public: writing -----------------------------------------------------------------------------------------

std::optional<std::string> render_contract(const Contract& contract) {
    const std::string target = other_skill(contract);
    if (target.empty() || !fields_writable(contract, target)) {
        return std::nullopt;
    }
    const Direction d = contract.direction;
    return std::string("### ") + (d == Direction::Outbound ? ">" : "<") + " " + contract.other + "\n\n**" +
           data_label(d) + ":** " + contract.data + "\n**" + action_label(d) + ":** " + contract.action +
           "\n**Feedback loop:** " + contract.feedback + "\n\n";
}

std::optional<std::string> render_relationships_section(const std::vector<Contract>& contracts) {
    if (contracts.empty()) {
        return std::nullopt;
    }
    std::vector<Contract> sorted = contracts;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Contract& a, const Contract& b) {
        if (a.direction != b.direction) {
            return a.direction == Direction::Inbound;
        }
        return a.other < b.other;
    });
    std::string out = "## Skill relationships\n\n";
    for (const auto& c : sorted) {
        const auto block = render_contract(c);
        if (!block) {
            return std::nullopt;
        }
        out += *block;
    }
    return out;
}

}  // namespace LinkAudit
