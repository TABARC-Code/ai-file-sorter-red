#include "MetadataPlanFile.hpp"

#if __has_include(<jsoncpp/json/json.h>)
#include <jsoncpp/json/json.h>
#elif __has_include(<json/json.h>)
#include <json/json.h>
#else
#error "jsoncpp headers not found. Install jsoncpp development files."
#endif

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>

namespace MetadataPlanFile {

namespace {

std::string write_json(const Json::Value& value, const std::string& indentation) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = indentation;
    builder["emitUTF8"] = true;
    builder["commentStyle"] = "None";
    builder["enableYAMLCompatibility"] = !indentation.empty();
    return Json::writeString(builder, value);
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

bool parse_object(const std::string& text, Json::Value& out) {
    if (has_comment(text)) {
        return false;
    }
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["rejectDupKeys"] = false;
    std::string errors;
    std::istringstream stream(text);
    return Json::parseFromStream(builder, stream, &out, &errors) && out.isObject();
}

Json::Value text_or_null(const std::optional<std::string>& value) {
    return value ? Json::Value(*value) : Json::Value(Json::nullValue);
}

Json::Value string_list(const std::vector<std::string>& items) {
    Json::Value list(Json::arrayValue);
    for (const std::string& item : items) {
        list.append(item);
    }
    return list;
}

Json::Value cover_value(const std::string& cover) {
    Json::Value parsed;
    if (!cover.empty() && parse_object(cover, parsed)) {
        return parsed;
    }
    return Json::Value(Json::nullValue);
}

Json::Value entry_value(const Entry& entry) {
    Json::Value out(Json::objectValue);
    out["file"] = entry.file;
    out["family"] = entry.family;
    out["type"] = entry.type;
    out["fingerprint"]["size"] = static_cast<Json::UInt64>(entry.fingerprint.size);
    out["fingerprint"]["mtime_ns"] = static_cast<Json::Int64>(entry.fingerprint.mtime_ns);
    out["changes"] = Json::Value(Json::arrayValue);
    for (const Change& change : entry.changes) {
        Json::Value item(Json::objectValue);
        item["key"] = change.key;
        item["from"] = text_or_null(change.from);
        item["to"] = change.to;
        item["source"] = change.source;
        out["changes"].append(item);
    }
    out["cover"] = cover_value(entry.cover);
    out["warnings"] = string_list(entry.warnings);
    return out;
}

Json::Value folder_value(const Folder& folder) {
    Json::Value out(Json::objectValue);
    out["dir"] = folder.dir;
    out["changes"] = Json::Value(Json::arrayValue);
    for (const FolderChange& change : folder.changes) {
        Json::Value item(Json::objectValue);
        item["target"] = change.target;
        if (!change.key.empty()) {
            item["key"] = change.key;
        }
        item["from"] = text_or_null(change.from);
        item["to"] = change.to;
        item["source"] = change.source;
        out["changes"].append(item);
    }
    return out;
}

std::string text_of(const Json::Value& object, const char* name) {
    const Json::Value& value = object[name];
    return value.isString() ? value.asString() : std::string();
}

std::vector<std::string> strings_of(const Json::Value& object, const char* name) {
    std::vector<std::string> out;
    const Json::Value& list = object[name];
    if (list.isArray()) {
        for (const Json::Value& item : list) {
            if (item.isString()) {
                out.push_back(item.asString());
            }
        }
    }
    return out;
}

std::size_t count_of(const Json::Value& object, const char* name) {
    const Json::Value& value = object[name];
    return value.isUInt64() ? static_cast<std::size_t>(value.asUInt64()) : 0;
}

/** The part of the version before the first dot, or empty if it is not a plain number. */
std::string major_of(const std::string& version) {
    const std::string major = version.substr(0, version.find('.'));
    const bool digits =
        !major.empty() && std::all_of(major.begin(), major.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    return digits ? major : std::string();
}

/** Empty when the version is acceptable, else the refusal. */
std::string check_version(const Json::Value& root) {
    const std::string version = text_of(root, "version");
    const std::string major = major_of(version);
    if (!major.empty() && major == major_of(kVersion)) {
        return "";
    }
    return "plan is from version " + (major.empty() ? std::string("?") : version) + "; re-run plan";
}

/** Before-value rules shared by both kinds of change: absent or null is none, text is text, anything else is bad. */
bool read_before(const Json::Value& item, std::optional<std::string>& out) {
    const Json::Value& from = item["from"];
    if (from.isNull()) {
        return true;
    }
    if (!from.isString()) {
        return false;
    }
    out = from.asString();
    return true;
}

std::string change_error(const std::string& where, std::size_t number, const std::string& what) {
    return where + ": change " + std::to_string(number) + " " + what;
}

std::string read_entry_changes(const Json::Value& item, const std::string& where, Entry& entry) {
    const Json::Value& list = item["changes"];
    if (list.isNull()) {
        return "";
    }
    if (!list.isArray()) {
        return where + ": changes is not a list";
    }
    std::size_t number = 0;
    for (const Json::Value& raw : list) {
        ++number;
        if (!raw.isObject()) {
            return change_error(where, number, "is not an object");
        }
        Change change;
        change.key = text_of(raw, "key");
        if (change.key.empty()) {
            return change_error(where, number, "has no key");
        }
        if (!raw["to"].isString()) {
            return change_error(where, number, "has no value");
        }
        change.to = raw["to"].asString();
        if (!read_before(raw, change.from)) {
            return change_error(where, number, "has a bad before value");
        }
        change.source = text_of(raw, "source");
        entry.changes.push_back(change);
    }
    return "";
}

std::string read_entry(const Json::Value& item, std::size_t number, Entry& entry) {
    const std::string where = "entry " + std::to_string(number);
    if (!item.isObject()) {
        return where + ": not an object";
    }
    entry.file = text_of(item, "file");
    if (entry.file.empty()) {
        return where + ": missing file";
    }
    entry.family = text_of(item, "family");
    if (entry.family.empty()) {
        return where + ": missing family";
    }
    const Json::Value& print = item["fingerprint"];
    if (!print.isObject() || !print["size"].isUInt64() || !print["mtime_ns"].isInt64()) {
        return where + ": missing fingerprint";
    }
    entry.fingerprint.size = static_cast<std::uintmax_t>(print["size"].asUInt64());
    entry.fingerprint.mtime_ns = static_cast<long long>(print["mtime_ns"].asInt64());
    entry.type = text_of(item, "type");
    entry.warnings = strings_of(item, "warnings");
    if (item["cover"].isObject()) {
        entry.cover = write_json(item["cover"], "");
    }
    return read_entry_changes(item, where, entry);
}

std::string read_folder_changes(const Json::Value& item, const std::string& where, Folder& folder) {
    const Json::Value& list = item["changes"];
    if (list.isNull()) {
        return "";
    }
    if (!list.isArray()) {
        return where + ": changes is not a list";
    }
    std::size_t number = 0;
    for (const Json::Value& raw : list) {
        ++number;
        if (!raw.isObject()) {
            return change_error(where, number, "is not an object");
        }
        FolderChange change;
        change.target = text_of(raw, "target");
        if (change.target.empty()) {
            return change_error(where, number, "has no target");
        }
        if (!raw["to"].isString()) {
            return change_error(where, number, "has no value");
        }
        change.to = raw["to"].asString();
        if (!read_before(raw, change.from)) {
            return change_error(where, number, "has a bad before value");
        }
        change.key = text_of(raw, "key");
        change.source = text_of(raw, "source");
        folder.changes.push_back(change);
    }
    return "";
}

std::string read_folder(const Json::Value& item, std::size_t number, Folder& folder) {
    const std::string where = "folder " + std::to_string(number);
    if (!item.isObject()) {
        return where + ": not an object";
    }
    folder.dir = text_of(item, "dir");
    if (folder.dir.empty()) {
        return where + ": missing dir";
    }
    return read_folder_changes(item, where, folder);
}

}  // namespace

std::string serialise(const Plan& plan) {
    Json::Value root(Json::objectValue);
    root["tool"] = plan.tool;
    root["version"] = plan.version;
    root["created"] = plan.created;
    root["source"] = plan.source;
    root["summary"]["files_scanned"] = static_cast<Json::UInt64>(plan.summary.files_scanned);
    root["summary"]["files_changing"] = static_cast<Json::UInt64>(plan.summary.files_changing);
    root["summary"]["folders_changing"] = static_cast<Json::UInt64>(plan.summary.folders_changing);
    root["warnings"] = string_list(plan.warnings);
    root["entries"] = Json::Value(Json::arrayValue);
    for (const Entry& entry : plan.entries) {
        root["entries"].append(entry_value(entry));
    }
    root["folders"] = Json::Value(Json::arrayValue);
    for (const Folder& folder : plan.folders) {
        root["folders"].append(folder_value(folder));
    }
    return write_json(root, "  ") + "\n";
}

Parsed parse(const std::string& text) {
    Parsed result;
    Json::Value root;
    if (!parse_object(text, root)) {
        result.error = "plan is not a JSON object";
        return result;
    }
    result.error = check_version(root);
    if (!result.error.empty()) {
        return result;
    }
    if (!root["entries"].isArray()) {
        result.error = "plan has no entries list";
        return result;
    }
    if (!root["folders"].isArray()) {
        result.error = "plan has no folders list";
        return result;
    }
    Plan& plan = result.plan;
    plan.version = text_of(root, "version");
    plan.created = text_of(root, "created");
    plan.source = text_of(root, "source");
    plan.warnings = strings_of(root, "warnings");
    plan.summary = {count_of(root["summary"], "files_scanned"), count_of(root["summary"], "files_changing"),
                    count_of(root["summary"], "folders_changing")};
    std::size_t number = 0;
    for (const Json::Value& item : root["entries"]) {
        Entry entry;
        result.error = read_entry(item, ++number, entry);
        if (!result.error.empty()) {
            return result;
        }
        plan.entries.push_back(entry);
    }
    number = 0;
    for (const Json::Value& item : root["folders"]) {
        Folder folder;
        result.error = read_folder(item, ++number, folder);
        if (!result.error.empty()) {
            return result;
        }
        plan.folders.push_back(folder);
    }
    return result;
}

BatchInput batch_input(const Plan& plan) {
    static const std::set<std::string> kTagFamilies = {"id3", "mp4", "vorbis"};
    BatchInput input;
    for (const Entry& entry : plan.entries) {
        if (kTagFamilies.count(entry.family) == 0) {
            input.unsupported.push_back(entry.file + ": no writer for " + entry.family + " files yet");
            continue;
        }
        MetadataBatch::PlannedEntry planned;
        planned.path = entry.file;
        planned.fingerprint = entry.fingerprint;
        for (const Change& change : entry.changes) {
            planned.edits.push_back({change.key, change.to});
        }
        input.entries.push_back(planned);
    }
    return input;
}

std::vector<MetadataFolder::Change> folder_changes(const Folder& folder) {
    std::vector<MetadataFolder::Change> out;
    for (const FolderChange& change : folder.changes) {
        out.push_back({change.target, change.key, change.from, change.to});
    }
    return out;
}

}  // namespace MetadataPlanFile
