#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Pre-flight checks on media facts, by destination profile. Pure rules: the facts are measured
 *        elsewhere (ffprobe, exiftool, a tag reader). Anything not measured is reported as unverified,
 *        never as a pass.
 */
namespace MediaCheck {

enum class Profile { Music, Audiobook, Acx, Film, Image };

/** @brief "music", "audiobook", "acx", "film" or "image", any case. */
std::optional<Profile> parse_profile(const std::string& name);

/** @brief ACX submission limits. Level bounds are inclusive. */
inline constexpr int kAcxMinKbps = 192;
inline constexpr int kAcxSampleRateHz = 44100;
inline constexpr double kAcxMaxSeconds = 120.0 * 60.0;
inline constexpr double kAcxRmsMinDb = -23.0;
inline constexpr double kAcxRmsMaxDb = -18.0;
inline constexpr double kAcxPeakMaxDb = -3.0;

enum class Severity { Error, Warning };

struct Finding {
    Severity severity{Severity::Warning};
    /** @brief File name, or empty for a finding about the whole set. */
    std::string file;
    std::string code;
    std::string message;
};

struct MediaFacts {
    std::string file;
    /** @brief Lower case, no dot. */
    std::string extension;
    /** @brief "cbr" or "vbr" when known. */
    std::optional<std::string> bitrate_mode;
    std::optional<int> bitrate_kbps;
    std::optional<int> sample_rate_hz;
    std::optional<int> channels;
    std::optional<double> duration_seconds;
    /** @brief Only set when levels were actually measured. */
    std::optional<double> rms_db;
    std::optional<double> peak_db;
    /** @brief Lower-case keys: title, artist, album, track, copyright, description. */
    std::map<std::string, std::string> tags;
};

/** @brief Tags a file should carry for this destination. Missing ones are warnings, not errors. */
std::vector<std::string> required_tags(Profile profile);

/** @brief Findings for one file. */
std::vector<Finding> check_file(const MediaFacts& facts, Profile profile);

/**
 * @brief Findings across a set: mixed mono and stereo, repeated track numbers, drifting album names,
 *        and for ACX the reminder that noise floor is never measured here.
 */
std::vector<Finding> check_set(const std::vector<MediaFacts>& files, Profile profile);

/** @brief A key the writer dropped from a container, as recorded in the ledger. */
struct DropRecord {
    std::string container;
    std::string key;
};

/** @brief "container:key" pairs dropped at least min_count times, sorted. Worth a proposed edit to the field map. */
std::vector<std::string> propose_edit_for(const std::vector<DropRecord>& history, int min_count = 3);

/** @brief Audiobookshelf sidecars: desc.txt from the description, reader.txt from the narrator. Blank values write
 * nothing. */
std::map<std::string, std::string> audiobookshelf_sidecars(const std::string& description, const std::string& narrator);

}  // namespace MediaCheck
