#include "MetadataLedger.hpp"

#if __has_include(<jsoncpp/json/json.h>)
#include <jsoncpp/json/json.h>
#elif __has_include(<json/json.h>)
#include <json/json.h>
#else
#error "jsoncpp headers not found. Install jsoncpp development files."
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include "MediaCheck.hpp"

namespace fs = std::filesystem;

namespace MetadataLedger {

namespace {

const std::size_t kRecentNotes = 10;

std::string write_line(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    builder["commentStyle"] = "None";
    return Json::writeString(builder, value);
}

Json::Value string_array(const std::vector<std::string>& values) {
    Json::Value out(Json::arrayValue);
    for (const std::string& value : values) {
        out.append(value);
    }
    return out;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/** ".flac" from "song.FLAC", or empty when there is no extension to speak of. */
std::string extension_of(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? "" : lower(name.substr(dot));
}

/** "name.ext: key" into a drop. Empty ext or key means it cannot be used. */
Json::Value drop_from(const std::string& line) {
    Json::Value drop(Json::objectValue);
    const auto split = line.rfind(": ");
    if (split == std::string::npos) {
        return Json::Value();
    }
    const std::string ext = extension_of(line.substr(0, split));
    const std::string key = line.substr(split + 2);
    if (ext.size() < 2 || key.empty()) {
        return Json::Value();
    }
    drop["ext"] = ext;
    drop["key"] = key;
    return drop;
}

Json::Value event(const std::string& at, const char* name) {
    Json::Value entry(Json::objectValue);
    entry["at"] = at;
    entry["event"] = name;
    return entry;
}

bool parse_object(const std::string& line, Json::Value& out) {
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    std::string errors;
    std::istringstream stream(line);
    return Json::parseFromStream(builder, stream, &out, &errors) && out.isObject();
}

bool is_text_pair(const Json::Value& item, const char* a, const char* b) {
    return item.isObject() && item[a].isString() && item[b].isString();
}

void count_drops(const Json::Value& entry, Lessons& out, std::vector<MediaCheck::DropRecord>& history) {
    for (const Json::Value& drop : entry["drops"]) {
        if (is_text_pair(drop, "ext", "key")) {
            ++out.container_drops[drop["ext"].asString() + ":" + drop["key"].asString()];
            history.push_back({drop["ext"].asString(), drop["key"].asString()});
        }
    }
}

void count_verdicts(const Json::Value& entry, Lessons& out) {
    if (entry["verdict"].isString()) {
        for (const Json::Value& key : entry["keys"]) {
            if (key.isString()) {
                ++out.verdicts[key.asString() + ":" + entry["verdict"].asString()];
            }
        }
    }
}

void read_entry(const Json::Value& entry, Lessons& out, std::vector<MediaCheck::DropRecord>& history,
                std::vector<std::string>& notes) {
    const std::string kind = entry["event"].isString() ? entry["event"].asString() : "";
    if (kind == "apply") {
        count_drops(entry, out, history);
    } else if (kind == "verdict") {
        count_verdicts(entry, out);
        if (entry["note"].isString() && !entry["note"].asString().empty()) {
            notes.push_back(entry["note"].asString());
        }
    } else if (kind == "restore" && entry["files"].isArray()) {
        out.restores += entry["files"].size();
    }
}

}  // namespace

std::string default_path(const std::string& base_folder) {
    return (fs::path(base_folder) / ".metadata-maestro" / "ledger.jsonl").string();
}

std::string apply_line(const std::string& at, const ApplyRecord& record) {
    Json::Value entry = event(at, "apply");
    entry["plan"] = record.plan;
    Json::Value summary(Json::objectValue);
    summary["ok"] = static_cast<Json::UInt64>(record.ok);
    summary["already_done"] = static_cast<Json::UInt64>(record.already_done);
    summary["failed"] = static_cast<Json::UInt64>(record.failures.size());
    summary["folders_written"] = static_cast<Json::UInt64>(record.folders_written);
    summary["dropped"] = string_array(record.dropped);
    entry["summary"] = summary;
    Json::Value drops(Json::arrayValue);
    for (const std::string& line : record.dropped) {
        const Json::Value drop = drop_from(line);
        if (!drop.isNull()) {
            drops.append(drop);
        }
    }
    entry["drops"] = drops;
    Json::Value failed(Json::arrayValue);
    for (const auto& failure : record.failures) {
        Json::Value item(Json::objectValue);
        item["file"] = failure.path;
        item["error"] = failure.error;
        failed.append(item);
    }
    entry["failed"] = failed;
    return write_line(entry);
}

std::string restore_line(const std::string& at, const std::vector<std::string>& files) {
    Json::Value entry = event(at, "restore");
    entry["files"] = string_array(files);
    return write_line(entry);
}

std::string verdict_line(const std::string& at, const VerdictRecord& record) {
    if (record.verdict != "accept" && record.verdict != "reject") {
        return "";
    }
    const std::set<std::string> unique(record.keys.begin(), record.keys.end());
    Json::Value entry = event(at, "verdict");
    entry["plan"] = record.plan;
    entry["verdict"] = record.verdict;
    entry["keys"] = string_array(std::vector<std::string>(unique.begin(), unique.end()));
    entry["note"] = record.note;
    return write_line(entry);
}

std::string append_line(const std::string& path, const std::string& line) {
    if (line.empty()) {
        return "";
    }
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << line << '\n';
    out.flush();
    return out ? "" : "could not write the ledger";
}

std::string read_ledger(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Lessons lessons(const std::string& ledger_text, const std::string& ledger_path) {
    Lessons out;
    out.ledger = ledger_path;
    std::vector<MediaCheck::DropRecord> history;
    std::vector<std::string> notes;
    std::istringstream lines(ledger_text);
    std::string line;
    Json::Value entry;
    while (std::getline(lines, line)) {
        if (parse_object(line, entry)) {
            ++out.entries;
            read_entry(entry, out, history, notes);
        }
    }
    out.propose_edit_for = MediaCheck::propose_edit_for(history);
    const std::size_t from = notes.size() > kRecentNotes ? notes.size() - kRecentNotes : 0;
    out.recent_notes.assign(notes.begin() + static_cast<std::ptrdiff_t>(from), notes.end());
    return out;
}

}  // namespace MetadataLedger
