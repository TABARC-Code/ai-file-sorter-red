#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "MediaCheck.hpp"

using namespace MediaCheck;

namespace {

MediaFacts good_music() {
    MediaFacts f;
    f.file = "01 song.flac";
    f.extension = "flac";
    f.channels = 2;
    f.tags = {{"title", "Song"}, {"artist", "Band"}, {"album", "Record"}, {"track", "1/10"}};
    return f;
}

MediaFacts good_acx() {
    MediaFacts f;
    f.file = "ch01.mp3";
    f.extension = "mp3";
    f.bitrate_mode = "cbr";
    f.bitrate_kbps = 192;
    f.sample_rate_hz = 44100;
    f.channels = 1;
    f.duration_seconds = 600;
    f.rms_db = -20;
    f.peak_db = -4;
    f.tags = {{"title", "Chapter 1"}, {"artist", "A. Author"}, {"album", "The Book"}, {"track", "1/3"}};
    return f;
}

bool has_code(const std::vector<Finding>& findings, const std::string& code) {
    for (const auto& f : findings) {
        if (f.code == code) {
            return true;
        }
    }
    return false;
}

const Finding* find_code(const std::vector<Finding>& findings, const std::string& code) {
    for (const auto& f : findings) {
        if (f.code == code) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Profiles parse by name in any case", "[media]") {
    REQUIRE(parse_profile("music") == Profile::Music);
    REQUIRE(parse_profile("AudioBook") == Profile::Audiobook);
    REQUIRE(parse_profile("ACX") == Profile::Acx);
    REQUIRE(parse_profile("film") == Profile::Film);
    REQUIRE(parse_profile("Image") == Profile::Image);
    REQUIRE_FALSE(parse_profile("podcast").has_value());
    REQUIRE_FALSE(parse_profile("").has_value());
}

TEST_CASE("Each profile names the tags it wants", "[media]") {
    REQUIRE(required_tags(Profile::Music) == std::vector<std::string>{"title", "artist", "album", "track"});
    REQUIRE(required_tags(Profile::Audiobook) == std::vector<std::string>{"title", "artist", "album", "track"});
    REQUIRE(required_tags(Profile::Acx) == std::vector<std::string>{"title", "artist", "album", "track"});
    REQUIRE(required_tags(Profile::Film) == std::vector<std::string>{"title"});
    REQUIRE(required_tags(Profile::Image) == std::vector<std::string>{"title", "artist", "copyright", "description"});
}

TEST_CASE("A well tagged music file has no findings", "[media]") {
    REQUIRE(check_file(good_music(), Profile::Music).empty());
}

TEST_CASE("Missing and blank tags are warnings that name the file and tag", "[media]") {
    MediaFacts f = good_music();
    f.tags.erase("album");
    f.tags["title"] = "   ";
    const auto findings = check_file(f, Profile::Music);
    REQUIRE(findings.size() == 2);
    for (const auto& finding : findings) {
        REQUIRE(finding.code == "missing-tag");
        REQUIRE(finding.severity == Severity::Warning);
        REQUIRE(finding.file == "01 song.flac");
    }
    REQUIRE(findings[0].message.find("title") != std::string::npos);
    REQUIRE(findings[1].message.find("album") != std::string::npos);
}

TEST_CASE("A compliant ACX file with measured levels has no findings", "[media]") {
    REQUIRE(check_file(good_acx(), Profile::Acx).empty());
}

TEST_CASE("ACX rejects anything that is not an MP3", "[media]") {
    MediaFacts f = good_acx();
    f.extension = "m4b";
    const auto findings = check_file(f, Profile::Acx);
    const Finding* hit = find_code(findings, "acx-format");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->severity == Severity::Error);
}

TEST_CASE("ACX needs constant bit rate", "[media]") {
    MediaFacts f = good_acx();
    f.bitrate_mode = "vbr";
    REQUIRE(has_code(check_file(f, Profile::Acx), "acx-bitrate-mode"));
    f.bitrate_mode = "CBR";
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-bitrate-mode"));
}

TEST_CASE("ACX bit rate floor is 192 and higher is fine", "[media]") {
    MediaFacts f = good_acx();
    f.bitrate_kbps = 191;
    REQUIRE(has_code(check_file(f, Profile::Acx), "acx-bitrate"));
    f.bitrate_kbps = 192;
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-bitrate"));
    f.bitrate_kbps = 320;
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-bitrate"));
}

TEST_CASE("ACX sample rate must be 44.1 kHz", "[media]") {
    MediaFacts f = good_acx();
    f.sample_rate_hz = 48000;
    REQUIRE(has_code(check_file(f, Profile::Acx), "acx-sample-rate"));
    f.sample_rate_hz = 44100;
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-sample-rate"));
}

TEST_CASE("ACX files stop at 120 minutes", "[media]") {
    MediaFacts f = good_acx();
    f.duration_seconds = 7200.5;
    REQUIRE(has_code(check_file(f, Profile::Acx), "acx-duration"));
    f.duration_seconds = 7200.0;
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-duration"));
}

TEST_CASE("ACX RMS must sit between -23 and -18 dB, ends included", "[media]") {
    MediaFacts f = good_acx();
    for (const double ok : {-23.0, -20.0, -18.0}) {
        f.rms_db = ok;
        REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-rms"));
    }
    for (const double bad : {-23.1, -17.9}) {
        f.rms_db = bad;
        REQUIRE(has_code(check_file(f, Profile::Acx), "acx-rms"));
    }
}

TEST_CASE("ACX peak must not rise above -3 dB", "[media]") {
    MediaFacts f = good_acx();
    f.peak_db = -3.0;
    REQUIRE_FALSE(has_code(check_file(f, Profile::Acx), "acx-peak"));
    f.peak_db = -2.9;
    REQUIRE(has_code(check_file(f, Profile::Acx), "acx-peak"));
}

TEST_CASE("Unmeasured ACX facts are unverified, never a pass", "[media]") {
    MediaFacts f = good_acx();
    f.rms_db.reset();
    f.peak_db.reset();
    f.bitrate_mode.reset();
    const auto findings = check_file(f, Profile::Acx);
    REQUIRE(findings.size() == 1);
    REQUIRE(findings[0].code == "unverified");
    REQUIRE(findings[0].severity == Severity::Warning);
    for (const char* name : {"rms", "peak", "bit rate mode"}) {
        INFO(name);
        REQUIRE(findings[0].message.find(name) != std::string::npos);
    }
    REQUIRE(findings[0].message.find("sample rate") == std::string::npos);
}

TEST_CASE("An unmeasured duration and sample rate are listed too", "[media]") {
    MediaFacts f = good_acx();
    f.duration_seconds.reset();
    f.sample_rate_hz.reset();
    f.bitrate_kbps.reset();
    const auto findings = check_file(f, Profile::Acx);
    REQUIRE(findings.size() == 1);
    REQUIRE(findings[0].message.find("duration") != std::string::npos);
    REQUIRE(findings[0].message.find("sample rate") != std::string::npos);
    REQUIRE(findings[0].message.find("bit rate,") != std::string::npos);
}

TEST_CASE("Technical ACX rules do not apply to other profiles", "[media]") {
    MediaFacts f = good_acx();
    f.extension = "flac";
    f.bitrate_mode = "vbr";
    f.bitrate_kbps = 96;
    f.sample_rate_hz = 22050;
    REQUIRE(check_file(f, Profile::Audiobook).empty());
}

TEST_CASE("An empty set has no findings, even for ACX", "[media]") {
    REQUIRE(check_set({}, Profile::Acx).empty());
    REQUIRE(check_set({}, Profile::Music).empty());
}

TEST_CASE("Mixed mono and stereo is an error for ACX and a warning elsewhere", "[media]") {
    MediaFacts mono = good_acx();
    MediaFacts stereo = good_acx();
    stereo.file = "ch02.mp3";
    stereo.channels = 2;
    stereo.tags["track"] = "2/3";

    const auto acx = check_set({mono, stereo}, Profile::Acx);
    const Finding* hit = find_code(acx, "mixed-channels");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->severity == Severity::Error);
    REQUIRE(hit->file.empty());

    const auto audiobook = check_set({mono, stereo}, Profile::Audiobook);
    REQUIRE(find_code(audiobook, "mixed-channels")->severity == Severity::Warning);

    MediaFacts unknown = mono;
    unknown.channels.reset();
    REQUIRE_FALSE(has_code(check_set({mono, unknown}, Profile::Audiobook), "mixed-channels"));
}

TEST_CASE("Repeated track numbers are reported once with the files involved", "[media]") {
    MediaFacts a = good_music();
    MediaFacts b = good_music();
    b.file = "02 other.flac";
    b.tags["track"] = "01";
    MediaFacts c = good_music();
    c.file = "03 third.flac";
    c.tags["track"] = "3";
    const auto findings = check_set({a, b, c}, Profile::Music);
    REQUIRE(findings.size() == 1);
    REQUIRE(findings[0].code == "duplicate-track");
    REQUIRE(findings[0].message.find("01 song.flac") != std::string::npos);
    REQUIRE(findings[0].message.find("02 other.flac") != std::string::npos);
    REQUIRE(findings[0].message.find("03 third.flac") == std::string::npos);
}

TEST_CASE("Files without a usable track number are ignored for duplicates", "[media]") {
    MediaFacts a = good_music();
    a.tags["track"] = "";
    MediaFacts b = good_music();
    b.file = "b.flac";
    b.tags["track"] = "intro";
    MediaFacts c = good_music();
    c.file = "c.flac";
    c.tags.erase("track");
    REQUIRE_FALSE(has_code(check_set({a, b, c}, Profile::Music), "duplicate-track"));
}

TEST_CASE("Drifting album names are flagged and spelled out", "[media]") {
    MediaFacts a = good_music();
    MediaFacts b = good_music();
    b.file = "02.flac";
    b.tags["track"] = "2/10";
    b.tags["album"] = "record";
    const auto findings = check_set({a, b}, Profile::Music);
    const Finding* hit = find_code(findings, "album-drift");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->message.find("Record") != std::string::npos);
    REQUIRE(hit->message.find("record") != std::string::npos);

    b.tags["album"] = " Record ";
    REQUIRE_FALSE(has_code(check_set({a, b}, Profile::Music), "album-drift"));
    b.tags["album"] = "";
    REQUIRE_FALSE(has_code(check_set({a, b}, Profile::Music), "album-drift"));
}

TEST_CASE("ACX sets always carry the noise floor reminder", "[media]") {
    const auto findings = check_set({good_acx()}, Profile::Acx);
    const Finding* hit = find_code(findings, "noise-floor-not-measured");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->severity == Severity::Warning);
    REQUIRE(check_set({good_music()}, Profile::Music).empty());
}

TEST_CASE("Repeated drops earn a proposed edit at three", "[media]") {
    const std::vector<DropRecord> history = {{"mp4", "narrator"}, {"mp3", "isrc"}, {"mp4", "narrator"},
                                             {"mp4", "narrator"}, {"mp3", "isrc"}, {"flac", "cover"},
                                             {"mp3", "isrc"},     {"mp3", "isrc"}};
    REQUIRE(propose_edit_for(history) == std::vector<std::string>{"mp3:isrc", "mp4:narrator"});
    REQUIRE(propose_edit_for(history, 4) == std::vector<std::string>{"mp3:isrc"});
    REQUIRE(propose_edit_for({}).empty());
    REQUIRE(propose_edit_for(history, 10).empty());
}

TEST_CASE("Audiobookshelf sidecars are written only for known values", "[media]") {
    const auto both = audiobookshelf_sidecars("A long summary.", "Jane Reader");
    REQUIRE(both.size() == 2);
    REQUIRE(both.at("desc.txt") == "A long summary.\n");
    REQUIRE(both.at("reader.txt") == "Jane Reader\n");

    const auto only_desc = audiobookshelf_sidecars("  Text  ", "   ");
    REQUIRE(only_desc.size() == 1);
    REQUIRE(only_desc.at("desc.txt") == "Text\n");

    REQUIRE(audiobookshelf_sidecars("", "").empty());

    const auto only_reader = audiobookshelf_sidecars("   \n", "Jane Reader");
    REQUIRE(only_reader.size() == 1);
    REQUIRE(only_reader.count("reader.txt") == 1);
}
