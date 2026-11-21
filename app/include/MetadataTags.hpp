#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * @brief Native tag reading and writing, built on the vendored TagLib. Keys map to ID3 v2.3 frames, iTunes MP4 atoms
 * and Vorbis comment fields.
 */
namespace MetadataTags {

enum class Family { Id3, Mp4, Vorbis, Unsupported };

/** @brief Container family from the file extension, case-insensitive. */
Family family_of(const std::string& path);

/** @brief ID3 frame id, MP4 atom name or Vorbis field name for a key; nullopt for an unsupported family. */
std::optional<std::string> frame_name(Family family, const std::string& key);

struct SplitNumber {
    int number{0};
    int total{0};
};

/** @brief "3" or "3/12" with optional spaces. Nullopt for anything else. */
std::optional<SplitNumber> split_number(const std::string& text);

struct Change {
    std::string key;
    /** @brief Empty removes the tag. */
    std::string value;
};

struct WriteResult {
    bool ok{false};
    std::string error;
};

/** @brief Applies changes in place and saves. Track and disc split "n/total". */
WriteResult write_tags(const std::string& path, const std::vector<Change>& changes);

/** @brief Current value for a key, or nullopt when absent or the file cannot be read. Track returns "n/total". */
std::optional<std::string> read_tag(const std::string& path, const std::string& key);

}  // namespace MetadataTags
