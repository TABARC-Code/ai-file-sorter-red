#include "MetadataFolder.hpp"

#if __has_include(<jsoncpp/json/json.h>)
#include <jsoncpp/json/json.h>
#elif __has_include(<json/json.h>)
#include <json/json.h>
#else
#error "jsoncpp headers not found. Install jsoncpp development files."
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>

#include "MetadataApply.hpp"

namespace fs = std::filesystem;

namespace MetadataFolder {

namespace {

const char* const kUnreadable = "unreadable metadata-maestro.json";

std::string trim(const std::string& text) {
    const char* const blanks = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string::npos) {
        return "";
    }
    return text.substr(first, text.find_last_not_of(blanks) - first + 1);
}

bool read_file(const fs::path& path, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::optional<std::string> read_trimmed(const fs::path& path) {
    std::string text;
    if (!read_file(path, text)) {
        return std::nullopt;
    }
    return trim(text);
}

bool round_trips(double number, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, number);
    return std::strtod(buffer, nullptr) == number;
}

bool all_numbers_round_trip(const Json::Value& value, int digits) {
    if (value.isDouble()) {
        return round_trips(value.asDouble(), digits);
    }
    for (const Json::Value& child : value) {
        if (!all_numbers_round_trip(child, digits)) {
            return false;
        }
    }
    return true;
}

/** Fewest digits (15 to 17) that bring every real number in the document back exactly, so 0.1 stays 0.1. */
int number_digits(const Json::Value& value) {
    for (int digits = 15; digits < 17; ++digits) {
        if (all_numbers_round_trip(value, digits)) {
            return digits;
        }
    }
    return 17;
}

std::string write_json(const Json::Value& value, const std::string& indentation) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = indentation;
    builder["emitUTF8"] = true;
    builder["commentStyle"] = "None";
    builder["enableYAMLCompatibility"] = !indentation.empty();
    builder["precision"] = number_digits(value);
    return Json::writeString(builder, value);
}

std::string compact_json(const Json::Value& value) {
    return write_json(value, "");
}

/** JSON has no comments, but the reader lets them through. A slash outside a string can only be one. */
bool has_comment(const std::string& text) {
    bool in_string = false;
    bool escaped = false;
    for (const char c : text) {
        if (in_string && escaped) {
            escaped = false;
        } else if (in_string) {
            escaped = c == '\\';
            in_string = escaped || c != '"';
        } else if (c == '"') {
            in_string = true;
        } else if (c == '/') {
            return true;
        }
    }
    return false;
}

/** Parse a manifest body into an object. Blank text and a bare null are an empty object. False if unreadable. */
bool parse_manifest(const std::string& body, Json::Value& out) {
    const std::string text = trim(body);
    out = Json::Value(Json::objectValue);
    if (text.empty() || text == "null") {
        return true;
    }
    if (has_comment(text)) {
        return false;
    }
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["rejectDupKeys"] = false;
    std::string errors;
    Json::Value parsed;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &parsed, &errors) || !parsed.isObject()) {
        return false;
    }
    out = parsed;
    return true;
}

bool is_cover_name(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::set<std::string> kNames = {"cover.jpg",  "cover.jpeg",  "cover.png",
                                                 "folder.jpg", "folder.jpeg", "folder.png"};
    return kNames.count(lower) > 0;
}

std::vector<std::string> find_covers(const fs::path& dir) {
    std::vector<std::string> covers;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (is_cover_name(name)) {
            covers.push_back(name);
        }
    }
    std::sort(covers.begin(), covers.end());
    return covers;
}

void fill_manifest(State& state, const Json::Value& object) {
    for (const std::string& key : object.getMemberNames()) {
        const Json::Value& value = object[key];
        state.manifest[key] =
            value.isString() ? ManifestValue{value.asString(), true} : ManifestValue{compact_json(value), false};
    }
}

std::set<std::string> distinct(const std::vector<std::string>& values, bool drop_empty) {
    std::set<std::string> out;
    for (const std::string& v : values) {
        if (!drop_empty || !v.empty()) {
            out.insert(v);
        }
    }
    return out;
}

// ---------------------------------------------------------------- applying

bool plain_name(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\") == std::string::npos;
}

std::string text_with_newline(const std::string& text) {
    return !text.empty() && text.back() == '\n' ? text : text + "\n";
}

bool has_marker(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path.string() + MetadataApply::kBackupSuffix, ec) ||
           fs::exists(path.string() + MetadataApply::kCreatedSuffix, ec);
}

/** Back the file up (or mark it as new), then write or delete it. Empty string is an error message-free success. */
std::string write_text(const fs::path& path, const std::string& text, bool no_backup) {
    std::error_code ec;
    if (!no_backup) {
        if (fs::exists(path, ec)) {
            const std::string backup = path.string() + MetadataApply::kBackupSuffix;
            fs::copy_file(path, backup, fs::copy_options::none, ec);
            if (ec) {
                return "could not make a backup of " + path.filename().string();
            }
            fs::last_write_time(backup, fs::last_write_time(path, ec), ec);
        } else {
            std::ofstream marker(path.string() + MetadataApply::kCreatedSuffix, std::ios::binary);
            if (!marker) {
                return "could not make a backup of " + path.filename().string();
            }
        }
    }
    if (text.empty()) {
        fs::remove(path, ec);
        return "";
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text_with_newline(text);
    out.flush();
    return out ? "" : "could not write " + path.filename().string();
}

Json::Value merged_manifest(const Json::Value& current, const std::vector<Change>& changes, const std::string& stamp,
                            const std::string& tool) {
    Json::Value merged = current;
    for (const Change& c : changes) {
        if (c.to.empty()) {
            merged.removeMember(c.key);
        } else {
            merged[c.key] = c.to;
        }
    }
    merged["_updated"] = stamp;
    merged["_tool"] = tool;
    return merged;
}

/** The last change per sidecar wins, in order of first appearance. Manifest changes are kept apart. */
struct Split {
    std::vector<std::string> names;
    std::map<std::string, std::string> text;
    std::vector<Change> manifest;
    std::string error;
};

Split split_changes(const std::vector<Change>& changes) {
    Split out;
    for (const Change& c : changes) {
        if (!plain_name(c.target)) {
            out.error = "bad target name: " + c.target;
            return out;
        }
        if (c.target == kManifest) {
            out.manifest.push_back(c);
            continue;
        }
        if (out.text.count(c.target) == 0) {
            out.names.push_back(c.target);
        }
        out.text[c.target] = c.to;
    }
    return out;
}

std::string marker_problem(const fs::path& dir, const Split& split, bool no_backup) {
    if (no_backup) {
        return "";
    }
    std::vector<std::string> names = split.names;
    if (!split.manifest.empty()) {
        names.push_back(kManifest);
    }
    for (const std::string& name : names) {
        if (has_marker(dir / name)) {
            return "backup exists for " + name + "; restore or clean first";
        }
    }
    return "";
}

}  // namespace

State read_folder(const std::string& dir) {
    State state;
    state.dir = dir;
    const fs::path base(dir);
    state.reader = read_trimmed(base / kReaderFile);
    state.desc = read_trimmed(base / kDescFile);
    state.cover_files = find_covers(base);
    std::string body;
    Json::Value object;
    if (read_file(base / kManifest, body)) {
        if (parse_manifest(body, object)) {
            fill_manifest(state, object);
        } else {
            state.manifest_error = kUnreadable;
        }
    }
    return state;
}

SidecarPlan plan_sidecar(const std::string& folder_name, const std::string& file, const std::string& key,
                         const std::vector<std::string>& planned, const std::vector<std::string>& existing,
                         const std::optional<std::string>& current) {
    SidecarPlan plan;
    const std::string label = folder_name + "/" + file + " skipped: ";
    std::set<std::string> wanted;
    for (const std::string& value : planned) {
        wanted.insert(trim(value));
    }
    const std::set<std::string> have = distinct(existing, true);
    std::string value;
    if (wanted.size() == 1) {
        value = *wanted.begin();
    } else if (!wanted.empty()) {
        plan.warning = label + "files disagree on planned " + key;
        return plan;
    } else if (have.size() == 1) {
        value = trim(*have.begin());
    } else {
        plan.warning = label + (have.empty() ? "no " : "files disagree on ") + key;
        return plan;
    }
    const bool nothing_to_clear = value.empty() && !current.has_value();
    if (!nothing_to_clear && !(current.has_value() && *current == value)) {
        plan.change = Change{file, "", current, value};
    }
    return plan;
}

ManifestPlan plan_manifest(const State& state, const std::vector<std::pair<std::string, std::string>>& sets) {
    ManifestPlan plan;
    if (sets.empty()) {
        return plan;
    }
    if (!state.manifest_error.empty()) {
        plan.error = state.manifest_error + " in " + state.dir;
        return plan;
    }
    for (const auto& [key, value] : sets) {
        const auto found = state.manifest.find(key);
        const bool present = found != state.manifest.end();
        const bool same = present && found->second.is_string && found->second.text == value;
        if (same || (!present && value.empty())) {
            continue;
        }
        plan.changes.push_back(
            {kManifest, key, present ? std::optional<std::string>(found->second.text) : std::nullopt, value});
    }
    return plan;
}

ApplyResult apply_folder(const std::string& dir, const std::vector<Change>& changes, bool no_backup,
                         const std::string& stamp, const std::string& tool) {
    ApplyResult result;
    const fs::path base(dir);
    std::error_code ec;
    if (!fs::is_directory(base, ec)) {
        result.error = "folder gone";
        return result;
    }
    const Split split = split_changes(changes);
    result.error = split.error.empty() ? marker_problem(base, split, no_backup) : split.error;
    if (!result.error.empty()) {
        return result;
    }
    Json::Value current;
    std::string body;
    if (!split.manifest.empty() && read_file(base / kManifest, body) && !parse_manifest(body, current)) {
        result.error = kUnreadable;
        return result;
    }
    for (const std::string& name : split.names) {
        result.error = write_text(base / name, split.text.at(name), no_backup);
        if (!result.error.empty()) {
            return result;
        }
        result.written.push_back(name);
    }
    if (!split.manifest.empty()) {
        const Json::Value merged =
            merged_manifest(current.isObject() ? current : Json::Value(Json::objectValue), split.manifest, stamp, tool);
        result.error = write_text(base / kManifest, write_json(merged, "  "), no_backup);
        if (!result.error.empty()) {
            return result;
        }
        result.written.push_back(kManifest);
    }
    result.ok = true;
    return result;
}

}  // namespace MetadataFolder
