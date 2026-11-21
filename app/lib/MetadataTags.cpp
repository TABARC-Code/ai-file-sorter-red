#include "MetadataTags.hpp"

#include <commentsframe.h>
#include <fileref.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <textidentificationframe.h>
#include <tpropertymap.h>
#include <tstringlist.h>

#include <cctype>
#include <cstdlib>
#include <map>
#include <regex>
#include <string>

namespace metadata_tags {

namespace {

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string upper_ascii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// Mirrors the three maps in scripts/mm.py.
std::optional<std::string> known_id3(const std::string& key) {
    static const std::map<std::string, std::string> table = {{"title", "TIT2"},
                                                             {"artist", "TPE1"},
                                                             {"album_artist", "TPE2"},
                                                             {"album", "TALB"},
                                                             {"date", "TDRC"},
                                                             {"genre", "TCON"},
                                                             {"composer", "TCOM"},
                                                             {"track", "TRCK"},
                                                             {"disc", "TPOS"},
                                                             {"isrc", "TSRC"},
                                                             {"copyright", "TCOP"},
                                                             {"publisher", "TPUB"},
                                                             {"language", "TLAN"},
                                                             {"comment", "COMM"},
                                                             {"narrator", "TXXX:NARRATOR"},
                                                             {"series", "TXXX:SERIES"},
                                                             {"description", "TXXX:DESCRIPTION"},
                                                             {"synopsis", "TXXX:SYNOPSIS"}};
    const auto found = table.find(key);
    return found == table.end() ? std::nullopt : std::optional<std::string>(found->second);
}

std::optional<std::string> known_mp4(const std::string& key) {
    static const std::string ff = "----:com.apple.iTunes:";
    static const std::map<std::string, std::string> table = {{"title", "\xa9nam"},
                                                             {"artist",
                                                              "\xa9"
                                                              "ART"},
                                                             {"album_artist", "aART"},
                                                             {"album",
                                                              "\xa9"
                                                              "alb"},
                                                             {"date",
                                                              "\xa9"
                                                              "day"},
                                                             {"genre", "\xa9gen"},
                                                             {"composer", "\xa9wrt"},
                                                             {"comment",
                                                              "\xa9"
                                                              "cmt"},
                                                             {"description", "desc"},
                                                             {"synopsis", "ldes"},
                                                             {"copyright", "cprt"},
                                                             {"series", "\xa9grp"},
                                                             {"track", "trkn"},
                                                             {"disc", "disk"},
                                                             {"narrator", ff + "NARRATOR"},
                                                             {"isrc", ff + "ISRC"},
                                                             {"publisher", ff + "PUBLISHER"},
                                                             {"language", ff + "LANGUAGE"}};
    const auto found = table.find(key);
    return found == table.end() ? std::nullopt : std::optional<std::string>(found->second);
}

std::optional<std::string> known_vorbis(const std::string& key) {
    static const std::map<std::string, std::string> table = {
        {"title", "TITLE"},       {"artist", "ARTIST"},       {"album_artist", "ALBUMARTIST"},
        {"album", "ALBUM"},       {"date", "DATE"},           {"genre", "GENRE"},
        {"composer", "COMPOSER"}, {"track", "TRACKNUMBER"},   {"disc", "DISCNUMBER"},
        {"isrc", "ISRC"},         {"comment", "COMMENT"},     {"description", "DESCRIPTION"},
        {"synopsis", "SYNOPSIS"}, {"copyright", "COPYRIGHT"}, {"publisher", "PUBLISHER"},
        {"language", "LANGUAGE"}, {"narrator", "NARRATOR"},   {"series", "SERIES"}};
    const auto found = table.find(key);
    return found == table.end() ? std::nullopt : std::optional<std::string>(found->second);
}

bool is_number_key(const std::string& key) {
    return key == "track" || key == "disc";
}

WriteResult failed(const std::string& why) {
    return {false, why};
}

// Writes ID3v2.3, as mutagen did in mm.py, so older players keep reading the tags.
WriteResult write_id3(const std::string& path, const std::vector<Change>& changes) {
    TagLib::MPEG::File file(path.c_str());
    if (!file.isValid()) {
        return failed("cannot open MP3 file");
    }
    TagLib::ID3v2::Tag* tag = file.ID3v2Tag(true);
    for (const Change& change : changes) {
        const std::string frame = *frame_name(Family::Id3, change.key);
        if (frame == "COMM") {
            tag->removeFrames("COMM");
            if (!change.value.empty()) {
                auto* comment = new TagLib::ID3v2::CommentsFrame(TagLib::String::UTF8);
                comment->setLanguage("eng");
                comment->setDescription("");
                comment->setText(TagLib::String(change.value, TagLib::String::UTF8));
                tag->addFrame(comment);
            }
        } else if (frame.rfind("TXXX:", 0) == 0) {
            const std::string desc = frame.substr(5);
            for (auto* existing : tag->frameList("TXXX")) {
                auto* user = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(existing);
                if (user != nullptr && user->description() == TagLib::String(desc, TagLib::String::UTF8)) {
                    tag->removeFrame(user);
                }
            }
            if (!change.value.empty()) {
                auto* user = new TagLib::ID3v2::UserTextIdentificationFrame(TagLib::String::UTF8);
                user->setDescription(TagLib::String(desc, TagLib::String::UTF8));
                user->setText(TagLib::String(change.value, TagLib::String::UTF8));
                tag->addFrame(user);
            }
        } else {
            tag->removeFrames(frame.c_str());
            if (!change.value.empty()) {
                auto* text = new TagLib::ID3v2::TextIdentificationFrame(frame.c_str(), TagLib::String::UTF8);
                text->setText(TagLib::String(change.value, TagLib::String::UTF8));
                tag->addFrame(text);
            }
        }
    }
    if (!file.save(TagLib::MPEG::File::ID3v2, TagLib::MPEG::File::StripOthers, TagLib::ID3v2::Version::v3)) {
        return failed("save failed");
    }
    return {true, ""};
}

WriteResult write_mp4(const std::string& path, const std::vector<Change>& changes) {
    TagLib::MP4::File file(path.c_str());
    if (!file.isValid() || file.tag() == nullptr) {
        return failed("cannot open MP4 file");
    }
    TagLib::MP4::Tag* tag = file.tag();
    for (const Change& change : changes) {
        const std::string atom = *frame_name(Family::Mp4, change.key);
        if (change.value.empty()) {
            tag->removeItem(TagLib::String(atom, TagLib::String::Latin1));
            continue;
        }
        const TagLib::String name(atom, TagLib::String::Latin1);
        if (is_number_key(change.key)) {
            const auto number = split_number(change.value);
            tag->setItem(name, TagLib::MP4::Item(number->number, number->total));
        } else {
            tag->setItem(name,
                         TagLib::MP4::Item(TagLib::StringList(TagLib::String(change.value, TagLib::String::UTF8))));
        }
    }
    if (!file.save()) {
        return failed("save failed");
    }
    return {true, ""};
}

WriteResult write_vorbis(const std::string& path, const std::vector<Change>& changes) {
    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || ref.file() == nullptr) {
        return failed("cannot open Vorbis or FLAC file");
    }
    TagLib::PropertyMap props = ref.properties();
    for (const Change& change : changes) {
        const std::string key = *frame_name(Family::Vorbis, change.key);
        props.erase(TagLib::String(key, TagLib::String::UTF8));
        if (is_number_key(change.key)) {
            const std::string total_key = change.key == "track" ? "TRACKTOTAL" : "DISCTOTAL";
            props.erase(TagLib::String(total_key, TagLib::String::UTF8));
            if (!change.value.empty()) {
                const auto number = split_number(change.value);
                props[TagLib::String(key, TagLib::String::UTF8)] = TagLib::StringList(std::to_string(number->number));
                if (number->total > 0) {
                    props[TagLib::String(total_key, TagLib::String::UTF8)] =
                        TagLib::StringList(std::to_string(number->total));
                }
            }
        } else if (!change.value.empty()) {
            props[TagLib::String(key, TagLib::String::UTF8)] =
                TagLib::StringList(TagLib::String(change.value, TagLib::String::UTF8));
        }
    }
    ref.file()->setProperties(props);
    if (!ref.save()) {
        return failed("save failed");
    }
    return {true, ""};
}

std::optional<std::string> read_id3(const std::string& path, const std::string& key) {
    TagLib::MPEG::File file(path.c_str());
    if (!file.isValid() || file.ID3v2Tag() == nullptr) {
        return std::nullopt;
    }
    const std::string frame = *frame_name(Family::Id3, key);
    TagLib::ID3v2::Tag* tag = file.ID3v2Tag();
    if (frame == "COMM") {
        const auto frames = tag->frameList("COMM");
        if (frames.isEmpty()) {
            return std::nullopt;
        }
        return frames.front()->toString().to8Bit(true);
    }
    if (frame.rfind("TXXX:", 0) == 0) {
        const std::string desc = frame.substr(5);
        for (auto* existing : tag->frameList("TXXX")) {
            auto* user = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(existing);
            if (user != nullptr && user->description() == TagLib::String(desc, TagLib::String::UTF8)) {
                return user->fieldList().back().to8Bit(true);
            }
        }
        return std::nullopt;
    }
    const auto frames = tag->frameList(frame.c_str());
    if (frames.isEmpty()) {
        return std::nullopt;
    }
    return frames.front()->toString().to8Bit(true);
}

std::optional<std::string> read_mp4(const std::string& path, const std::string& key) {
    TagLib::MP4::File file(path.c_str());
    if (!file.isValid() || file.tag() == nullptr) {
        return std::nullopt;
    }
    const std::string atom = *frame_name(Family::Mp4, key);
    const TagLib::MP4::Item item = file.tag()->item(TagLib::String(atom, TagLib::String::Latin1));
    if (!item.isValid()) {
        return std::nullopt;
    }
    if (is_number_key(key)) {
        const auto pair = item.toIntPair();
        const std::string number = std::to_string(pair.first);
        return pair.second > 0 ? number + "/" + std::to_string(pair.second) : number;
    }
    const auto list = item.toStringList();
    if (list.isEmpty()) {
        return std::nullopt;
    }
    return list.front().to8Bit(true);
}

std::optional<std::string> read_vorbis(const std::string& path, const std::string& key) {
    TagLib::FileRef ref(path.c_str());
    if (ref.isNull()) {
        return std::nullopt;
    }
    const TagLib::PropertyMap props = ref.properties();
    const std::string field = *frame_name(Family::Vorbis, key);
    const auto found = props.find(TagLib::String(field, TagLib::String::UTF8));
    if (found == props.end() || found->second.isEmpty()) {
        return std::nullopt;
    }
    std::string value = found->second.front().to8Bit(true);
    if (is_number_key(key)) {
        const std::string total_key = key == "track" ? "TRACKTOTAL" : "DISCTOTAL";
        const auto total = props.find(TagLib::String(total_key, TagLib::String::UTF8));
        if (total != props.end() && !total->second.isEmpty()) {
            value += "/" + total->second.front().to8Bit(true);
        }
    }
    return value;
}

}  // namespace

Family family_of(const std::string& path) {
    static const std::map<std::string, Family> by_extension = {
        {".mp3", Family::Id3}, {".wav", Family::Id3},     {".aif", Family::Id3},    {".aiff", Family::Id3},
        {".mp4", Family::Mp4}, {".m4a", Family::Mp4},     {".m4b", Family::Mp4},    {".m4v", Family::Mp4},
        {".mov", Family::Mp4}, {".flac", Family::Vorbis}, {".ogg", Family::Vorbis}, {".opus", Family::Vorbis}};
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return Family::Unsupported;
    }
    const auto found = by_extension.find(lower_ascii(path.substr(dot)));
    return found == by_extension.end() ? Family::Unsupported : found->second;
}

std::optional<std::string> frame_name(Family family, const std::string& key) {
    switch (family) {
        case Family::Id3:
            if (auto known = known_id3(key)) {
                return known;
            }
            return "TXXX:" + upper_ascii(key);
        case Family::Mp4:
            if (auto known = known_mp4(key)) {
                return known;
            }
            return "----:com.apple.iTunes:" + upper_ascii(key);
        case Family::Vorbis:
            if (auto known = known_vorbis(key)) {
                return known;
            }
            return upper_ascii(key);
        case Family::Unsupported:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<SplitNumber> split_number(const std::string& text) {
    static const std::regex pattern(R"(^\s*(\d+)\s*(?:/\s*(\d+))?\s*$)");
    std::smatch match;
    if (!std::regex_match(text, match, pattern)) {
        return std::nullopt;
    }
    SplitNumber out;
    out.number = std::atoi(match[1].str().c_str());
    out.total = match[2].matched ? std::atoi(match[2].str().c_str()) : 0;
    return out;
}

WriteResult write_tags(const std::string& path, const std::vector<Change>& changes) {
    const Family family = family_of(path);
    if (family == Family::Unsupported) {
        return failed("no tag writer for this file type");
    }
    for (const Change& change : changes) {
        if (is_number_key(change.key) && !change.value.empty() && !split_number(change.value)) {
            return failed("track/disc must look like 3 or 3/12, got \"" + change.value + "\"");
        }
    }
    switch (family) {
        case Family::Id3:
            return write_id3(path, changes);
        case Family::Mp4:
            return write_mp4(path, changes);
        case Family::Vorbis:
            return write_vorbis(path, changes);
        case Family::Unsupported:
            break;
    }
    return failed("no tag writer for this file type");
}

std::optional<std::string> read_tag(const std::string& path, const std::string& key) {
    switch (family_of(path)) {
        case Family::Id3:
            return read_id3(path, key);
        case Family::Mp4:
            return read_mp4(path, key);
        case Family::Vorbis:
            return read_vorbis(path, key);
        case Family::Unsupported:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace metadata_tags
