#include "MediaCheck.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace MediaCheck {

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

std::string tag_value(const MediaFacts& facts, const std::string& key) {
    const auto it = facts.tags.find(key);
    return it == facts.tags.end() ? std::string() : trim(it->second);
}

Finding make(Severity severity, const std::string& file, const std::string& code, const std::string& message) {
    return Finding{severity, file, code, message};
}

void append_unverified(const MediaFacts& f, std::vector<Finding>& out) {
    std::vector<std::string> unknown;
    if (!f.bitrate_mode) {
        unknown.push_back("bit rate mode");
    }
    if (!f.bitrate_kbps) {
        unknown.push_back("bit rate");
    }
    if (!f.sample_rate_hz) {
        unknown.push_back("sample rate");
    }
    if (!f.duration_seconds) {
        unknown.push_back("duration");
    }
    if (!f.rms_db) {
        unknown.push_back("rms");
    }
    if (!f.peak_db) {
        unknown.push_back("peak");
    }
    if (!unknown.empty()) {
        std::string list;
        for (const auto& name : unknown) {
            list += list.empty() ? name : ", " + name;
        }
        out.push_back(make(Severity::Warning, f.file, "unverified", "not measured: " + list));
    }
}

void check_acx_encoding(const MediaFacts& f, std::vector<Finding>& out) {
    const auto error = [&](const char* code, const std::string& message) {
        out.push_back(make(Severity::Error, f.file, code, message));
    };
    if (f.extension != "mp3") {
        error("acx-format", "ACX takes MP3 only, this is ." + f.extension);
    }
    if (f.bitrate_mode && lower(*f.bitrate_mode) != "cbr") {
        error("acx-bitrate-mode", "ACX needs constant bit rate, this is " + *f.bitrate_mode);
    }
    if (f.bitrate_kbps && *f.bitrate_kbps < kAcxMinKbps) {
        error("acx-bitrate", "ACX needs 192 kbps or higher, this is " + std::to_string(*f.bitrate_kbps));
    }
    if (f.sample_rate_hz && *f.sample_rate_hz != kAcxSampleRateHz) {
        error("acx-sample-rate", "ACX needs 44.1 kHz, this is " + std::to_string(*f.sample_rate_hz) + " Hz");
    }
}

void check_acx_levels(const MediaFacts& f, std::vector<Finding>& out) {
    const auto error = [&](const char* code, const std::string& message) {
        out.push_back(make(Severity::Error, f.file, code, message));
    };
    if (f.duration_seconds && *f.duration_seconds > kAcxMaxSeconds) {
        error("acx-duration", "ACX files must be no longer than 120 minutes");
    }
    if (f.rms_db && (*f.rms_db < kAcxRmsMinDb || *f.rms_db > kAcxRmsMaxDb)) {
        error("acx-rms", "RMS " + std::to_string(*f.rms_db) + " dB is outside -23 to -18 dB");
    }
    if (f.peak_db && *f.peak_db > kAcxPeakMaxDb) {
        error("acx-peak", "peak " + std::to_string(*f.peak_db) + " dB is above -3 dB");
    }
}

void check_acx_limits(const MediaFacts& f, std::vector<Finding>& out) {
    check_acx_encoding(f, out);
    check_acx_levels(f, out);
    append_unverified(f, out);
}

/** The number before any "/" in a track tag, or nullopt when it is not a plain number. */
std::optional<int> track_number(const std::string& value) {
    const std::string head = trim(value.substr(0, value.find('/')));
    if (head.empty() || head.size() > 6 ||
        !std::all_of(head.begin(), head.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    return std::stoi(head);
}

void check_channels(const std::vector<MediaFacts>& files, Profile profile, std::vector<Finding>& out) {
    std::set<int> counts;
    for (const auto& f : files) {
        if (f.channels) {
            counts.insert(*f.channels);
        }
    }
    if (counts.size() < 2) {
        return;
    }
    std::string list;
    for (const int c : counts) {
        list += list.empty() ? std::to_string(c) : ", " + std::to_string(c);
    }
    out.push_back(make(profile == Profile::Acx ? Severity::Error : Severity::Warning, "", "mixed-channels",
                       "files differ in channel count (" + list + ")"));
}

void check_tracks(const std::vector<MediaFacts>& files, std::vector<Finding>& out) {
    std::map<int, std::vector<std::string>> by_track;
    for (const auto& f : files) {
        const auto number = track_number(tag_value(f, "track"));
        if (number) {
            by_track[*number].push_back(f.file);
        }
    }
    for (const auto& [number, names] : by_track) {
        if (names.size() < 2) {
            continue;
        }
        std::string list;
        for (const auto& name : names) {
            list += list.empty() ? name : ", " + name;
        }
        out.push_back(make(Severity::Warning, "", "duplicate-track",
                           "track " + std::to_string(number) + " appears more than once: " + list));
    }
}

void check_albums(const std::vector<MediaFacts>& files, std::vector<Finding>& out) {
    std::vector<std::string> seen;
    for (const auto& f : files) {
        const std::string album = tag_value(f, "album");
        if (!album.empty() && std::find(seen.begin(), seen.end(), album) == seen.end()) {
            seen.push_back(album);
        }
    }
    if (seen.size() < 2) {
        return;
    }
    std::string list;
    for (const auto& album : seen) {
        list += list.empty() ? "\"" + album + "\"" : ", \"" + album + "\"";
    }
    out.push_back(make(Severity::Warning, "", "album-drift", "album names differ: " + list));
}

}  // namespace

std::optional<Profile> parse_profile(const std::string& name) {
    const std::string key = lower(name);
    if (key == "music") {
        return Profile::Music;
    }
    if (key == "audiobook") {
        return Profile::Audiobook;
    }
    if (key == "acx") {
        return Profile::Acx;
    }
    if (key == "film") {
        return Profile::Film;
    }
    if (key == "image") {
        return Profile::Image;
    }
    return std::nullopt;
}

std::vector<std::string> required_tags(Profile profile) {
    switch (profile) {
        case Profile::Film:
            return {"title"};
        case Profile::Image:
            return {"title", "artist", "copyright", "description"};
        case Profile::Music:
        case Profile::Audiobook:
        case Profile::Acx:
            break;
    }
    return {"title", "artist", "album", "track"};
}

std::vector<Finding> check_file(const MediaFacts& facts, Profile profile) {
    std::vector<Finding> out;
    for (const auto& key : required_tags(profile)) {
        if (tag_value(facts, key).empty()) {
            out.push_back(make(Severity::Warning, facts.file, "missing-tag", "missing tag: " + key));
        }
    }
    if (profile == Profile::Acx) {
        check_acx_limits(facts, out);
    }
    return out;
}

std::vector<Finding> check_set(const std::vector<MediaFacts>& files, Profile profile) {
    std::vector<Finding> out;
    if (files.empty()) {
        return out;
    }
    check_channels(files, profile, out);
    check_tracks(files, out);
    check_albums(files, out);
    if (profile == Profile::Acx) {
        out.push_back(make(Severity::Warning, "", "noise-floor-not-measured",
                           "noise floor (ACX maximum -60 dB) is not measured here; check it by ear and with a meter"));
    }
    return out;
}

std::vector<std::string> propose_edit_for(const std::vector<DropRecord>& history, int min_count) {
    std::map<std::string, int> counts;
    for (const auto& drop : history) {
        ++counts[drop.container + ":" + drop.key];
    }
    std::vector<std::string> out;
    for (const auto& [key, count] : counts) {
        if (count >= min_count) {
            out.push_back(key);
        }
    }
    return out;
}

std::map<std::string, std::string> audiobookshelf_sidecars(const std::string& description,
                                                           const std::string& narrator) {
    std::map<std::string, std::string> files;
    if (!trim(description).empty()) {
        files["desc.txt"] = trim(description) + "\n";
    }
    if (!trim(narrator).empty()) {
        files["reader.txt"] = trim(narrator) + "\n";
    }
    return files;
}

}  // namespace MediaCheck
