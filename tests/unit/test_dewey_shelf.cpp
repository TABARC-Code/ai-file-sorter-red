#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "DeweyEngine.hpp"
#include "DeweyShelf.hpp"
#include "MetadataFolder.hpp"

using namespace DeweyShelf;
namespace fo = FileOrganizerRules;

namespace {

constexpr std::int64_t kNow = 1'700'000'000;
constexpr std::int64_t kOld = 1'400'000'000;

Item plain(const std::string& name, std::int64_t mtime = kNow) {
    return {{name, mtime, false}, std::nullopt};
}

Item tagged(const std::string& name, const AudioFacts& facts, std::int64_t mtime = kNow) {
    return {{name, mtime, false}, facts};
}

std::string destination(const ShelfPlan& plan, const std::string& from) {
    for (const auto& move : plan.plan.moves) {
        if (move.from == from) {
            return move.to;
        }
    }
    return "(no move)";
}

}  // namespace

// ---------------------------------------------------------------- name guesses

TEST_CASE("Audio extensions guess music, with low confidence and the reason said", "[shelf]") {
    for (const char* name : {"a.mp3", "A.FLAC", "b.m4a", "c.ogg", "d.opus", "e.wav", "f.aiff", "g.aac", "h.wma"}) {
        const Guess guess = guess_from_name(name);
        INFO(name);
        CHECK(guess.anchor_code == "780.AUD");
        CHECK(guess.format == "AUD");
        CHECK(guess.confidence == "low");
        CHECK(guess.reason == "extension only");
        CHECK(guess.dewey_number.empty());
        CHECK(guess.context_tags.empty());
    }
}

TEST_CASE("An m4b is an audiobook, which is not music", "[shelf]") {
    const Guess guess = guess_from_name("Dune.M4B");
    CHECK(guess.anchor_code == "000");
    CHECK(guess.format == "AUD");
    CHECK(guess.confidence == "low");
    CHECK(guess.context_tags == std::vector<std::string>{"audiobook"});
}

TEST_CASE("Video extensions guess film and video", "[shelf]") {
    for (const char* name : {"a.mp4", "b.MOV", "c.mkv", "d.avi", "e.webm", "f.m4v"}) {
        const Guess guess = guess_from_name(name);
        INFO(name);
        CHECK(guess.anchor_code == "791.VID");
        CHECK(guess.format == "VID");
        CHECK(guess.confidence == "low");
    }
}

TEST_CASE("Anything else gets no guess", "[shelf]") {
    for (const char* name :
         {"report.pdf", "photo.jpg", "noextension", ".mp3", "song.mp3.txt", "archive.zip", "", "song.", "song.mp3."}) {
        INFO(name);
        CHECK(guess_from_name(name).empty());
    }
}

// ---------------------------------------------------------------- tag refinement

TEST_CASE("Any music tag lifts the guess to medium", "[shelf]") {
    const Guess base = guess_from_name("a.mp3");
    for (const AudioFacts& facts :
         {AudioFacts{"Artist", "", "", ""}, AudioFacts{"", "Album", "", ""}, AudioFacts{"", "", "Pop", ""}}) {
        const Guess guess = refine_with_tags(base, facts);
        CHECK(guess.anchor_code == "780.AUD");
        CHECK(guess.confidence == "medium");
        CHECK(guess.dewey_number == "780");
        CHECK(guess.reason == "tags present");
    }
}

TEST_CASE("No tags leaves the name guess alone", "[shelf]") {
    const Guess base = guess_from_name("a.mp3");
    const Guess guess = refine_with_tags(base, AudioFacts{});
    CHECK(guess.confidence == "low");
    CHECK(guess.reason == "extension only");
    CHECK(guess.dewey_number.empty());
}

TEST_CASE("Jazz is high and 781.65, rock is medium and 781.66", "[shelf]") {
    const Guess base = guess_from_name("a.flac");
    const Guess jazz = refine_with_tags(base, AudioFacts{"", "", "Jazz", ""});
    CHECK(jazz.confidence == "high");
    CHECK(jazz.dewey_number == "781.65");
    const Guess bebop = refine_with_tags(base, AudioFacts{"", "", "Modern JAZZ Fusion", ""});
    CHECK(bebop.dewey_number == "781.65");
    const Guess rock = refine_with_tags(base, AudioFacts{"", "", "Rock", ""});
    CHECK(rock.confidence == "medium");
    CHECK(rock.dewey_number == "781.66");
}

TEST_CASE("Classical leaves the number blank and says why", "[shelf]") {
    const Guess guess = refine_with_tags(guess_from_name("a.flac"), AudioFacts{"", "", "Classical", ""});
    CHECK(guess.anchor_code == "780.AUD");
    CHECK(guess.confidence == "medium");
    CHECK(guess.dewey_number.empty());
    CHECK(guess.reason.find("782") != std::string::npos);
}

TEST_CASE("A narrator makes it an audiobook, whatever the extension", "[shelf]") {
    const Guess guess =
        refine_with_tags(guess_from_name("a.mp3"), AudioFacts{"Frank Herbert", "Dune", "", "Scott Brick"});
    CHECK(guess.anchor_code == "000");
    CHECK(guess.format == "AUD");
    CHECK(guess.confidence == "low");
    CHECK(guess.context_tags == std::vector<std::string>{"audiobook"});
    CHECK(guess.dewey_number.empty());
    CHECK(guess.reason.find("class it with the work") != std::string::npos);
}

TEST_CASE("Audiobook and spoken-word genres are audiobooks too", "[shelf]") {
    for (const char* genre : {"Audiobook", "AUDIOBOOKS", "Spoken Word", "spoken & audio", "Audio Book"}) {
        INFO(genre);
        const Guess guess = refine_with_tags(guess_from_name("a.m4a"), AudioFacts{"", "", genre, ""});
        CHECK(guess.anchor_code == "000");
    }
}

TEST_CASE("A narrator beats a jazz genre", "[shelf]") {
    const Guess guess = refine_with_tags(guess_from_name("a.mp3"), AudioFacts{"", "", "Jazz", "Someone"});
    CHECK(guess.anchor_code == "000");
}

TEST_CASE("An m4b with music tags stays an audiobook", "[shelf]") {
    const Guess guess = refine_with_tags(guess_from_name("a.m4b"), AudioFacts{"Artist", "Album", "Rock", ""});
    CHECK(guess.anchor_code == "000");
}

TEST_CASE("Video and empty guesses pass through refinement unchanged", "[shelf]") {
    const Guess video = guess_from_name("a.mp4");
    const Guess same = refine_with_tags(video, AudioFacts{"A", "B", "Jazz", "N"});
    CHECK(same.anchor_code == "791.VID");
    CHECK(same.confidence == "low");
    CHECK(refine_with_tags(Guess{}, AudioFacts{"A", "B", "", ""}).empty());
}

TEST_CASE("Every guess uses a legal anchor, format and confidence", "[shelf]") {
    std::vector<Guess> all = {guess_from_name("a.mp3"), guess_from_name("a.m4b"), guess_from_name("a.mp4")};
    all.push_back(refine_with_tags(all[0], AudioFacts{"", "", "Jazz", ""}));
    all.push_back(refine_with_tags(all[0], AudioFacts{"", "", "Classical", ""}));
    all.push_back(refine_with_tags(all[0], AudioFacts{"", "", "", "Narrator"}));
    for (const Guess& guess : all) {
        CHECK(DeweyEngine::anchor_label(guess.anchor_code).has_value());
        CHECK(DeweyEngine::is_legal_format(guess.format));
        CHECK(DeweyEngine::is_legal_confidence(guess.confidence));
    }
}

// ---------------------------------------------------------------- shelves

TEST_CASE("Shelf folders are named from the anchor", "[shelf]") {
    CHECK(shelf_folder(guess_from_name("a.mp3")) == "780-music-and-audio");
    CHECK(shelf_folder(guess_from_name("a.mp4")) == "791-film-video-moving-image");
    CHECK(shelf_folder(guess_from_name("a.m4b")) == "000-audiobooks");
    CHECK(shelf_folder(Guess{}).empty());
}

TEST_CASE("Music is shelved by artist and album when the tags are there", "[shelf]") {
    const Guess guess = guess_from_name("t.mp3");
    const AudioFacts both{"Miles Davis", "Kind of Blue", "", ""};
    CHECK(shelf_path(guess, &both, "t.mp3") == "780-music-and-audio/Miles Davis/Kind of Blue/t.mp3");
    const AudioFacts artist_only{"Miles Davis", "", "", ""};
    CHECK(shelf_path(guess, &artist_only, "t.mp3") == "780-music-and-audio/Miles Davis/t.mp3");
    const AudioFacts album_only{"", "Kind of Blue", "", ""};
    CHECK(shelf_path(guess, &album_only, "t.mp3") == "780-music-and-audio/Kind of Blue/t.mp3");
    CHECK(shelf_path(guess, nullptr, "t.mp3") == "780-music-and-audio/t.mp3");
    const AudioFacts none;
    CHECK(shelf_path(guess, &none, "t.mp3") == "780-music-and-audio/t.mp3");
}

TEST_CASE("Unsafe characters in tags become hyphens", "[shelf]") {
    const Guess guess = guess_from_name("t.mp3");
    const AudioFacts facts{"AC/DC", "Back in Black: Live?", "", ""};
    CHECK(shelf_path(guess, &facts, "t.mp3") == "780-music-and-audio/AC-DC/Back in Black- Live-/t.mp3");
}

TEST_CASE("Folder names are trimmed and cannot be dots or blank", "[shelf]") {
    const Guess guess = guess_from_name("t.mp3");
    const AudioFacts padded{"  Artist. ", "..", "", ""};
    CHECK(shelf_path(guess, &padded, "t.mp3") == "780-music-and-audio/Artist/t.mp3");
    const AudioFacts blank{"   ", "\t", "", ""};
    CHECK(shelf_path(guess, &blank, "t.mp3") == "780-music-and-audio/t.mp3");
    const AudioFacts spacing{"A\tB", "C\rD\nE", "", ""};
    CHECK(shelf_path(guess, &spacing, "t.mp3") == "780-music-and-audio/A B/C D E/t.mp3");
    const AudioFacts backslash{"A\\B", "", "", ""};
    CHECK(shelf_path(guess, &backslash, "t.mp3") == "780-music-and-audio/A-B/t.mp3");
}

TEST_CASE("Audiobooks are shelved by author and title", "[shelf]") {
    const AudioFacts facts{"Frank Herbert", "Dune", "", "Scott Brick"};
    const Guess guess = refine_with_tags(guess_from_name("c1.mp3"), facts);
    CHECK(shelf_path(guess, &facts, "c1.mp3") == "000-audiobooks/Frank Herbert/Dune/c1.mp3");
}

TEST_CASE("Video has no sub-levels, and no guess has no shelf", "[shelf]") {
    const AudioFacts facts{"A", "B", "", ""};
    CHECK(shelf_path(guess_from_name("f.mp4"), &facts, "f.mp4") == "791-film-video-moving-image/f.mp4");
    CHECK(shelf_path(Guess{}, &facts, "f.pdf").empty());
}

// ---------------------------------------------------------------- manifest notes

TEST_CASE("Manifest notes carry the anchor, confidence and reason", "[shelf]") {
    const Guess guess = refine_with_tags(guess_from_name("a.flac"), AudioFacts{"", "", "Jazz", ""});
    const std::vector<std::pair<std::string, std::string>> want = {
        {"dewey", "781.65"}, {"dewey_anchor", "780.AUD"}, {"dewey_confidence", "high"}, {"dewey_note", "tags present"}};
    CHECK(manifest_sets(guess) == want);
}

TEST_CASE("An unsure number is left out of the manifest rather than guessed", "[shelf]") {
    const auto sets = manifest_sets(guess_from_name("a.mp3"));
    REQUIRE(sets.size() == 3);
    CHECK(sets[0].first == "dewey_anchor");
    CHECK(sets[1] == std::make_pair(std::string("dewey_confidence"), std::string("low")));
    CHECK(sets[2] == std::make_pair(std::string("dewey_note"), std::string("extension only")));
}

TEST_CASE("No guess, no manifest notes", "[shelf]") {
    CHECK(manifest_sets(Guess{}).empty());
}

// ---------------------------------------------------------------- planning

TEST_CASE("Music goes to its shelf and the rest keeps the type folders", "[shelf][plan]") {
    const std::vector<Item> items = {tagged("song.mp3", AudioFacts{"Miles Davis", "Kind of Blue", "Jazz", ""}),
                                     plain("report.pdf"), plain("clip.mp4"), plain("mystery.xyz")};
    const ShelfPlan plan = plan_by_shelf(items, kNow, fo::Options{});
    CHECK(destination(plan, "song.mp3") == "780-music-and-audio/Miles Davis/Kind of Blue/song.mp3");
    CHECK(destination(plan, "report.pdf") == "Documents/report.pdf");
    CHECK(destination(plan, "clip.mp4") == "791-film-video-moving-image/clip.mp4");
    CHECK(destination(plan, "mystery.xyz") == "To-Sort/mystery.xyz");
    CHECK(plan.plan.moves.size() == 4);
}

TEST_CASE("Old music is shelved, not archived", "[shelf][plan]") {
    const ShelfPlan plan = plan_by_shelf({plain("old.mp3", kOld), plain("old.pdf", kOld)}, kNow, fo::Options{});
    CHECK(destination(plan, "old.mp3") == "780-music-and-audio/old.mp3");
    CHECK(destination(plan, "old.pdf").rfind("Archive/", 0) == 0);
}

TEST_CASE("Guesses are recorded for shelved files only", "[shelf][plan]") {
    const ShelfPlan plan =
        plan_by_shelf({tagged("song.mp3", AudioFacts{"A", "B", "Rock", ""}), plain("b.m4b"), plain("report.pdf")}, kNow,
                      fo::Options{});
    REQUIRE(plan.guesses.size() == 2);
    CHECK(plan.guesses.at("song.mp3").dewey_number == "781.66");
    CHECK(plan.guesses.at("b.m4b").anchor_code == "000");
    CHECK(plan.guesses.count("report.pdf") == 0);
}

TEST_CASE("Audio with no tags is shelved by name at low confidence", "[shelf][plan]") {
    const ShelfPlan plan = plan_by_shelf({plain("bare.flac")}, kNow, fo::Options{});
    CHECK(destination(plan, "bare.flac") == "780-music-and-audio/bare.flac");
    CHECK(plan.guesses.at("bare.flac").confidence == "low");
}

TEST_CASE("Skipped files stay skipped and are not shelved", "[shelf][plan]") {
    fo::Options options;
    options.protected_prefixes = {"current"};
    const ShelfPlan plan = plan_by_shelf({plain(".hidden.mp3"), plain("Current Album.mp3"), plain("ok.mp3"),
                                          Item{{"Music Folder", kNow, true}, std::nullopt}},
                                         kNow, options);
    CHECK(plan.plan.moves.size() == 1);
    CHECK(plan.plan.skipped.size() == 3);
    CHECK(plan.guesses.count(".hidden.mp3") == 0);
    CHECK(plan.guesses.count("Current Album.mp3") == 0);
}

TEST_CASE("Download tails are cleaned from shelved names", "[shelf][plan]") {
    const ShelfPlan plan = plan_by_shelf({plain("song (1).mp3")}, kNow, fo::Options{});
    CHECK(destination(plan, "song (1).mp3") == "780-music-and-audio/song.mp3");
    fo::Options keep;
    keep.clean_names = false;
    const ShelfPlan raw = plan_by_shelf({plain("song (1).mp3")}, kNow, keep);
    CHECK(destination(raw, "song (1).mp3") == "780-music-and-audio/song (1).mp3");
}

TEST_CASE("Shelved names never collide with each other or with what is there", "[shelf][plan]") {
    const AudioFacts same{"A", "B", "", ""};
    const ShelfPlan plan = plan_by_shelf({tagged("t.mp3", same), tagged("t (1).mp3", same), tagged("t (2).mp3", same)},
                                         kNow, fo::Options{}, {"780-music-and-audio/A/B/t.mp3"});
    std::set<std::string> seen;
    for (const auto& move : plan.plan.moves) {
        CHECK(seen.insert(move.to).second);
        CHECK(move.to != "780-music-and-audio/A/B/t.mp3");
    }
    CHECK(destination(plan, "t.mp3") == "780-music-and-audio/A/B/t-2.mp3");
    CHECK(destination(plan, "t (1).mp3") == "780-music-and-audio/A/B/t-3.mp3");
    CHECK(destination(plan, "t (2).mp3") == "780-music-and-audio/A/B/t-4.mp3");
}

TEST_CASE("A shelved file does not take a name the type folders needed", "[shelf][plan]") {
    // The base plan puts report.pdf in Documents; a shelved name can never equal that, but both must stay unique.
    const ShelfPlan plan = plan_by_shelf({plain("a.mp3"), plain("a.pdf"), plain("a (1).pdf")}, kNow, fo::Options{});
    std::set<std::string> seen;
    for (const auto& move : plan.plan.moves) {
        CHECK(seen.insert(move.to).second);
    }
    CHECK(plan.plan.moves.size() == 3);
}

TEST_CASE("The plan can be undone like any other", "[shelf][plan]") {
    const ShelfPlan plan =
        plan_by_shelf({tagged("t.mp3", AudioFacts{"A", "B", "", ""}), plain("r.pdf")}, kNow, fo::Options{});
    const fo::Plan undo = fo::undo_plan(plan.plan);
    REQUIRE(undo.moves.size() == 2);
    CHECK(undo.moves[0].to == "r.pdf");
    CHECK(undo.moves[1].from == "780-music-and-audio/A/B/t.mp3");
    CHECK(undo.moves[1].to == "t.mp3");
}

TEST_CASE("No items, no plan", "[shelf][plan]") {
    const ShelfPlan plan = plan_by_shelf({}, kNow, fo::Options{});
    CHECK(plan.plan.moves.empty());
    CHECK(plan.guesses.empty());
}

TEST_CASE("Manifest notes go straight into the folder manifest planner", "[shelf]") {
    const Guess guess = refine_with_tags(guess_from_name("a.flac"), AudioFacts{"", "", "Rock", ""});
    MetadataFolder::State state;
    state.dir = "/music/album";
    state.manifest["dewey_confidence"] = {"medium", true};
    const auto plan = MetadataFolder::plan_manifest(state, manifest_sets(guess));
    REQUIRE(plan.error.empty());
    // The confidence already matches, so only the other three keys change.
    REQUIRE(plan.changes.size() == 3);
    CHECK(plan.changes[0].key == "dewey");
    CHECK(plan.changes[0].to == "781.66");
    CHECK(plan.changes[1].key == "dewey_anchor");
    CHECK(plan.changes[2].key == "dewey_note");
}
