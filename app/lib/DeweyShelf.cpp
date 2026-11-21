#include "DeweyShelf.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "DeweyEngine.hpp"

namespace DeweyShelf {

namespace {

const char* const kExtensionOnly = "extension only";
const char* const kTagsPresent = "tags present";
const char* const kAudiobookReason = "audiobook: the subject is the book's, not music; class it with the work";
const char* const kClassicalReason =
    "classical music is arranged by form and medium (782, 784 to 788); number left blank";

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extension_of(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return "";
    }
    return lower(name.substr(dot + 1));
}

bool contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

bool blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool in_set(const std::string& value, const std::set<std::string>& set) {
    return set.count(value) > 0;
}

Guess make(const char* anchor, const char* format, const std::string& reason) {
    Guess guess;
    guess.anchor_code = anchor;
    guess.format = format;
    guess.confidence = "low";
    guess.reason = reason;
    return guess;
}

Guess audiobook_guess() {
    Guess guess = make("000", "AUD", kAudiobookReason);
    guess.context_tags = {"audiobook"};
    return guess;
}

bool is_audiobook_genre(const std::string& genre) {
    const std::string g = lower(genre);
    return contains(g, "audiobook") || contains(g, "audio book") || contains(g, "spoken");
}

bool is_audiobook(const Guess& guess) {
    return std::find(guess.context_tags.begin(), guess.context_tags.end(), "audiobook") != guess.context_tags.end();
}

std::string clean_level(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const bool spacing = c == '\t' || c == '\r' || c == '\n';
        const bool unsafe =
            static_cast<unsigned char>(c) < 0x20 || std::string("/\\:*?\"<>|").find(c) != std::string::npos;
        out += spacing ? ' ' : (unsafe ? '-' : c);
    }
    const char* const edge = " \t\r\n.";
    const auto first = out.find_first_not_of(edge);
    if (first == std::string::npos) {
        return "";
    }
    return out.substr(first, out.find_last_not_of(edge) - first + 1);
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : "/") + part;
    }
    return out;
}

Guess guess_for(const Item& item) {
    const Guess by_name = guess_from_name(item.file.name);
    return item.audio ? refine_with_tags(by_name, *item.audio) : by_name;
}

}  // namespace

Guess guess_from_name(const std::string& file_name) {
    static const std::set<std::string> kAudio = {"mp3", "m4a",  "flac", "ogg", "opus",
                                                 "wav", "aiff", "aif",  "aac", "wma"};
    static const std::set<std::string> kVideo = {"mp4", "mov", "mkv", "avi", "webm", "m4v"};
    const std::string ext = extension_of(file_name);
    if (ext == "m4b") {
        Guess guess = audiobook_guess();
        guess.reason = std::string(kExtensionOnly) + ": .m4b is an audiobook container; class it with the work";
        return guess;
    }
    if (in_set(ext, kAudio)) {
        return make("780.AUD", "AUD", kExtensionOnly);
    }
    if (in_set(ext, kVideo)) {
        return make("791.VID", "VID", kExtensionOnly);
    }
    return Guess{};
}

Guess refine_with_tags(const Guess& by_name, const AudioFacts& facts) {
    if (by_name.format != "AUD") {
        return by_name;
    }
    if (is_audiobook(by_name) || !blank(facts.narrator) || is_audiobook_genre(facts.genre)) {
        return audiobook_guess();
    }
    if (blank(facts.artist) && blank(facts.album) && blank(facts.genre)) {
        return by_name;
    }
    Guess guess = by_name;
    guess.confidence = "medium";
    guess.reason = kTagsPresent;
    guess.dewey_number = "780";
    const std::string genre = lower(facts.genre);
    if (contains(genre, "jazz")) {
        guess.confidence = "high";
        guess.dewey_number = "781.65";
    } else if (contains(genre, "rock")) {
        guess.dewey_number = "781.66";
    } else if (contains(genre, "classical")) {
        guess.dewey_number.clear();
        guess.reason = kClassicalReason;
    }
    return guess;
}

std::string shelf_folder(const Guess& guess) {
    if (guess.empty()) {
        return "";
    }
    const std::string cls = guess.anchor_code.substr(0, 3);
    if (is_audiobook(guess)) {
        return DeweyEngine::folder_name(cls, "audiobooks");
    }
    return DeweyEngine::folder_name(cls, DeweyEngine::anchor_label(guess.anchor_code).value_or("unclassified"));
}

std::string shelf_path(const Guess& guess, const AudioFacts* facts, const std::string& file_name) {
    if (guess.empty()) {
        return "";
    }
    std::vector<std::string> parts = {shelf_folder(guess)};
    if (guess.format == "AUD" && facts != nullptr) {
        for (const std::string& level : {clean_level(facts->artist), clean_level(facts->album)}) {
            if (!level.empty()) {
                parts.push_back(level);
            }
        }
    }
    parts.push_back(file_name);
    return join(parts);
}

std::vector<std::pair<std::string, std::string>> manifest_sets(const Guess& guess) {
    std::vector<std::pair<std::string, std::string>> sets;
    if (guess.empty()) {
        return sets;
    }
    if (!guess.dewey_number.empty()) {
        sets.emplace_back("dewey", guess.dewey_number);
    }
    sets.emplace_back("dewey_anchor", guess.anchor_code);
    sets.emplace_back("dewey_confidence", guess.confidence);
    sets.emplace_back("dewey_note", guess.reason);
    return sets;
}

ShelfPlan plan_by_shelf(const std::vector<Item>& items, std::int64_t now_seconds,
                        const FileOrganizerRules::Options& options, const std::vector<std::string>& existing_names) {
    std::vector<FileOrganizerRules::FileInfo> files;
    std::map<std::string, const Item*> by_name;
    for (const Item& item : items) {
        files.push_back(item.file);
        by_name.emplace(item.file.name, &item);
    }
    ShelfPlan out;
    out.plan = FileOrganizerRules::plan_by_type(files, now_seconds, options, existing_names);
    std::set<std::string> taken(existing_names.begin(), existing_names.end());
    auto is_taken = [&](const std::string& name) { return taken.count(name) > 0; };
    for (auto& move : out.plan.moves) {
        const Item& item = *by_name.at(move.from);
        const Guess guess = guess_for(item);
        if (guess.empty()) {
            continue;
        }
        const std::string name =
            options.clean_names ? FileOrganizerRules::clean_download_artifacts(move.from) : move.from;
        move.to =
            FileOrganizerRules::unique_name(shelf_path(guess, item.audio ? &*item.audio : nullptr, name), is_taken);
        taken.insert(move.to);
        out.guesses[move.from] = guess;
    }
    return out;
}

}  // namespace DeweyShelf
