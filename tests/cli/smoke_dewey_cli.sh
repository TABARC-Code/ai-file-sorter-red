#!/usr/bin/env bash
# Smoke test for dewey-cli. Usage: smoke_dewey_cli.sh /path/to/dewey-cli
# Checks the promises that matter: dry run touches nothing, apply and undo round-trip,
# nothing is overwritten or deleted, duplicates are reported only.
set -u
CLI=${1:?path to dewey-cli}
fails=0
check() { if [ "$2" = "0" ]; then echo "ok    $1"; else echo "FAIL  $1"; fails=$((fails + 1)); fi; }

work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
dir="$work/dl"; mkdir -p "$dir"
echo "invoice" > "$dir/invoice final (1).pdf"
echo "pic" > "$dir/holiday photo.jpg"
echo "code" > "$dir/tool.py"
echo "keep me" > "$dir/project-x-notes.txt"
echo ".hidden" > "$dir/.secret"
mkdir "$dir/SomeFolder"
before=$(cd "$dir" && find . | sort)

"$CLI" organise "$dir" > "$work/dry.txt"; rc=$?
check "dry run exits 0" $rc
after=$(cd "$dir" && find . | sort)
[ "$before" = "$after" ]; check "dry run changes nothing" $?
grep -q "dry run" "$work/dry.txt"; check "dry run says so" $?

"$CLI" organise "$dir" --apply --protect project-x --log "$work/log.tsv" > "$work/apply.txt"; rc=$?
check "apply exits 0" $rc
[ -f "$dir/Images/holiday-photo.jpg" ] || [ -f "$dir/Images/holiday photo.jpg" ]; check "image filed under Images" $?
[ -f "$dir/Code/tool.py" ]; check "code filed under Code" $?
[ -f "$dir/project-x-notes.txt" ]; check "protected file left alone" $?
[ -f "$dir/.secret" ] && [ -d "$dir/SomeFolder" ]; check "hidden file and folder left alone" $?
[ -s "$work/log.tsv" ]; check "undo log written" $?

"$CLI" undo "$dir" --log "$work/log.tsv" --apply > /dev/null; rc=$?
check "undo exits 0" $rc
[ -f "$dir/tool.py" ] && [ -f "$dir/project-x-notes.txt" ]; check "undo restores files" $?

# Never overwrite: block a destination, then apply.
mkdir -p "$dir/Code"; echo "other" > "$dir/Code/tool.py"
"$CLI" organise "$dir" --apply --log "$work/log2.tsv" > /dev/null 2>&1
[ "$(cat "$dir/Code/tool.py")" = "other" ]; check "existing destination never overwritten" $?

# Symlinks (including broken ones) are left alone, not archived as 1970.
sl="$work/sl"; mkdir -p "$sl"; ln -s /nonexistent "$sl/broken.pdf"; echo t > "$sl/real.txt"; ln -s "$sl/real.txt" "$sl/link.txt"
plan=$("$CLI" organise "$sl" 2>&1)
echo "$plan" | grep -q "broken.pdf  ->" ; [ $? -ne 0 ]; check "plan never moves a symlink" $?
echo "$plan" | grep -q "skip  broken.pdf"; check "plan says why the symlink is skipped" $?
"$CLI" organise "$sl" --apply --log "$work/log3.tsv" > /dev/null 2>&1; check "apply with symlinks exits 0" $?
[ -L "$sl/broken.pdf" ] && [ -L "$sl/link.txt" ] && [ ! -e "$sl/Archive" ]; check "symlinks left in place" $?

# Duplicates: report only.
dup="$work/dup"; mkdir -p "$dup/a" "$dup/b"
echo "same content here" > "$dup/a/one.txt"; cp "$dup/a/one.txt" "$dup/b/two.txt"
echo "different" > "$dup/a/three.txt"; : > "$dup/a/empty1"; : > "$dup/b/empty2"
out=$("$CLI" duplicates "$dup")
echo "$out" | grep -q "1 group"; check "one duplicate group found (empties ignored)" $?
[ -f "$dup/a/one.txt" ] && [ -f "$dup/b/two.txt" ]; check "duplicates left in place" $?

# Hash matches the system tool.
f="$work/h.bin"; head -c 5000 /dev/urandom > "$f"
[ "$("$CLI" sha256 "$f" | cut -d' ' -f1)" = "$(sha256sum "$f" | cut -d' ' -f1)" ]; check "sha256 matches sha256sum" $?

# wiki-lint: a clean vault passes, a broken one fails, warnings alone do not fail the run.
vault="$work/vault"; mkdir -p "$vault/sub" "$vault/.obsidian"
printf -- '---\ntitle: A\ntype: concept\ntags: [x]\nsummary: About A\n---\nSee [[b]]\n' > "$vault/a.md"
printf -- '---\ntitle: B\ntype: concept\ntags: [x]\nsummary: About B\n---\nSee [[a]]\n' > "$vault/sub/b.md"
printf 'not a note, no frontmatter\n' > "$vault/.obsidian/ignored.md"
"$CLI" wiki-lint "$vault" > "$work/wl.txt"; rc=$?
check "clean vault exits 0" $rc
grep -q "0 error" "$work/wl.txt"; check "clean vault reports zero errors" $?
grep -q "ignored.md" "$work/wl.txt"; [ $? -ne 0 ]; check "hidden folders are skipped" $?
printf -- '---\ntitle: C\n---\nSee [[ghost]]\n' > "$vault/c.md"
"$CLI" wiki-lint "$vault" > "$work/wl2.txt"; [ $? -eq 1 ]; check "missing fields exit 1" $?
grep -q "missing field: summary" "$work/wl2.txt"; check "names the missing field" $?
grep -q "broken-link" "$work/wl2.txt"; check "reports the broken link" $?
rm "$vault/c.md"; printf -- '---\ntitle: D\ntype: t\ntags: [x]\nsummary: s\n---\nnothing linked\n' > "$vault/d.md"
"$CLI" wiki-lint "$vault" > "$work/wl3.txt"; check "warnings alone exit 0" $?
grep -q "orphan" "$work/wl3.txt"; check "orphan reported as a warning" $?
"$CLI" wiki-lint "$vault" --schema dewey > "$work/wl4.txt"; [ $? -eq 1 ]; check "dewey schema asks for different fields" $?
grep -q "missing field: anchor_code" "$work/wl4.txt"; check "dewey schema names its fields" $?
"$CLI" wiki-lint "$vault" --schema nonsense >/dev/null 2>&1; [ $? -eq 2 ]; check "unknown schema exits 2" $?
"$CLI" wiki-lint "$work/nope" >/dev/null 2>&1; [ $? -eq 1 ]; check "wiki-lint on a missing folder exits 1" $?

# register-check: reads a decision register and runs the automatic consistency pass.
reg="$work/register.md"
entry() { printf '## Entry %03d\n\n### Rule\nR\n\n### Reason\nWhy\n\n### Trigger pattern\n%s\n\n### Anchor guidance\n%s\n\n### Link guidance\n\n### Organisation guidance\n\n### Example\n\n### Supersedes\nnothing\n\n### Status\nactive\n\n### Reviewed\ntoday\n\n' "$1" "$2" "$3"; }
{ printf '# Decision register\n\n'; entry 1 "scanned receipts" "330.ECON"; entry 2 "mixed folders" "006.3.AI"; } > "$reg"
"$CLI" register-check "$reg" > "$work/rc1.txt"; rc=$?
check "consistent register exits 0" $rc
grep -q "2 entr" "$work/rc1.txt"; check "counts the entries" $?
{ printf '# Decision register\n\n'; entry 1 "scanned receipts" "330.ECON"; entry 2 "Scanned  Receipts" "999.NOPE"; } > "$reg"
"$CLI" register-check "$reg" > "$work/rc2.txt"; [ $? -eq 1 ]; check "inconsistent register exits 1" $?
grep -q "same trigger" "$work/rc2.txt"; check "reports the shared trigger" $?
grep -q "999.NOPE" "$work/rc2.txt"; check "reports the unknown anchor" $?
"$CLI" register-check "$work/none.md" >/dev/null 2>&1; [ $? -eq 1 ]; check "register-check on a missing file exits 1" $?
"$CLI" register-check 2>/dev/null; [ $? -eq 2 ]; check "register-check without a file exits 2" $?

# link-audit / link-chains / link-unwire: a folder of skill folders, contracts in any .md inside each.
cblock() {
  if [ "$1" = ">" ]; then d="Sends"; a="Action for target"; else d="Receives"; a="Action here"; fi
  printf '### %s %s\n\n**%s:** wound and lie chain with arc stages\n**%s:** Cross-reference the received beats against the stage map\n**Feedback loop:** none\n\n' "$1" "$2" "$d" "$a"
}
eco="$work/eco"; mkdir -p "$eco/alpha" "$eco/beta/subskills" "$eco/gamma" "$eco/delta" "$eco/.git"
{ printf '# alpha\n\n'; cblock '>' beta; } > "$eco/alpha/SKILL.md"
printf '# beta\n\n' > "$eco/beta/SKILL.md"
{ cblock '<' alpha; cblock '>' gamma; } > "$eco/beta/subskills/intake.md"
{ printf '# gamma\n\n'; cblock '<' beta; } > "$eco/gamma/SKILL.md"
printf '# delta\n\nnothing wired here\n' > "$eco/delta/SKILL.md"
cblock '>' nothing > "$eco/.git/bad.md"
cblock '>' ghost > "$work/outside.md"; ln -s "$work/outside.md" "$eco/alpha/linked.md"
mkdir -p "$eco/alpha/.drafts" "$work/outside_skill"
cblock '>' ghost2 > "$eco/alpha/.drafts/bad.md"
cblock '>' ghost4 > "$eco/alpha/notes.txt"
cblock '>' ghost3 > "$work/outside_skill/SKILL.md"; ln -s "$work/outside_skill" "$eco/linkedskill"
snap() { (cd "$1" && find . | sort && find . -type f -exec cat {} + | cksum); }
eco_before=$(snap "$eco")

"$CLI" link-audit "$eco" > "$work/la1.txt"; rc=$?
check "link-audit passes a wired ecosystem (orphans are only warnings)" $rc
grep -q "## Bidirectional Link Audit Report" "$work/la1.txt"; check "link-audit prints the report" $?
grep -q "Total contracts checked: 4" "$work/la1.txt"; check "link-audit reads contracts inside sub-skill files" $?
grep -q "Pass rate: 100.0%" "$work/la1.txt"; check "link-audit reports the pass rate" $?
grep -q "orphan-skill.*delta" "$work/la1.txt"; check "link-audit reports the orphan" $?
! grep -q "nothing wired" "$work/la1.txt" && ! grep -q "ghost" "$work/la1.txt" && ! grep -q "missing-target" "$work/la1.txt"; check "link-audit skips hidden folders, symlinked files, symlinked skills and non-Markdown files" $?
grep -q "(4 skills)" "$work/la1.txt"; check "link-audit counts the skills" $?
grep -Eq "^Date: [0-9]{4}-[0-9]{2}-[0-9]{2}$" "$work/la1.txt"; check "link-audit dates the report" $?
[ "$eco_before" = "$(snap "$eco")" ]; check "link-audit changes nothing" $?

cp "$eco/beta/subskills/intake.md" "$work/intake.bak"
cblock '>' gamma > "$eco/beta/subskills/intake.md"
"$CLI" link-audit "$eco" > "$work/la2.txt"; [ $? -eq 1 ]; check "link-audit exits 1 on a missing backlink" $?
grep -q "missing-backlink" "$work/la2.txt"; check "link-audit names the broken link" $?
grep -q "alpha/SKILL.md:3" "$work/la2.txt"; check "link-audit gives file and line" $?
cp "$work/intake.bak" "$eco/beta/subskills/intake.md"

# A Medium finding alone still fails the audit: the report calls it a failure.
cp "$eco/alpha/SKILL.md" "$work/alpha.bak"
sed 's/wound and lie chain with arc stages/character data/' "$work/alpha.bak" > "$eco/alpha/SKILL.md"
"$CLI" link-audit "$eco" > "$work/la3.txt"; [ $? -eq 1 ]; check "link-audit exits 1 on a vague field alone" $?
grep -q "vague-field" "$work/la3.txt"; check "link-audit names the vague field" $?
cp "$work/alpha.bak" "$eco/alpha/SKILL.md"

"$CLI" link-audit "$work/nope" >/dev/null 2>&1; [ $? -eq 1 ]; check "link-audit on a missing folder exits 1" $?
"$CLI" link-audit 2>/dev/null; [ $? -eq 2 ]; check "link-audit without a folder exits 2" $?
"$CLI" link-audit "$eco" extra 2>/dev/null; [ $? -eq 2 ]; check "link-audit with two arguments exits 2" $?

"$CLI" link-chains "$eco" > "$work/lc.txt"; rc=$?
check "link-chains exits 0" $rc
grep -q "^alpha > beta > gamma$" "$work/lc.txt"; check "link-chains prints the chain" $?
grep -q "1 chain" "$work/lc.txt"; check "link-chains counts chains" $?
# Skills written in an older contract style have no "### >" or "### <" headings. Say so rather than print an empty pass.
eco4="$work/eco4"; mkdir -p "$eco4/old-style"
printf '# old\n\n### Relationship Contract: old-style to other\n**Sends:** things\n' > "$eco4/old-style/SKILL.md"
"$CLI" link-audit "$eco4" > "$work/la4.txt"; check "link-audit on skills with no v2 blocks still exits 0" $?
grep -q "Note: no contract blocks were found" "$work/la4.txt"; check "link-audit says when it found no contract blocks" $?
! grep -q "Note: no contract blocks were found" "$work/la1.txt"; check "link-audit stays quiet about that when blocks exist" $?
"$CLI" link-chains "$work/nope" >/dev/null 2>&1; [ $? -eq 1 ]; check "link-chains on a missing folder exits 1" $?
eco3="$work/eco3"; mkdir -p "$eco3/a" "$eco3/b" "$eco3/c"
{ cblock '>' b; cblock '<' c; } > "$eco3/a/SKILL.md"
{ cblock '<' a; cblock '>' c; } > "$eco3/b/SKILL.md"
{ cblock '<' b; cblock '>' a; } > "$eco3/c/SKILL.md"
"$CLI" link-chains "$eco3" > "$work/lc2.txt"; grep -q "^a > b > c  (loops back)$" "$work/lc2.txt"; check "link-chains marks a loop, once" $?
[ "$(grep -c '>' "$work/lc2.txt")" -eq 1 ]; check "link-chains does not repeat a loop from each start" $?
"$CLI" link-audit "$eco3" > /dev/null; check "link-audit passes a loop of wired skills" $?

"$CLI" link-unwire "$eco" beta > "$work/lu.txt"; rc=$?
check "link-unwire exits 0" $rc
sed -n 1p "$work/lu.txt" | grep -q "gamma/SKILL.md:3"; check "link-unwire lists receivers first" $?
sed -n 2p "$work/lu.txt" | grep -q "alpha/SKILL.md:3"; check "link-unwire lists senders second" $?
! grep -q "beta/subskills" "$work/lu.txt"; check "link-unwire leaves out the departing skill's own files" $?
"$CLI" link-unwire "$eco" nosuch >/dev/null 2>&1; [ $? -eq 1 ]; check "link-unwire on an unknown skill exits 1" $?
"$CLI" link-unwire "$eco" 2>/dev/null; [ $? -eq 2 ]; check "link-unwire without a skill exits 2" $?
"$CLI" link-unwire "$eco" alpha gamma > "$work/lu2.txt"; rc=$?
check "link-unwire accepts a wired replacement" $rc
grep -q "replacement gamma: wired" "$work/lu2.txt"; check "link-unwire says the replacement is wired" $?
"$CLI" link-unwire "$eco" alpha delta > "$work/lu3.txt"; [ $? -eq 1 ]; check "link-unwire refuses an unwired replacement" $?
grep -q "replacement delta: NOT wired" "$work/lu3.txt"; check "link-unwire says to wire it first" $?
"$CLI" link-unwire "$eco" alpha nosuch > "$work/lu4.txt" 2>&1; [ $? -eq 1 ]; check "link-unwire with an unknown replacement exits 1" $?
grep -q "no such skill: nosuch" "$work/lu4.txt"; check "link-unwire says the replacement does not exist" $?
"$CLI" link-unwire "$eco" alpha gamma extra 2>/dev/null; [ $? -eq 2 ]; check "link-unwire with too many arguments exits 2" $?
[ "$eco_before" = "$(snap "$eco")" ]; check "link-chains and link-unwire change nothing" $?

# Quiet Council rules.
"$CLI" council-classify "clinical trial vaccine" > "$work/cc1.txt"; rc=$?
check "council-classify exits 0" $rc
grep -qx "domain  SCIENTIFIC_MEDICAL" "$work/cc1.txt"; check "council-classify names the domain" $?
grep -qx "weight  The Scientist  1.50" "$work/cc1.txt"; check "council-classify lists weights" $?
grep -qx "temperature  The Scientist  0.15" "$work/cc1.txt"; check "council-classify lists temperature overrides" $?
"$CLI" council-classify "Rainfall in Spain" | grep -qx "domain  GENERAL"; check "council-classify falls back to GENERAL" $?
"$CLI" council-classify "annual report" --constraint "peer-reviewed clinical evidence" | grep -qx "domain  SCIENTIFIC_MEDICAL"; check "council-classify reads --constraint" $?
"$CLI" council-classify "AI regulation" | grep -qx "domain  LEGAL_REGULATORY+TECHNOLOGY_AI"; check "council-classify blends close domains" $?
"$CLI" council-classify 2>/dev/null; [ $? -eq 2 ]; check "council-classify without a topic exits 2" $?

"$CLI" council-cost standard > "$work/cost1.txt"; rc=$?
check "council-cost exits 0" $rc
grep -qx "models  sonar-pro / sonar-deep-research" "$work/cost1.txt"; check "council-cost names the models" $?
grep -qx "total  USD 5.452" "$work/cost1.txt"; check "council-cost totals standard depth" $?
"$CLI" council-cost quick | grep -qx "total  USD 0.084"; check "council-cost totals quick depth" $?
"$CLI" council-cost academic >/dev/null 2>"$work/cost2.err"; [ $? -eq 1 ]; check "council-cost refuses academic without --ack" $?
grep -q "academic" "$work/cost2.err"; check "council-cost explains the refusal" $?
"$CLI" council-cost academic --ack | grep -qx "total  USD 65.260"; check "council-cost accepts academic with --ack" $?
"$CLI" council-cost thorough 2>/dev/null; [ $? -eq 2 ]; check "council-cost with an unknown depth exits 2" $?
"$CLI" council-cost 2>/dev/null; [ $? -eq 2 ]; check "council-cost without a depth exits 2" $?

kg="$work/kg"; mkdir -p "$kg"
"$CLI" council-session-id "$kg" --date 2026-11-20 | grep -qx "session-2026-11-20-001"; check "council-session-id starts at 001" $?
printf '## session-2026-11-20-001\n\n## session-2026-11-19-007\n' > "$kg/session-log.md"
"$CLI" council-session-id "$kg" --date 2026-11-20 | grep -qx "session-2026-11-20-002"; check "council-session-id counts up" $?
"$CLI" council-session-id "$kg" --date 2026-11-21 | grep -qx "session-2026-11-21-001"; check "council-session-id restarts each day" $?
"$CLI" council-session-id "$kg" --date 2026-13-45 2>/dev/null; [ $? -eq 2 ]; check "council-session-id rejects a bad date" $?
"$CLI" council-session-id "$work/nope" 2>/dev/null; [ $? -eq 1 ]; check "council-session-id on a missing folder exits 1" $?
"$CLI" council-session-id 2>/dev/null; [ $? -eq 2 ]; check "council-session-id without a folder exits 2" $?
[ "$(cat "$kg/session-log.md")" = "$(printf '## session-2026-11-20-001\n\n## session-2026-11-19-007')" ]; check "council-session-id changes nothing" $?

tsv="$work/cites.tsv"
printf 'The Scientist\t[SOURCE_1]\thttps://www.nature.com/articles/x?utm_source=a\nThe Historian\t[SOURCE_1]\thttps://www.nature.com/articles/x\nThe Lawyer\t[SOURCE_1]\thttps://www.nature.com/articles/x/\nThe Lawyer\t[SOURCE_2]\thttps://someblog.example/post\n' > "$tsv"
"$CLI" council-sources "$tsv" > "$work/src1.txt"; rc=$?
check "council-sources exits 0" $rc
grep -q "^SOURCE_1  TIER_1  https://www.nature.com/articles/x?utm_source=a  found by: The Scientist, The Historian, The Lawyer  convergent$" "$work/src1.txt"; check "council-sources merges duplicate URLs and marks convergence" $?
grep -q "^SOURCE_2  TIER_3  https://someblog.example/post  found by: The Lawyer$" "$work/src1.txt"; check "council-sources keeps a single-finder source plain" $?
grep -q "^2 source(s): tier 1 1, tier 2 0, tier 3 1, tier 4 0, convergent 1$" "$work/src1.txt"; check "council-sources summarises" $?
printf 'only two\tfields\n' > "$work/bad.tsv"
"$CLI" council-sources "$work/bad.tsv" >/dev/null 2>"$work/src2.err"; [ $? -eq 1 ]; check "council-sources rejects a malformed line" $?
grep -q "line 1" "$work/src2.err"; check "council-sources names the bad line" $?
"$CLI" council-sources "$work/nope.tsv" >/dev/null 2>&1; [ $? -eq 1 ]; check "council-sources on a missing file exits 1" $?
"$CLI" council-sources 2>/dev/null; [ $? -eq 2 ]; check "council-sources without a file exits 2" $?

cat > "$kg/confirmed-facts.md" <<'FACTS'
# Confirmed Facts

## screen-time
- **FACT-001** Old claim [SOURCE_1]
  - Confirmed by: A, B, C
  - Session: session-2020-01-01-001
  - Confidence: HIGH

- **FACT-002** Fresh claim
  - Confirmed by: A, B, C
  - Session: session-2026-11-01-001
  - Confidence: HIGH
FACTS
"$CLI" council-kg-check "$kg" --topic "Screen Time" --domain TECHNOLOGY_AI --today 2026-11-20 > "$work/kg1.txt"; rc=$?
check "council-kg-check exits 0" $rc
grep -qx "topic  screen-time" "$work/kg1.txt"; check "council-kg-check prints the slug" $?
grep -qx -e "- Old claim" "$work/kg1.txt" && grep -qx -e "- Fresh claim" "$work/kg1.txt"; check "council-kg-check prints the prior knowledge block" $?
grep -qx "stale  FACT-001  2020-01-01" "$work/kg1.txt"; check "council-kg-check flags an old fact" $?
! grep -q "stale  FACT-002" "$work/kg1.txt"; check "council-kg-check leaves a recent fact alone" $?
"$CLI" council-kg-check "$kg" --topic "Other Topic" | grep -qx "no prior knowledge for other-topic"; check "council-kg-check says when there is nothing" $?
"$CLI" council-kg-check "$kg" --topic "Screen Time" --today 2026-02-30 2>/dev/null; [ $? -eq 2 ]; check "council-kg-check rejects a bad date" $?
"$CLI" council-kg-check "$kg" 2>/dev/null; [ $? -eq 2 ]; check "council-kg-check without a topic exits 2" $?
"$CLI" council-kg-check "$work/nope" --topic x >/dev/null 2>&1; [ $? -eq 1 ]; check "council-kg-check on a missing folder exits 1" $?

# Usage errors.
"$CLI" frobnicate 2>/dev/null; [ $? -eq 2 ]; check "unknown command exits 2" $?
"$CLI" 2>/dev/null; [ $? -eq 2 ]; check "no arguments exits 2" $?
"$CLI" organise 2>/dev/null; [ $? -eq 2 ]; check "organise without folder exits 2" $?
"$CLI" organise "$dir" --bogus 2>/dev/null; [ $? -eq 2 ]; check "unknown option exits 2" $?
"$CLI" organise "$work/nope" >/dev/null 2>&1; [ $? -eq 1 ]; check "missing folder exits 1" $?
[ "$("$CLI" anchors | wc -l)" -eq 45 ]; check "45 anchors listed" $?

[ $fails -eq 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
