#include "DecisionRegister.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>

#include "DeweyEngine.hpp"

namespace DecisionRegister {

namespace {

const char* const kStatuses[] = {"active", "superseded", "retired"};

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string normalise_trigger(const std::string& text) {
    std::string out;
    bool pending_space = false;
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isspace(c) != 0) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string supersedes_text(const std::vector<int>& numbers) {
    if (numbers.empty()) {
        return "nothing";
    }
    std::string out;
    for (const int n : numbers) {
        out += out.empty() ? "" : ", ";
        out += std::to_string(n);
    }
    return out;
}

std::vector<int> parse_supersedes(const std::string& text) {
    std::vector<int> numbers;
    static const std::regex digits("[0-9]+");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), digits); it != std::sregex_iterator(); ++it) {
        numbers.push_back(std::stoi(it->str()));
    }
    return numbers;
}

bool has_heading_line(const std::string& text) {
    for (const auto& line : split_lines(text)) {
        if (line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool known_status(const std::string& status) {
    return std::any_of(std::begin(kStatuses), std::end(kStatuses), [&](const char* s) { return status == s; });
}

void assign_section(Entry& entry, const std::string& name, const std::string& body) {
    if (name == "Rule") {
        entry.rule = body;
    } else if (name == "Reason") {
        entry.reason = body;
    } else if (name == "Trigger pattern") {
        entry.trigger_pattern = body;
    } else if (name == "Anchor guidance") {
        entry.anchor_guidance = body;
    } else if (name == "Link guidance") {
        entry.link_guidance = body;
    } else if (name == "Organisation guidance") {
        entry.organisation_guidance = body;
    } else if (name == "Example") {
        entry.example = body;
    } else if (name == "Supersedes") {
        entry.supersedes = parse_supersedes(body);
    } else if (name == "Status") {
        entry.status = body;
    } else if (name == "Reviewed") {
        entry.reviewed = body;
    }
}

/** Anchor-shaped tokens (three digits, optional dotted suffixes) that are not part of a longer number or a percentage.
 */
std::vector<std::string> anchor_like_tokens(const std::string& text) {
    std::vector<std::string> tokens;
    static const std::regex shape("[0-9]{3}(\\.[0-9A-Za-z]+)*");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), shape); it != std::sregex_iterator(); ++it) {
        const auto start = static_cast<std::size_t>(it->position());
        const auto stop = start + static_cast<std::size_t>(it->length());
        const bool glued_before =
            start > 0 && (std::isalnum(static_cast<unsigned char>(text[start - 1])) != 0 || text[start - 1] == '.');
        const bool glued_after =
            stop < text.size() && (std::isalnum(static_cast<unsigned char>(text[stop])) != 0 || text[stop] == '%');
        if (!glued_before && !glued_after) {
            tokens.push_back(it->str());
        }
    }
    return tokens;
}

void append_field_problems(const Entry& entry, std::vector<std::string>& problems) {
    if (trim(entry.rule).empty()) {
        problems.push_back("rule is empty");
    } else if (entry.rule.find('\n') != std::string::npos) {
        problems.push_back("rule must be a single line");
    }
    if (trim(entry.reason).empty()) {
        problems.push_back("reason is empty");
    }
    if (trim(entry.trigger_pattern).empty()) {
        problems.push_back("trigger pattern is empty");
    }
    if (!known_status(entry.status)) {
        problems.push_back("status '" + entry.status + "' is not active, superseded or retired");
    }
    const std::string* fields[] = {&entry.rule,
                                   &entry.reason,
                                   &entry.trigger_pattern,
                                   &entry.anchor_guidance,
                                   &entry.link_guidance,
                                   &entry.organisation_guidance,
                                   &entry.example,
                                   &entry.reviewed};
    for (const std::string* field : fields) {
        if (has_heading_line(*field)) {
            problems.push_back("text contains a line that looks like a register heading");
            break;
        }
    }
}

}  // namespace

bool should_write_entry(int occurrences) {
    return occurrences >= kWriteAfterOccurrences;
}

int next_number(const std::vector<Entry>& entries) {
    int highest = 0;
    for (const auto& entry : entries) {
        highest = std::max(highest, entry.number);
    }
    return highest + 1;
}

std::string render_entry(const Entry& entry) {
    std::ostringstream out;
    out << "## Entry " << std::setfill('0') << std::setw(3) << entry.number << "\n\n";
    const auto section = [&](const char* name, const std::string& body) {
        out << "### " << name << "\n" << trim(body) << "\n\n";
    };
    section("Rule", entry.rule);
    section("Reason", entry.reason);
    section("Trigger pattern", entry.trigger_pattern);
    section("Anchor guidance", entry.anchor_guidance);
    section("Link guidance", entry.link_guidance);
    section("Organisation guidance", entry.organisation_guidance);
    section("Example", entry.example);
    section("Supersedes", supersedes_text(entry.supersedes));
    section("Status", entry.status);
    section("Reviewed", entry.reviewed);
    return out.str();
}

std::string render_register(const std::vector<Entry>& entries) {
    std::vector<Entry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) { return a.number < b.number; });
    std::string out = "# Decision register\n\n";
    for (const auto& entry : sorted) {
        out += render_entry(entry);
    }
    return out;
}

std::vector<Entry> parse_register(const std::string& text) {
    std::vector<Entry> entries;
    Entry current;
    bool in_entry = false;
    std::string section_name;
    std::vector<std::string> body;

    const auto close_section = [&]() {
        if (in_entry && !section_name.empty()) {
            std::string joined;
            for (std::size_t i = 0; i < body.size(); ++i) {
                joined += (i == 0 ? "" : "\n") + body[i];
            }
            assign_section(current, section_name, trim(joined));
        }
        section_name.clear();
        body.clear();
    };
    const auto close_entry = [&]() {
        close_section();
        if (in_entry) {
            entries.push_back(current);
        }
        in_entry = false;
        current = Entry{};
    };

    static const std::regex entry_heading("^## Entry ([0-9]+)\\s*$");
    for (const auto& line : split_lines(text)) {
        std::smatch match;
        if (line.rfind("## ", 0) == 0) {
            close_entry();
            if (std::regex_match(line, match, entry_heading)) {
                current.number = std::stoi(match[1].str());
                in_entry = true;
            }
        } else if (line.rfind("### ", 0) == 0) {
            close_section();
            section_name = trim(line.substr(4));
        } else if (!section_name.empty()) {
            body.push_back(line);
        }
    }
    close_entry();

    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) { return a.number < b.number; });
    return entries;
}

std::vector<std::string> validate_entry(const Entry& entry, const std::vector<Entry>& existing) {
    std::vector<std::string> problems;
    if (entry.number < 1) {
        problems.push_back("number must be positive");
    } else if (std::any_of(existing.begin(), existing.end(),
                           [&](const Entry& e) { return e.number == entry.number; })) {
        problems.push_back("number " + std::to_string(entry.number) + " is already used");
    }
    append_field_problems(entry, problems);
    for (const int target : entry.supersedes) {
        const bool found =
            std::any_of(existing.begin(), existing.end(), [&](const Entry& e) { return e.number == target; });
        if (!found) {
            problems.push_back("supersedes entry " + std::to_string(target) + ", which does not exist");
        }
    }
    return problems;
}

std::vector<std::string> consistency_pass(const std::vector<Entry>& entries) {
    std::vector<std::string> findings;

    std::vector<const Entry*> active;
    for (const auto& entry : entries) {
        if (entry.status == "active") {
            active.push_back(&entry);
        }
    }

    for (std::size_t i = 0; i < active.size(); ++i) {
        for (std::size_t j = i + 1; j < active.size(); ++j) {
            if (normalise_trigger(active[i]->trigger_pattern) == normalise_trigger(active[j]->trigger_pattern)) {
                findings.push_back("entries " + std::to_string(active[i]->number) + " and " +
                                   std::to_string(active[j]->number) +
                                   " have the same trigger; one should supersede the other");
            }
        }
    }

    for (const Entry* entry : active) {
        std::set<std::string> reported;
        for (const auto& token : anchor_like_tokens(entry->anchor_guidance)) {
            if (!DeweyEngine::anchor_label(token).has_value() && reported.insert(token).second) {
                findings.push_back("entry " + std::to_string(entry->number) + " names anchor " + token +
                                   ", which is not in the vocabulary");
            }
        }
    }
    return findings;
}

std::vector<int> orphan_candidates(const std::vector<Entry>& entries, const std::map<int, int>& days_since_fired,
                                   int threshold_days) {
    std::vector<int> orphans;
    for (const auto& entry : entries) {
        const auto seen = days_since_fired.find(entry.number);
        if (entry.status == "active" && seen != days_since_fired.end() && seen->second >= threshold_days) {
            orphans.push_back(entry.number);
        }
    }
    std::sort(orphans.begin(), orphans.end());
    return orphans;
}

bool supersede(std::vector<Entry>& entries, int old_number, Entry replacement, std::string& error) {
    error.clear();
    const auto old_entry =
        std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.number == old_number; });
    if (old_entry == entries.end()) {
        error = "entry " + std::to_string(old_number) + " does not exist";
        return false;
    }
    if (old_entry->status != "active") {
        error = "entry " + std::to_string(old_number) + " is already " + old_entry->status;
        return false;
    }
    if (std::find(replacement.supersedes.begin(), replacement.supersedes.end(), old_number) ==
        replacement.supersedes.end()) {
        replacement.supersedes.push_back(old_number);
    }
    replacement.status = "active";
    const auto problems = validate_entry(replacement, entries);
    if (!problems.empty()) {
        error = problems.front();
        return false;
    }
    old_entry->status = "superseded";
    entries.push_back(std::move(replacement));
    return true;
}

}  // namespace DecisionRegister
