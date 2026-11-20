#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "FileOrganizerRules.hpp"

using namespace FileOrganizerRules;

namespace {

constexpr std::int64_t kDay = 86400;
constexpr std::int64_t kNow = 1700000000;  // 2023-11-14 22:13:20 UTC

FileInfo file(const std::string& name, std::int64_t age_days = 1) {
    return FileInfo{name, kNow - age_days * kDay, false};
}

const Move* find_move(const Plan& plan, const std::string& from) {
    for (const auto& move : plan.moves) {
        if (move.from == from) {
            return &move;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("File groups follow the extension, case-insensitively", "[organizer]") {
    REQUIRE(category_for("report.pdf") == Category::Documents);
    REQUIRE(category_for("REPORT.DOCX") == Category::Documents);
    REQUIRE(category_for("notes.txt") == Category::Documents);
    REQUIRE(category_for("photo.JPG") == Category::Images);
    REQUIRE(category_for("logo.svg") == Category::Images);
    REQUIRE(category_for("clip.mp4") == Category::Videos);
    REQUIRE(category_for("clip.MOV") == Category::Videos);
    REQUIRE(category_for("bundle.zip") == Category::Archives);
    REQUIRE(category_for("bundle.tar") == Category::Archives);
    REQUIRE(category_for("setup.dmg") == Category::Installers);
    REQUIRE(category_for("setup.pkg") == Category::Installers);
    REQUIRE(category_for("script.py") == Category::Code);
    REQUIRE(category_for("budget.xlsx") == Category::Spreadsheets);
    REQUIRE(category_for("data.csv") == Category::Spreadsheets);
    REQUIRE(category_for("deck.pptx") == Category::Presentations);
    REQUIRE(category_for("deck.key") == Category::Presentations);
}

TEST_CASE("Unknown, missing and hidden-style extensions are Other", "[organizer]") {
    REQUIRE(category_for("README") == Category::Other);
    REQUIRE(category_for("mystery.xyz") == Category::Other);
    REQUIRE(category_for(".bashrc") == Category::Other);
    REQUIRE(category_for("archive.tar.gz") == Category::Archives);
    REQUIRE(category_for("") == Category::Other);
}

TEST_CASE("Category folder names", "[organizer]") {
    REQUIRE(category_folder(Category::Documents) == "Documents");
    REQUIRE(category_folder(Category::Images) == "Images");
    REQUIRE(category_folder(Category::Videos) == "Videos");
    REQUIRE(category_folder(Category::Archives) == "Archives");
    REQUIRE(category_folder(Category::Installers) == "Installers");
    REQUIRE(category_folder(Category::Code) == "Code");
    REQUIRE(category_folder(Category::Spreadsheets) == "Spreadsheets");
    REQUIRE(category_folder(Category::Presentations) == "Presentations");
    REQUIRE(category_folder(Category::Other) == "To-Sort");
}

TEST_CASE("Download artefacts are cleaned from names", "[organizer]") {
    REQUIRE(clean_download_artifacts("document-final-v2 (1).pdf") == "document.pdf");
    REQUIRE(clean_download_artifacts("IMG_0847 (1).png") == "IMG_0847.png");
    REQUIRE(clean_download_artifacts("report_copy.docx") == "report.docx");
    REQUIRE(clean_download_artifacts("data-v2-final.csv") == "data.csv");
    REQUIRE(clean_download_artifacts("Report FINAL.pdf") == "Report.pdf");
    REQUIRE(clean_download_artifacts("notes.txt") == "notes.txt");
}

TEST_CASE("Cleaning leaves honest names alone", "[organizer]") {
    REQUIRE(clean_download_artifacts("final.pdf") == "final.pdf");
    REQUIRE(clean_download_artifacts("v2.pdf") == "v2.pdf");
    REQUIRE(clean_download_artifacts("my-version-2.pdf") == "my-version-2.pdf");
    REQUIRE(clean_download_artifacts("archive.tar.gz") == "archive.tar.gz");
    REQUIRE(clean_download_artifacts("README") == "README");
    REQUIRE(clean_download_artifacts("(1).pdf") == "(1).pdf");
    REQUIRE(clean_download_artifacts(" (1).pdf") == " (1).pdf");  // nothing would be left, so keep it
    REQUIRE(clean_download_artifacts("") == "");
}

TEST_CASE("Spaces become hyphens", "[organizer]") {
    REQUIRE(hyphenate_spaces("Client Work") == "Client-Work");
    REQUIRE(hyphenate_spaces("a   b") == "a-b");
    REQUIRE(hyphenate_spaces("nospaces") == "nospaces");
}

TEST_CASE("Archive candidacy has a firm boundary", "[organizer]") {
    REQUIRE(is_archive_candidate(kNow - 180 * kDay, kNow));
    REQUIRE_FALSE(is_archive_candidate(kNow - 179 * kDay, kNow));
    REQUIRE(is_archive_candidate(kNow - 400 * kDay, kNow));
    REQUIRE_FALSE(is_archive_candidate(kNow + kDay, kNow));  // a future mtime is not old
    REQUIRE(is_archive_candidate(kNow - 30 * kDay, kNow, 30));
    REQUIRE_FALSE(is_archive_candidate(kNow - 29 * kDay, kNow, 30));
}

TEST_CASE("Date folders are UTC year and month", "[organizer]") {
    REQUIRE(date_folder(1700000000) == "2023/11-November");
    REQUIRE(date_folder(0) == "1970/01-January");
    REQUIRE(date_folder(951782400) == "2000/02-February");  // 2000-02-29, a leap day
    REQUIRE(date_folder(1709251199) == "2024/02-February");
    REQUIRE(date_folder(1709251200) == "2024/03-March");
    REQUIRE(date_folder(-1) == "1969/12-December");  // one second before the epoch
    REQUIRE(date_folder(-2208988800) == "1900/01-January");
    REQUIRE(year_folder(1700000000) == "2023");
    REQUIRE(year_folder(0) == "1970");
}

TEST_CASE("Unique names keep the extension and count up", "[organizer]") {
    std::set<std::string> taken = {"a.pdf", "a-2.pdf"};
    auto exists = [&](const std::string& name) { return taken.count(name) > 0; };

    REQUIRE(unique_name("b.pdf", exists) == "b.pdf");
    REQUIRE(unique_name("a.pdf", exists) == "a-3.pdf");
    taken.insert("README");
    REQUIRE(unique_name("README", exists) == "README-2");
    REQUIRE(unique_name("Documents/a.pdf", exists) == "Documents/a.pdf");
}

TEST_CASE("Plan sorts top-level files into type folders", "[organizer]") {
    const auto plan = plan_by_type({file("report.pdf"), file("photo.jpg"), file("README")}, kNow, Options{});

    REQUIRE(plan.moves.size() == 3);
    REQUIRE(find_move(plan, "report.pdf")->to == "Documents/report.pdf");
    REQUIRE(find_move(plan, "photo.jpg")->to == "Images/photo.jpg");
    REQUIRE(find_move(plan, "README")->to == "To-Sort/README");
}

TEST_CASE("Plan sends old files to Archive by year", "[organizer]") {
    const auto plan = plan_by_type({file("old.pdf", 400), file("new.pdf", 3)}, kNow, Options{});

    REQUIRE(find_move(plan, "old.pdf")->to == "Archive/2022/old.pdf");  // 400 days before 2023-11-14
    REQUIRE(find_move(plan, "new.pdf")->to == "Documents/new.pdf");
}

TEST_CASE("Plan cleans names unless told not to", "[organizer]") {
    const auto cleaned = plan_by_type({file("report (1).pdf")}, kNow, Options{});
    REQUIRE(cleaned.moves[0].to == "Documents/report.pdf");

    Options keep;
    keep.clean_names = false;
    const auto kept = plan_by_type({file("report (1).pdf")}, kNow, keep);
    REQUIRE(kept.moves[0].to == "Documents/report (1).pdf");
}

TEST_CASE("Plan skips folders, hidden files and protected names with a reason", "[organizer]") {
    Options options;
    options.protected_prefixes = {"Current-Project"};
    FileInfo dir{"Projects", kNow - kDay, true};
    const auto plan =
        plan_by_type({dir, file(".DS_Store"), file("current-project-notes.txt"), file("ok.txt")}, kNow, options);

    REQUIRE(plan.moves.size() == 1);
    REQUIRE(plan.moves[0].from == "ok.txt");
    REQUIRE(plan.skipped.size() == 3);
    for (const auto& line : plan.skipped) {
        REQUIRE(line.find(": ") != std::string::npos);  // "<name>: <reason>"
    }
}

TEST_CASE("Plan never sends two files to the same place", "[organizer]") {
    const auto plan = plan_by_type({file("a.pdf"), file("a (1).pdf"), file("a_copy.pdf")}, kNow, Options{});

    std::set<std::string> targets;
    for (const auto& move : plan.moves) {
        targets.insert(move.to);
    }
    REQUIRE(plan.moves.size() == 3);
    REQUIRE(targets.size() == 3);
    REQUIRE(find_move(plan, "a.pdf")->to == "Documents/a.pdf");
    REQUIRE(find_move(plan, "a (1).pdf")->to == "Documents/a-2.pdf");
    REQUIRE(find_move(plan, "a_copy.pdf")->to == "Documents/a-3.pdf");
}

TEST_CASE("Plan respects names already in the destination", "[organizer]") {
    const auto plan = plan_by_type({file("a.pdf")}, kNow, Options{}, {"Documents/a.pdf"});
    REQUIRE(plan.moves[0].to == "Documents/a-2.pdf");
}

TEST_CASE("Plan only moves and only names input files", "[organizer]") {
    const std::vector<FileInfo> input = {file("a.pdf"), file("b.jpg"), file("c.zip", 500), file("d")};
    const auto plan = plan_by_type(input, kNow, Options{});

    std::set<std::string> names;
    for (const auto& f : input) {
        names.insert(f.name);
    }
    for (const auto& move : plan.moves) {
        REQUIRE(names.count(move.from) == 1);
        REQUIRE(move.to != move.from);
        REQUIRE(move.to.find('/') != std::string::npos);  // always lands inside a folder
    }
}

TEST_CASE("Undo reverses every move in reverse order", "[organizer]") {
    const auto plan = plan_by_type({file("a.pdf"), file("b.jpg")}, kNow, Options{});
    const auto undo = undo_plan(plan);

    REQUIRE(undo.moves.size() == plan.moves.size());
    REQUIRE(undo.moves[0].from == plan.moves[1].to);
    REQUIRE(undo.moves[0].to == plan.moves[1].from);
    REQUIRE(undo.moves[1].from == plan.moves[0].to);
    REQUIRE(undo.moves[1].to == plan.moves[0].from);
    REQUIRE(undo_plan(undo).moves[0].from == plan.moves[0].from);
}

TEST_CASE("Move log round-trips awkward names", "[organizer]") {
    Plan plan;
    plan.moves.push_back({"plain.txt", "Documents/plain.txt"});
    plan.moves.push_back({"tab\tname.txt", "Documents/tab\tname.txt"});
    plan.moves.push_back({"line\nbreak.txt", "Documents/line\nbreak.txt"});
    plan.moves.push_back({"back\\slash.txt", "Documents/back\\slash.txt"});

    const std::string text = serialize_log(plan);
    REQUIRE(std::count(text.begin(), text.end(), '\n') == 4);  // one line per move, escapes included

    const auto parsed = parse_log(text);
    REQUIRE(parsed.moves.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(parsed.moves[i].from == plan.moves[i].from);
        REQUIRE(parsed.moves[i].to == plan.moves[i].to);
    }
}

TEST_CASE("Move log skips malformed lines", "[organizer]") {
    const auto parsed = parse_log("good\tDocuments/good\nno tab here\n\n\tmissing-from\nmissing-to\t\n");
    REQUIRE(parsed.moves.size() == 1);
    REQUIRE(parsed.moves[0].from == "good");
}
