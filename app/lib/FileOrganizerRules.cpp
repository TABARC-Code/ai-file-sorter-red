#include "FileOrganizerRules.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <unordered_map>

namespace FileOrganizerRules {

namespace {

constexpr std::int64_t kSecondsPerDay = 86400;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return text;
}

// Position of the extension dot within the last path segment, or npos when there is none.
std::size_t extension_dot(const std::string& name) {
    const auto slash = name.find_last_of('/');
    const std::size_t start = slash == std::string::npos ? 0 : slash + 1;
    const auto dot = name.find_last_of('.');
    return (dot == std::string::npos || dot <= start) ? std::string::npos : dot;
}

const std::unordered_map<std::string, Category>& extension_map() {
    static const std::unordered_map<std::string, Category> map = [] {
        std::unordered_map<std::string, Category> built;
        const std::vector<std::pair<Category, std::vector<const char*>>> groups = {
            {Category::Documents, {"pdf", "docx", "doc", "txt", "rtf", "odt", "md"}},
            {Category::Images, {"jpg", "jpeg", "png", "gif", "svg", "webp", "heic", "bmp", "tif", "tiff"}},
            {Category::Videos, {"mp4", "mov", "mkv", "avi", "webm", "m4v"}},
            {Category::Archives, {"zip", "tar", "gz", "tgz", "7z", "rar", "bz2", "xz"}},
            {Category::Installers, {"dmg", "pkg", "exe", "msi", "deb", "rpm", "appimage"}},
            {Category::Code, {"py", "js", "ts", "cpp", "hpp", "c", "h", "java", "rs", "go", "sh", "json"}},
            {Category::Spreadsheets, {"xlsx", "xls", "csv", "ods"}},
            {Category::Presentations, {"pptx", "ppt", "key", "odp"}}};
        for (const auto& [category, extensions] : groups) {
            for (const char* extension : extensions) {
                built[extension] = category;
            }
        }
        return built;
    }();
    return map;
}

std::string escape_field(const std::string& text) {
    std::string out;
    for (char ch : text) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            default:
                out.push_back(ch);
        }
    }
    return out;
}

std::string unescape_field(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            out.push_back(text[i] == 't' ? '\t' : (text[i] == 'n' ? '\n' : text[i]));
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool is_protected(const std::string& name, const std::vector<std::string>& prefixes) {
    const std::string lowered = lower(name);
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& prefix) {
        return !prefix.empty() && lowered.rfind(lower(prefix), 0) == 0;
    });
}

// Reason this entry is left alone, or an empty string when it should be planned.
std::string skip_reason(const FileInfo& file, const Options& options) {
    if (file.is_directory) {
        return "folder left in place";
    }
    if (file.name.empty() || file.name.front() == '.') {
        return "hidden file left in place";
    }
    if (is_protected(file.name, options.protected_prefixes)) {
        return "protected name";
    }
    return "";
}

std::string destination_for(const FileInfo& file, std::int64_t now_seconds, const Options& options) {
    const std::string name = options.clean_names ? clean_download_artifacts(file.name) : file.name;
    if (is_archive_candidate(file.mtime_seconds, now_seconds, options.archive_after_days)) {
        return "Archive/" + year_folder(file.mtime_seconds) + "/" + name;
    }
    return category_folder(category_for(file.name)) + "/" + name;
}

}  // namespace

Category category_for(const std::string& file_name) {
    const auto dot = extension_dot(file_name);
    if (dot == std::string::npos) {
        return Category::Other;
    }
    const auto found = extension_map().find(lower(file_name.substr(dot + 1)));
    return found == extension_map().end() ? Category::Other : found->second;
}

std::string category_folder(Category category) {
    switch (category) {
        case Category::Documents:
            return "Documents";
        case Category::Images:
            return "Images";
        case Category::Videos:
            return "Videos";
        case Category::Archives:
            return "Archives";
        case Category::Installers:
            return "Installers";
        case Category::Code:
            return "Code";
        case Category::Spreadsheets:
            return "Spreadsheets";
        case Category::Presentations:
            return "Presentations";
        case Category::Other:
            break;
    }
    return "To-Sort";
}

std::string clean_download_artifacts(const std::string& file_name) {
    const auto dot = extension_dot(file_name);
    const std::string stem = dot == std::string::npos ? file_name : file_name.substr(0, dot);
    const std::string extension = dot == std::string::npos ? "" : file_name.substr(dot);

    static const std::regex tail(R"((\s\(\d+\)|[ _-]copy(\s\d+)?|[ _-]v\d+|[ _-]final)+$)", std::regex::icase);
    const std::string cleaned = std::regex_replace(stem, tail, "");
    const bool nothing_left = cleaned.find_first_not_of(" \t") == std::string::npos;
    return nothing_left ? file_name : cleaned + extension;
}

std::string hyphenate_spaces(const std::string& name) {
    static const std::regex spaces(" +");
    return std::regex_replace(name, spaces, "-");
}

bool is_archive_candidate(std::int64_t mtime_seconds, std::int64_t now_seconds, int archive_after_days) {
    return now_seconds - mtime_seconds >= static_cast<std::int64_t>(archive_after_days) * kSecondsPerDay;
}

namespace {

struct Civil {
    int year;
    int month;
};

// Howard Hinnant's days-to-civil conversion, so the result does not depend on the platform's gmtime.
Civil civil_from_epoch(std::int64_t epoch_seconds) {
    std::int64_t days = epoch_seconds / kSecondsPerDay;
    if (epoch_seconds % kSecondsPerDay < 0) {
        --days;
    }
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t day_of_era = days - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_index = (5 * day_of_year + 2) / 153;
    const int month = static_cast<int>(month_index < 10 ? month_index + 3 : month_index - 9);
    const int year = static_cast<int>(year_of_era + era * 400 + (month <= 2 ? 1 : 0));
    return {year, month};
}

std::string two_digits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

}  // namespace

std::string date_folder(std::int64_t epoch_seconds) {
    static const char* const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                          "July",    "August",   "September", "October", "November", "December"};
    const Civil civil = civil_from_epoch(epoch_seconds);
    return std::to_string(civil.year) + "/" + two_digits(civil.month) + "-" + kMonths[civil.month - 1];
}

std::string year_folder(std::int64_t epoch_seconds) {
    return std::to_string(civil_from_epoch(epoch_seconds).year);
}

std::string unique_name(const std::string& desired, const std::function<bool(const std::string&)>& exists) {
    if (!exists(desired)) {
        return desired;
    }
    const auto dot = extension_dot(desired);
    const std::string stem = dot == std::string::npos ? desired : desired.substr(0, dot);
    const std::string extension = dot == std::string::npos ? "" : desired.substr(dot);
    for (int counter = 2;; ++counter) {
        const std::string candidate = stem + "-" + std::to_string(counter) + extension;
        if (!exists(candidate)) {
            return candidate;
        }
    }
}

Plan plan_by_type(const std::vector<FileInfo>& files, std::int64_t now_seconds, const Options& options,
                  const std::vector<std::string>& existing_names) {
    Plan plan;
    std::set<std::string> taken(existing_names.begin(), existing_names.end());
    auto is_taken = [&](const std::string& name) { return taken.count(name) > 0; };

    for (const auto& file : files) {
        const std::string reason = skip_reason(file, options);
        if (!reason.empty()) {
            plan.skipped.push_back(file.name + ": " + reason);
            continue;
        }
        const std::string target = unique_name(destination_for(file, now_seconds, options), is_taken);
        taken.insert(target);
        plan.moves.push_back({file.name, target});
    }
    return plan;
}

Plan undo_plan(const Plan& plan) {
    Plan undo;
    for (auto it = plan.moves.rbegin(); it != plan.moves.rend(); ++it) {
        undo.moves.push_back({it->to, it->from});
    }
    return undo;
}

std::string serialize_log(const Plan& plan) {
    std::string out;
    for (const auto& move : plan.moves) {
        out += escape_field(move.from) + "\t" + escape_field(move.to) + "\n";
    }
    return out;
}

Plan parse_log(const std::string& text) {
    Plan plan;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) {
            continue;
        }
        plan.moves.push_back({unescape_field(line.substr(0, tab)), unescape_field(line.substr(tab + 1))});
    }
    return plan;
}

}  // namespace FileOrganizerRules
