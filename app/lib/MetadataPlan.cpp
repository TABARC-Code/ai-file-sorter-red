#include "MetadataPlan.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace MetadataPlan {

namespace {

const std::set<std::string> kImageExtensions = {"jpg", "jpeg", "png", "gif", "tif", "tiff", "webp", "heic"};
const std::set<std::string> kTaggableExtensions = {"mp3", "wav", "aiff", "flac", "ogg", "opus",
                                                   "m4a", "m4b", "mp4",  "mov",  "mkv", "webm"};
const std::set<std::string> kReadOnlyExtensions = {"avi", "ts", "wma", "aac"};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return text;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string base_name(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string parent_name(const std::string& path) {
    const auto last = path.find_last_of("/\\");
    if (last == std::string::npos) {
        return "";
    }
    const auto before = path.find_last_of("/\\", last == 0 ? 0 : last - 1);
    const std::size_t start = (before == std::string::npos || before >= last) ? 0 : before + 1;
    return path.substr(start, last - start);
}

std::string extension_of(const std::string& path) {
    const std::string name = base_name(path);
    const auto dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? "" : lower(name.substr(dot + 1));
}

std::string stem_of(const std::string& path) {
    const std::string name = base_name(path);
    const auto dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

bool is_digit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

std::size_t digit_run_end(const std::string& text, std::size_t start) {
    std::size_t end = start;
    while (end < text.size() && is_digit(text[end])) {
        ++end;
    }
    return end;
}

// Negative when the number in a is smaller, positive when larger, zero when equal.
int compare_numbers(const std::string& a, const std::string& b) {
    const auto strip = [](const std::string& digits) {
        const auto first = digits.find_first_not_of('0');
        return first == std::string::npos ? std::string("0") : digits.substr(first);
    };
    const std::string x = strip(a);
    const std::string y = strip(b);
    if (x.size() != y.size()) {
        return x.size() < y.size() ? -1 : 1;
    }
    return x.compare(y);
}

std::string pad(int value, int width) {
    std::string digits = std::to_string(value);
    if (static_cast<int>(digits.size()) < width) {
        digits.insert(0, static_cast<std::size_t>(width) - digits.size(), '0');
    }
    return digits;
}

// Handles the text between braces. Nullopt for anything that is not a known token.
std::optional<std::string> expand_one(const std::string& token, const TokenContext& context) {
    if (token == "total") {
        return std::to_string(context.total);
    }
    if (token == "stem") {
        return context.stem;
    }
    if (token == "dir") {
        return context.dir;
    }
    if (token == "n") {
        return std::to_string(context.n);
    }
    if (token.rfind("n:", 0) == 0) {
        std::string width_text = token.substr(2);
        if (width_text.size() > 1 && width_text.front() == '0') {
            width_text.erase(0, 1);  // "{n:02}" and "{n:2}" mean the same width
        }
        if (width_text.empty() || width_text.size() > 2 ||
            !std::all_of(width_text.begin(), width_text.end(), is_digit)) {
            return std::nullopt;
        }
        const int width = std::stoi(width_text);
        return width < 1 ? std::nullopt : std::optional<std::string>(pad(context.n, width));
    }
    return std::nullopt;
}

bool wants_extension(const std::string& extension, const std::set<std::string>& selected) {
    if (!selected.empty()) {
        return selected.count(extension) > 0;
    }
    return kImageExtensions.count(extension) == 0;
}

// Reason a path is left out of the plan, or an empty string when it is kept.
std::string skip_reason(const std::string& path, const std::set<std::string>& selected) {
    const std::string extension = extension_of(path);
    if (kReadOnlyExtensions.count(extension) > 0) {
        return "read-only format";
    }
    const bool is_image = kImageExtensions.count(extension) > 0;
    if (kTaggableExtensions.count(extension) == 0 && !is_image) {
        return "not a taggable format";
    }
    if (!wants_extension(extension, selected)) {
        return is_image && selected.empty() ? "image (opt in with an extensions filter)"
                                            : "not in the selected extensions";
    }
    return "";
}

void apply_value(Entry& entry, const std::string& field, const std::string& value) {
    if (value == kClearMarker) {
        entry.clear.push_back(field);
    } else {
        entry.set[field] = value;
    }
}

Plan refuse(const std::string& message) {
    Plan plan;
    plan.error = message;
    return plan;
}

}  // namespace

bool natural_less(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    int zero_tie = 0;  // first place where equal numbers had different leading zeros: fewer zeros first
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            const std::size_t end_a = digit_run_end(a, i);
            const std::size_t end_b = digit_run_end(b, j);
            const int order = compare_numbers(a.substr(i, end_a - i), b.substr(j, end_b - j));
            if (order != 0) {
                return order < 0;
            }
            if (zero_tie == 0 && end_a - i != end_b - j) {
                zero_tie = end_a - i < end_b - j ? -1 : 1;
            }
            i = end_a;
            j = end_b;
            continue;
        }
        const int left = std::tolower(static_cast<unsigned char>(a[i]));
        const int right = std::tolower(static_cast<unsigned char>(b[j]));
        if (left != right) {
            return left < right;
        }
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size()) {
        return i >= a.size();  // the shorter one comes first
    }
    if (zero_tie != 0) {
        return zero_tie < 0;
    }
    return a < b;  // same text ignoring case: fall back to the raw bytes
}

std::vector<std::string> natural_sort(std::vector<std::string> names) {
    std::stable_sort(names.begin(), names.end(), natural_less);
    return names;
}

std::optional<std::string> expand_tokens(const std::string& pattern, const TokenContext& context) {
    std::string out;
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        const auto open = pattern.find('{', pos);
        if (open == std::string::npos) {
            out += pattern.substr(pos);
            break;
        }
        out += pattern.substr(pos, open - pos);
        const auto close = pattern.find('}', open);
        if (close == std::string::npos) {
            return std::nullopt;
        }
        const auto value = expand_one(pattern.substr(open + 1, close - open - 1), context);
        if (!value) {
            return std::nullopt;
        }
        out += *value;
        pos = close + 1;
    }
    return out;
}

std::optional<std::pair<std::string, std::string>> parse_set(const std::string& text) {
    const auto equals = text.find('=');
    if (equals == std::string::npos || equals == 0) {
        return std::nullopt;
    }
    return std::make_pair(text.substr(0, equals), text.substr(equals + 1));
}

Plan build_plan(const std::vector<std::string>& paths, const Request& request) {
    if (trim(request.source).empty()) {
        return refuse("no value without a source: a source is required");
    }
    std::set<std::string> fields;
    for (const auto& entry : request.sets) {
        if (!fields.insert(entry.first).second) {
            return refuse("field '" + entry.first + "' is set more than once");
        }
    }

    std::set<std::string> selected;
    for (std::string extension : request.only_extensions) {
        if (!extension.empty() && extension.front() == '.') {
            extension.erase(0, 1);
        }
        if (!extension.empty()) {
            selected.insert(lower(extension));
        }
    }

    Plan plan;
    std::vector<std::string> kept;
    for (const auto& path : paths) {
        const std::string reason = skip_reason(path, selected);
        if (reason.empty()) {
            kept.push_back(path);
        } else {
            plan.skipped.push_back(path + ": " + reason);
        }
    }
    kept = natural_sort(kept);

    if (kept.empty()) {
        plan.warnings.push_back("no files matched");
        return plan;
    }
    if (request.sets.empty()) {
        plan.warnings.push_back("nothing to change: no fields were set");
        return plan;
    }

    for (std::size_t index = 0; index < kept.size(); ++index) {
        TokenContext context{static_cast<int>(index) + 1, static_cast<int>(kept.size()), stem_of(kept[index]),
                             parent_name(kept[index])};
        Entry entry;
        entry.path = kept[index];
        for (const auto& [field, pattern] : request.sets) {
            const auto value = expand_tokens(pattern, context);
            if (!value) {
                return refuse("bad template for field '" + field + "': " + pattern);
            }
            apply_value(entry, field, *value);
        }
        plan.entries.push_back(std::move(entry));
    }
    return plan;
}

std::vector<std::vector<std::string>> parse_csv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool in_quotes = false;
    bool row_has_content = false;

    const auto end_cell = [&]() {
        row.push_back(cell);
        cell.clear();
    };
    const auto end_row = [&]() {
        end_cell();
        if (row_has_content) {
            rows.push_back(row);
        }
        row.clear();
        row_has_content = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_quotes) {
            if (ch == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                cell.push_back('"');
                ++i;
            } else if (ch == '"') {
                in_quotes = false;
            } else {
                cell.push_back(ch);
            }
        } else if (ch == '"') {
            in_quotes = true;
            row_has_content = true;
        } else if (ch == ',') {
            end_cell();
            row_has_content = true;
        } else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            end_row();
        } else {
            cell.push_back(ch);
            row_has_content = true;
        }
    }
    if (row_has_content || !cell.empty()) {
        row_has_content = true;
        end_row();
    }
    return rows;
}

Plan plan_from_csv(const std::string& csv_text, const std::string& source) {
    if (trim(source).empty()) {
        return refuse("no value without a source: a source is required");
    }
    const auto rows = parse_csv(csv_text);
    if (rows.empty() || lower(trim(rows[0][0])) != "path") {
        return refuse("the first column of the CSV must be named path");
    }

    Plan plan;
    std::set<std::string> seen;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        const std::string path = row[0];
        if (path.empty()) {
            continue;
        }
        if (!seen.insert(path).second) {
            return refuse("path '" + path + "' appears more than once");
        }
        Entry entry;
        entry.path = path;
        for (std::size_t c = 1; c < rows[0].size() && c < row.size(); ++c) {
            const std::string field = trim(rows[0][c]);
            if (!field.empty() && !row[c].empty()) {
                apply_value(entry, field, row[c]);
            }
        }
        if (entry.set.empty() && entry.clear.empty()) {
            plan.skipped.push_back(path + ": no changes");
        } else {
            plan.entries.push_back(std::move(entry));
        }
    }
    return plan;
}

}  // namespace MetadataPlan
