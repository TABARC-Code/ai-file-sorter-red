#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FileOrganizerRules.hpp"

/**
 * @brief Lets Dewey anchors steer where media files are shelved, and what the folder manifest says about them.
 *        Audio and video get a shelf named from their anchor ("780-music-and-audio"); everything else keeps the
 *        plain type folders from FileOrganizerRules. The anchor choices follow the deweygraph-file-organizer
 *        vocabulary and the Dewey pass in Metadata Maestro. Nothing here reads a file: tags are handed in.
 */
namespace DeweyShelf {

/** @brief Tag facts for one audio file. Empty means unknown. */
struct AudioFacts {
    std::string artist;
    std::string album;
    std::string genre;
    std::string narrator;
};

/**
 * @brief An anchor guess with the evidence behind it. An empty anchor_code means no guess: the file is not
 *        media this module shelves. dewey_number is the finer number for the manifest; blank when unsure.
 */
struct Guess {
    std::string anchor_code;
    std::string format;
    std::string confidence;
    std::string reason;
    std::string dewey_number;
    std::vector<std::string> context_tags;

    bool empty() const { return anchor_code.empty(); }
};

/**
 * @brief Guess from the file name alone. Extension is weak evidence, so confidence is always "low". Audio is
 *        780.AUD, video 791.VID, and an .m4b is an audiobook, which is not music (anchor 000, tag "audiobook").
 */
Guess guess_from_name(const std::string& file_name);

/**
 * @brief Firm up an audio guess with tags. Any tag raises music to "medium"; a jazz genre makes it "high"
 *        (781.65), rock stays "medium" (781.66), classical leaves the number blank because it is arranged by form
 *        and medium. A narrator, or an audiobook or spoken-word genre, makes it an audiobook. Other guesses pass
 *        through unchanged.
 */
Guess refine_with_tags(const Guess& by_name, const AudioFacts& facts);

/** @brief "DDD-kebab" shelf folder for a guess, or empty for no guess. */
std::string shelf_folder(const Guess& guess);

/**
 * @brief "<shelf>/<artist>/<album>/<file>" for music and audiobooks with those tags; levels with no tag are left
 *        out. Characters that are unsafe in a folder name become hyphens. Video has no sub-levels.
 */
std::string shelf_path(const Guess& guess, const AudioFacts* facts, const std::string& file_name);

/** @brief Folder manifest keys for the guess: dewey (when known), dewey_anchor, dewey_confidence, dewey_note. */
std::vector<std::pair<std::string, std::string>> manifest_sets(const Guess& guess);

struct Item {
    FileOrganizerRules::FileInfo file;
    std::optional<AudioFacts> audio;
};

struct ShelfPlan {
    FileOrganizerRules::Plan plan;
    /** @brief Guess per source name, only for files that were shelved. */
    std::map<std::string, Guess> guesses;
};

/**
 * @brief FileOrganizerRules::plan_by_type, then audio and video moved to their shelves. A shelved file is not
 *        sent to Archive however old it is: a music library is old and still in use. Skips, protected names and
 *        collision handling are the base plan's; destinations never collide.
 */
ShelfPlan plan_by_shelf(const std::vector<Item>& items, std::int64_t now_seconds,
                        const FileOrganizerRules::Options& options,
                        const std::vector<std::string>& existing_names = {});

}  // namespace DeweyShelf
