#include "WikiLint.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace WikiLint {

namespace {

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

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
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

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** Index of the closing fence, or 0 when the text has no complete frontmatter block. */
std::size_t frontmatter_end(const std::vector<std::string>& lines) {
    if (lines.empty() || trim(lines[0]) != "---") {
        return 0;
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (trim(lines[i]) == "---") {
            return i;
        }
    }
    return 0;
}

std::string unquote(const std::string& value) {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string join_list(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        out += out.empty() ? item : ", " + item;
    }
    return out;
}

std::string inline_list(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream in(value.substr(1, value.size() - 2));
    std::string item;
    while (std::getline(in, item, ',')) {
        item = unquote(trim(item));
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return join_list(items);
}

bool split_key(const std::string& line, std::string& key, std::string& value) {
    if (line.empty() || std::isspace(static_cast<unsigned char>(line[0])) != 0) {
        return false;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    key = lower(trim(line.substr(0, colon)));
    value = trim(line.substr(colon + 1));
    return true;
}

bool list_item(const std::string& line, std::string& item) {
    const std::string t = trim(line);
    if (t.empty() || t[0] != '-' || (t.size() > 1 && t[1] != ' ')) {
        return false;
    }
    item = unquote(trim(t.substr(1)));
    return true;
}

std::string without_inline_code(const std::string& line) {
    std::string out;
    bool in_code = false;
    for (const char c : line) {
        if (c == '`') {
            in_code = !in_code;
        } else if (!in_code) {
            out.push_back(c);
        }
    }
    return out;
}

std::string link_target(std::string inner) {
    for (const char cut : {'|', '#', '^'}) {
        inner = inner.substr(0, inner.find(cut));
    }
    return trim(inner);
}

void collect_links(const std::string& line, std::vector<std::string>& links) {
    const std::string text = without_inline_code(line);
    std::size_t pos = 0;
    while ((pos = text.find("[[", pos)) != std::string::npos) {
        const auto close = text.find("]]", pos + 2);
        if (close == std::string::npos) {
            return;
        }
        const bool embed = pos > 0 && text[pos - 1] == '!';
        const std::string target = link_target(text.substr(pos + 2, close - pos - 2));
        if (!embed && !target.empty() && std::find(links.begin(), links.end(), target) == links.end()) {
            links.push_back(target);
        }
        pos = close + 2;
    }
}

struct Names {
    std::string stem;
    std::string path_no_ext;
};

Names names_for(const std::string& path) {
    std::string base = lower(path);
    if (ends_with(base, ".md")) {
        base.resize(base.size() - 3);
    }
    const auto slash = base.rfind('/');
    return {slash == std::string::npos ? base : base.substr(slash + 1), base};
}

bool is_hub(const Names& names) {
    return names.stem == "index" || names.stem == "readme" || names.stem == "log";
}

constexpr std::size_t kNone = static_cast<std::size_t>(-1);
constexpr std::size_t kAmbiguous = static_cast<std::size_t>(-2);

class Resolver {
   public:
    explicit Resolver(const std::vector<Names>& names) : names_(names) {}

    /** Index of the one matching page, kNone when nothing matches and kAmbiguous when several do. */
    std::size_t resolve(const std::string& target) const {
        std::string key = lower(target);
        if (ends_with(key, ".md")) {
            key.resize(key.size() - 3);
        }
        const bool by_path = key.find('/') != std::string::npos;
        std::size_t found = kNone;
        for (std::size_t i = 0; i < names_.size(); ++i) {
            const bool hit = by_path ? (names_[i].path_no_ext == key || ends_with(names_[i].path_no_ext, "/" + key))
                                     : names_[i].stem == key;
            if (hit) {
                if (found != kNone) {
                    return kAmbiguous;
                }
                found = i;
            }
        }
        return found;
    }

   private:
    const std::vector<Names>& names_;
};

struct PageLinks {
    std::vector<std::string> broken;
    std::vector<std::string> ambiguous;
    std::set<std::size_t> out;
};

void build_link_graph(const std::vector<Page>& pages, const Resolver& resolver, std::vector<PageLinks>& links,
                      std::vector<std::set<std::size_t>>& inbound) {
    for (std::size_t i = 0; i < pages.size(); ++i) {
        for (const auto& target : extract_wikilinks(pages[i].text)) {
            const std::size_t hit = resolver.resolve(target);
            if (hit == kNone) {
                links[i].broken.push_back(target);
            } else if (hit == kAmbiguous) {
                links[i].ambiguous.push_back(target);
            } else if (hit != i) {
                links[i].out.insert(hit);
                inbound[hit].insert(i);
            }
        }
    }
}

void add_link_issues(const std::vector<Page>& pages, const std::vector<Names>& names, std::size_t i,
                     const std::vector<PageLinks>& links, const std::vector<std::set<std::size_t>>& inbound,
                     std::vector<Issue>& issues) {
    const std::string& path = pages[i].path;
    for (const auto& target : links[i].broken) {
        issues.push_back({Severity::Warning, path, "broken-link", "link to [[" + target + "]] goes nowhere"});
    }
    for (const auto& target : links[i].ambiguous) {
        issues.push_back({Severity::Warning, path, "ambiguous-link",
                          "[[" + target + "]] matches more than one page; use the folder path"});
    }
    if (is_hub(names[i])) {
        return;
    }
    for (const std::size_t source : inbound[i]) {
        if (!is_hub(names[source]) && links[i].out.count(source) == 0) {
            issues.push_back({Severity::Warning, path, "non-reciprocal",
                              pages[source].path + " links here but this page does not link back"});
        }
    }
    if (links[i].out.empty() && inbound[i].empty()) {
        issues.push_back({Severity::Warning, path, "orphan", "no links in or out"});
    }
}

void add_frontmatter_issues(const Page& page, const std::vector<std::string>& required, std::vector<Issue>& issues) {
    const Frontmatter fm = parse_frontmatter(page.text);
    if (!fm.present) {
        issues.push_back({Severity::Error, page.path, "no-frontmatter", "page has no frontmatter block"});
        return;
    }
    for (const auto& field : required) {
        const auto it = fm.fields.find(field);
        if (it == fm.fields.end() || trim(it->second).empty()) {
            issues.push_back({Severity::Error, page.path, "missing-field", "missing field: " + field});
        }
    }
}

}  // namespace

const std::vector<std::string>& required_fields() {
    static const std::vector<std::string> fields = {"title", "type", "tags", "summary"};
    return fields;
}

const std::vector<std::string>& dewey_required_fields() {
    static const std::vector<std::string> fields = {"id",
                                                    "title",
                                                    "source_path",
                                                    "format",
                                                    "anchor_code",
                                                    "anchor_label",
                                                    "material_role",
                                                    "classification_confidence",
                                                    "worthiness_class",
                                                    "status"};
    return fields;
}

Frontmatter parse_frontmatter(const std::string& text) {
    Frontmatter fm;
    const auto lines = split_lines(text);
    const std::size_t end = frontmatter_end(lines);
    if (end == 0) {
        return fm;
    }
    fm.present = true;
    for (std::size_t i = 1; i < end; ++i) {
        std::string key;
        std::string value;
        if (!split_key(lines[i], key, value)) {
            continue;
        }
        if (value.empty()) {
            std::vector<std::string> items;
            std::string item;
            while (i + 1 < end && list_item(lines[i + 1], item)) {
                items.push_back(item);
                ++i;
            }
            value = join_list(items);
        } else if (value.front() == '[' && value.back() == ']') {
            value = inline_list(value);
        } else {
            value = unquote(value);
        }
        fm.fields[key] = value;
    }
    return fm;
}

std::vector<std::string> extract_wikilinks(const std::string& text) {
    std::vector<std::string> links;
    const auto lines = split_lines(text);
    const std::size_t end = frontmatter_end(lines);
    bool in_fence = false;
    for (std::size_t i = end == 0 ? 0 : end + 1; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.rfind("```", 0) == 0 || t.rfind("~~~", 0) == 0) {
            in_fence = !in_fence;
        } else if (!in_fence) {
            collect_links(lines[i], links);
        }
    }
    return links;
}

std::vector<Issue> lint(const std::vector<Page>& pages, const std::vector<std::string>& required) {
    std::vector<Names> names;
    for (const auto& page : pages) {
        names.push_back(names_for(page.path));
    }
    const Resolver resolver(names);

    std::vector<PageLinks> links(pages.size());
    std::vector<std::set<std::size_t>> inbound(pages.size());
    build_link_graph(pages, resolver, links, inbound);

    std::vector<Issue> issues;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (!is_hub(names[i])) {
            add_frontmatter_issues(pages[i], required, issues);
        }
        add_link_issues(pages, names, i, links, inbound, issues);
    }
    return issues;
}

std::size_t count_errors(const std::vector<Issue>& issues) {
    return static_cast<std::size_t>(
        std::count_if(issues.begin(), issues.end(), [](const Issue& i) { return i.severity == Severity::Error; }));
}

}  // namespace WikiLint
