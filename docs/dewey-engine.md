# Dewey engine

A set of plain C++17 modules that hold the deterministic rules from the Dewey file-organising
approach: what counts as a legal record, how files get sorted by type, what a duplicate really is,
and what a vault of notes has to look like before anything is written to disk. No Qt, no network, no
model. That is deliberate. Rules that can be checked with a unit test should be checked with one, and
the fuzzy parts should stay out of the way until they are actually needed.

Everything below lives in `app/include` and `app/lib` and has a Catch2 file in `tests/unit`. All twenty
test files are registered in `app/CMakeLists.txt`. Seventeen modules are picked up by the existing source glob.
The three tag modules (`MetadataTags`, `MetadataApply`, `MetadataBatch`) are the exception: they need the
vendored TagLib in `third_party/taglib`, so they are left out of the app glob and built into the test target
only, until the main binary links TagLib.

## Modules

| Module | What it does |
| --- | --- |
| `DeweyEngine` | Anchor vocabulary (45 entries), the legal values for role, worthiness, confidence, format and lens, relation types with their reverses, record validation, reverse-link verification, duplicate evidence tiers. |
| `FileOrganizerRules` | Sorts a folder's top level by file type, archives old files by year, cleans "(1)" and "final" tails, plans collision-free moves, and writes and reads an undo log. Plans only ever move. |
| `MetadataPlan` | Builds a batch tagging plan: natural file ordering, `{n}` / `{total}` / `{stem}` tokens, CSV round trip. It plans. It does not write tags. |
| `Sha256` | A small streaming SHA-256, checked against the system `sha256sum` at awkward block lengths. |
| `VaultExport` | Renders notes, an index and a graph file, lints the lot, then writes. A failed lint writes nothing. Existing files with different content are conflicts unless you say otherwise. |
| `DecisionRegister` | The register of edge-case rulings: render, parse, validate, supersede, and the automatic part of the consistency pass. |
| `MediaCheck` | Pre-flight checks on media facts by destination (music, audiobook, ACX, film, image): per-file rules, set rules (mixed mono and stereo, repeated track numbers, drifting album names), the dropped-tag learning rule, and Audiobookshelf sidecar text. It judges facts someone else measured. |
| `WikiLint` | Lints a folder of Markdown notes: required frontmatter, broken and ambiguous `[[wikilinks]]`, links that are not returned, orphans. The required fields are a choice between the llm-wiki schema and the Dewey record schema. |
| `LinkAudit` | The bidirectional-link-engine (v2.0.0) rules for skills. Reads `### > target` and `### < source` contract blocks and audits them by link-matching, not by counting mentions. Also finds chains and loops, lists everything that refers to a skill before it is removed, checks a replacement is wired, and writes a contract block only when every field is real. |
| `SourceLedger` | The Quiet Council's source arithmetic. Judges a URL's tier on its host alone, merges every researcher's citations into one numbered registry (dedupe key, convergence at three finders), remaps `[SOURCE_n]` tags in a single pass, and prices age and distance from the evidence: a decay class per domain, a temporal penalty that stops at a per-class cap, a citation-depth penalty, and the final weight. "Today" is passed in; nothing reads the clock. |
| `QuietCouncil` | The rest of the Quiet Council (v3.1.0) that is rules rather than research. The thirteen-researcher registry, depth to model choice, a cost estimate with the academic acknowledgement, session ids, domain routing (six classes, blends of two), audience calibration, the contradiction and steelman rules, the eight Kaizen flags and the three-in-a-row improvement trigger, entity and slug rules, and the plain-Markdown knowledge graph: confirmed facts, disputes, topic index, session log, wiki page. Writers never overwrite; a failed write is reported, not thrown. It reads the graph back for the prior-knowledge block and for stale facts. |
| `MetadataTags` | Reads and writes tags through the vendored TagLib: ID3 v2.3 for MP3, WAV and AIFF, iTunes atoms for the MP4 family, Vorbis comments for FLAC, Ogg and Opus. Track and disc take `3` or `3/12`; anything else is refused before the file is touched. |
| `MetadataApply` | The safety layer around a tag write. A fingerprint (size and modification time) stops a file that changed since the plan. A backup is made first; one that matches the plan's fingerprint is our own from a crashed run and is rolled back, any other stops that file. A failed write puts the original back. The result is read back, so a tag that did not stick is reported as dropped. Also `find_marks`, `restore` and `clean` (a dry run unless told otherwise). |
| `MetadataBatch` | Runs a plan in batches. Progress goes in a plain tab-separated file, one line per file, and only `ok` lines count, so failures are retried. A free-space check comes first: each folder needs the size of its files plus a tenth. A failing file is recorded and the rest carry on. Files run one at a time. |
| `MetadataFolder` | The folder-level files that sit beside the audio: Audiobookshelf `reader.txt` (narrator) and `desc.txt` (description), and the `metadata-maestro.json` manifest. Reading trims the text files, lists cover files and parses the manifest as strict JSON; a bad one is reported, not guessed at. Planning is pure: one planned value wins, several is a warning, and with none planned a single existing tag value is used. Applying follows the tag rules (`.mm-bak` copy, or a `.mm-new` marker for a new file) and checks every target before the first write. Empty text removes a sidecar; manifest keys are merged and stamped. Manifest keys are written sorted. |
| `MetadataLedger` | The Metadata Maestro ledger: one JSON object per line for each apply, restore and verdict. Lessons count what each container dropped (`.flac:genre`) and what was accepted or rejected per key; a pair dropped three or more times is proposed as an edit to the field map, using the rule in `MediaCheck`. A line that is not a JSON object is ignored, and an entry missing a field loses only that field. Nothing calls it yet: apply and restore do not write to it on their own. |
| `MetadataPlanFile` | The saved plan as JSON: tool and version, a summary, entries per file (family, fingerprint, changes with before and after values, cover instructions) and per folder. Reading is strict: a plan from another major version, a missing entries or folders list, or an entry without a file, family, fingerprint or well-formed changes is refused with the entry number. `batch_input` hands the tag families (`id3`, `mp4`, `vorbis`) to `MetadataBatch` and lists the rest as unsupported rather than dropping them; `folder_changes` feeds `MetadataFolder`. Comments are refused and a repeated key keeps the last value. |
| `MetadataResolve` | Turns a request (set or clear these tags on these files) into a saved plan by checking each file as it is now. Reading the files is handed in as functions, so it stays pure. A value already in place is left out; a changed one keeps what it was; `__clear__` or an empty value removes a tag, and clearing an absent tag is nothing. A file that cannot be read or has no tag writer is warned about and left out. |
| `DeweyShelf` | Lets Dewey anchors steer where media is shelved. Audio is `780.AUD` and video `791.VID`, in folders named from the anchor (`780-music-and-audio/<artist>/<album>`). Evidence is graded honestly: an extension alone is `low`, tags make music `medium`, a jazz genre `high` (781.65), rock 781.66, and classical leaves the number blank because it is arranged by form and medium. A narrator, a spoken-word genre or an `.m4b` makes it an audiobook, anchored `000` and shelved in `000-audiobooks`, never as music, since it should be classed with the work. Shelved files skip Archive (a music library is old and still in use); everything else goes through `FileOrganizerRules` unchanged. `manifest_sets` turns a guess into `dewey`, `dewey_anchor`, `dewey_confidence` and `dewey_note` for the folder manifest. Tags are handed in, not read. |

## The command-line tool

`dewey-cli` builds on its own, no Qt needed:

```text
cmake -S dewey-cli -B build-dewey-cli
cmake --build build-dewey-cli
```

```text
dewey-cli organise <folder> [--apply] [--archive-days N] [--protect PREFIX]... [--log FILE]
dewey-cli undo <folder> --log FILE [--apply]
dewey-cli duplicates <folder>
dewey-cli anchors
dewey-cli wiki-lint <vault> [--schema wiki|dewey]
dewey-cli register-check <register.md>
dewey-cli sha256 <file>
dewey-cli link-audit <skills-folder>
dewey-cli link-chains <skills-folder>
dewey-cli link-unwire <skills-folder> <skill> [replacement]
dewey-cli council-classify <topic> [--constraint TEXT]...
dewey-cli council-cost <quick|standard|deep|academic> [--ack]
dewey-cli council-session-id <kg-folder> [--date YYYY-MM-DD]
dewey-cli council-sources <citations.tsv>
dewey-cli council-kg-check <kg-folder> --topic TEXT [--domain CLASS] [--today YYYY-MM-DD]
```

`organise` and `undo` print a plan and stop unless you pass `--apply`. Applying never overwrites a
file that is already there, never deletes anything, and leaves symbolic links, hidden files and
folders alone. The undo log records only the moves that actually happened. `duplicates` reports
groups of identical files and changes nothing. Empty files are ignored, because they all match each
other and that tells you nothing.

`wiki-lint` reads every `.md` file under a folder, skips hidden folders such as `.obsidian` and `.git`,
and exits 1 only when there are errors. Orphans and one-way links are warnings. The `dewey` schema is the
one `VaultExport` writes; a test keeps the two in step. `register-check` reads a decision register, runs
the validation and the automatic consistency pass, and exits 1 on any finding.

The three `link-` commands read a folder of skill folders. Each folder is a skill, and every `.md` inside it
belongs to that skill; hidden folders, symlinks and anything that is not Markdown are skipped. `link-audit`
prints the skill's report and exits 1 on any Critical, High or Medium finding, since the report calls those
failures. Low findings are advice. `link-unwire` lists what refers to a skill in the order to update it
(receivers, senders, chain notes, prose). Name a replacement and it refuses to call it ready unless the
replacement is wired: wire the new skill first, then unwire the old one. None of the three changes a file.

Only `### > skill` and `### < skill` headings are read. A skill written in an older contract style, such as
`### Relationship Contract: A to B`, shows up as having no links, and `link-audit` says so when it finds no
blocks at all.

The five `council-` commands expose the Quiet Council rules without running a council. `council-classify`
prints the domain, weights and temperature overrides for a topic. `council-cost` prints the model choice and
the estimate; academic depth exits 1 until you add `--ack`. `council-session-id` reads `session-log.md` and
prints the next id for the day. `council-sources` takes tab-separated `researcher`, `tag`, `url` rows and prints
the merged registry with tiers and convergence; a malformed row is named and the exit code is 1.
`council-kg-check` prints the prior-knowledge block for a topic and any fact old enough to need
re-confirming under the domain you name (Medium decay when you name none). None of them writes a file.

`tests/cli/smoke_dewey_cli.sh <path-to-dewey-cli>` checks those promises end to end.

## Judgement calls

These are places where the rules did not give a number or a mapping and a decision had to be made.
Change them if you disagree.

- A "Strong" duplicate match allows two seconds of modification-time difference, and does not need a
  similar name.
- A month is 30 days. Dates are UTC.
- DMG and PKG files count as installers, not archives.
- Unsorted files go to a folder called `To-Sort`.
- `{n:2}` and `{n:02}` mean the same width.
- A repeated path in a tagging CSV is an error rather than "last one wins".
- A record needs ten non-empty fields to count as complete.
- ACX limits are 192 kbps or higher CBR, 44.1 kHz, MP3 only, no file over 120 minutes, RMS from -23 to
  -18 dB and peak no higher than -3 dB, all ends included. They come from ACX's own submission pages. An old
  blog post mentions a 170 MB size cap; I could not confirm a current figure, so there is no size rule.
- Missing tags are warnings, not errors. Which tags each profile wants is my choice, not the skill's:
  title, artist, album and track for music, audiobooks and ACX; title for film; title, artist, copyright
  and description for images.
- Anything not measured is reported as "unverified". It is never counted as a pass.
- Pages named index, readme or log are hubs. They need no frontmatter and are not asked to link back.
- Link matching is by skill name. `a > b/deep` is satisfied by any block in `b` that receives from `a`; the
  skill does not say how fine the match should be.
- "Vague" is a heuristic that only flags clear cases. Data of one or two words ending in data, information,
  info, output, analysis, results or stuff. An action under three words, or a stock phrase such as "use as
  needed". A feedback loop that is not "none" and is under four words or names no target. The skill gives
  examples, not a rule, so these lists are a judgement call.
- A block fails if a Critical, High or Medium finding is attached to it. Orphans, prose mentions and chain
  notes are Low and never fail a block.
- An orphan has no contract of its own and is not named by anyone else's. For the prose checks, two skills
  count as linked if a contract names either end. A prose mention is a whole-name match, so `b` is not found
  in `b-extra`, and it is reported once per file. Skills with ordinary-word names (`docs`, `router`) make a
  lot of Low noise. That is the rule working as written.
- `missing-chain-annotation` is a reading of the chain rule that the middle of a chain should say where its
  data originated: for `x > y > z`, the `y > z` block should name `x`, unless `x` has its own contract with `z`.
  It cannot tell a pass-through chain from a transformed one, so it is only ever Low.
- Chain search stops after 1000 candidates. A loop is listed once, starting at its alphabetically first skill.
- Quiet Council domain routing matches a keyword at the start of a word, so "AI" does not fire inside "Spain"
  or "said". A keyword of three letters or fewer must be the whole word, with an
  "s" or "es" allowed, and "WHO" only counts when it is capitalised. "State" and "power" still match
  "statement" and "powered", because the word-start rule cannot tell them apart.
- A topic whose top class scores twice the runner-up wins outright; otherwise the two leading classes are
  blended. Ties go to the class listed first.
- The blend's 0.5 to 2.0 clamp is gone. Every configured weight is between 0.5 and 1.6, so an average cannot
  leave the range and the clamp could never fire.
- A topic heading counts as present only as a whole line, so `## screen` is not found in `## screen-time`. A
  disputed claim is always written, with or without the heading.
- Session numbers are one more than the highest logged for the date. Counting every mention of the date's id
  would repeat or skip a number after a gap, or after an id mentioned twice. Only runs of one to
  nine digits are read; a longer run is ignored rather than overflowing.
- Keyword matching and entity extraction run on text where non-ASCII spaces become a space and non-ASCII
  punctuation becomes a full stop, so a curly quote, an em dash or a non-breaking space no longer glues two
  words together. A stray or malformed UTF-8 byte is dropped.
- A slug drops Latin-1 symbols (including the multiplication sign), the General Punctuation block and ASCII
  punctuation, keeps other letters, and cuts at 80 characters rather than bytes. A leading or trailing underscore
  does not become a hyphen.
- An entity is a run of two or more words that each look like `Capital` then lower-case ASCII, separated by
  whitespace only. "NASA", "McDonald" and accented names do not count. It is a heuristic; a model does this
  better.
- The Kaizen flag texts and actions are the quality checker's table. A flag is raised from numbers the caller
  counts; this code does not read a synthesis to find an uncited claim.
- Prices for the cost estimate are the skill's own, at 2k tokens in and 2k out per call. They have not been
  checked against Perplexity's current price list, so treat the figure as an order of magnitude.
- Temporal decay follows the sub-skill's `apply_temporal_penalty` code (`subskills/temporal-layer.md`). Each class starts losing confidence after its threshold (SLOW 5 years, MEDIUM 3,
  FAST 1, BREAKING 0.5) at a rate per year (0.010, 0.033, 0.080, 0.150) and stops at a floor (0.70, 0.60, 0.50, 0.40).
- FAST loses 20% a year and BREAKING 30% per six months in the sub-skill's prose; the rates above subtract a
  fixed amount per year, linearly, and those are the ones used.
- A fact or source is stale when its penalty is more than the threshold (default 20%), as in the sub-skill's
  check.
- Contracts are written or not at all. A placeholder, a vague field or a field spanning lines gets nothing
  back, because a weak block is worse than a missing one.
- Embeds (`![[x]]`) are not links. An unresolved link is a warning, since a note you mean to write later is
  legitimate. Two pages may share a name; only a bare-name link to a shared name is flagged.

## What is not here

- The judgement steps. Deciding what a folder is for, whether a file is worth keeping, which anchor
  fits, and whether a register entry contradicts a sub-rule all need a person or a model. The code
  validates what those steps produce. It does not produce it.
- Any hook into the Qt app. The modules are not yet called from the GUI or the headless runtime.
- Cover art, and the image and remux writers. `MetadataTags` writes MP3, M4A, FLAC and Ogg tags; it does not
  embed covers, write EXIF or XMP, or remux a container. Nothing reads real files into `MetadataResolve` or runs a plan from the command line yet,
  and nothing writes ledger lines automatically.
- Reading or measuring media. `MediaCheck` needs the facts (bit rate, levels, tags) handed to it, and nothing
  here runs ffprobe or exiftool. Noise floor is not measured at all, and `MediaCheck` says so every time.
- The Quiet Council's research itself. The Sonar calls, the thirteen prompts' wording, contradiction
  detection, steelman arguments, synthesis and the audience rewrite are model work. The code grades and files
  what those steps produce. `prior_knowledge_block` reads the graph back, but nothing yet feeds it to a prompt.
- Parts of the bidirectional-link engine. `LinkAudit` covers the audit, chain finding, the blast radius and
  replacement check for removing a skill, the type weights, and contract writing. It does not decide which link
  type fits two skills (the cross-link-mapper step, which is judgement), classify a chain link as pass-through or
  transformed, or rewrite anyone's contracts. The types and weights are a table; contracts do not carry a type.
- A full app build. These modules were built and tested standalone; nobody has yet compiled them
  inside the Qt project. The CMake wiring for the tag modules was checked in a stand-in project with the same
  TagLib block, source exclusion and link line, because Qt was not available where it was written.
