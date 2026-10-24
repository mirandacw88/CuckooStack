// Levels edited in two places brought together (LevelMerge.h): each side's changes kept where they don't touch (the
// header field by field, the course column by column, hazards as sets); a conflict named where both changed the same
// thing differently, or where the two together don't read; and a level refitted to its gauntlet's new width.
#include "TestUtil.h"
#include "editor/EditCourse.h"
#include "editor/LevelMerge.h"

#include <algorithm>
#include <string>

using namespace cs;
using namespace cs::ed;
using test::check;

namespace {

const std::string kBase = "; A test course.\nlength 30\ndifficulty 70 0.35\nraids auto\ncourse\n16 0 crate\n5 2 hay 6\n4 3 mix\n30 0 crate\nsaws\n14.1 22.5\ndiscos\n10.5 1.45\n";

std::string edited(const std::string& text, const std::function<void(Course&)>& f) {
    Course c = fromData(*parseLevel(4, text));
    f(c);
    return levelText(toData(c));
}

void clean() {
    const std::string mine = edited(kBase, [](Course& c) { addItem(c, kSawHazard, {3}); setHeight(c, 2, 2); });
    const std::string theirs = edited(kBase, [](Course& c) { setLength(c, 33); setHeight(c, 40, 1); removeItem(c, 0); }); // (removes the first item: a saw? the disco?)
    const MergeResult r = merge3(4, kBase, mine, theirs);
    check(r.ok && r.conflicts.empty(), "each side's own changes: merged cleanly");
    const auto d = parseLevel(4, r.text);
    check(d && d->length == 33 && d->course.size() > 4, "...theirs: the finish line");
    const Course c = fromData(*d);
    check(c.cols[2].h == 2 && c.cols[40].h == 1, "...the course column by column: both walls");
    check(d->xsOf(kSawHazard).front() == 3, "...the hazards as sets: my saw in");
    check(merge3(4, kBase, mine, kBase).text == mine && merge3(4, kBase, kBase, theirs).text == theirs, "one side unchanged: the other's");
}

void conflicts() {
    const std::string mine = edited(kBase, [](Course& c) { setLength(c, 31); setHeight(c, 3, 2); });
    const std::string theirs = edited(kBase, [](Course& c) { setLength(c, 35); setHeight(c, 3, 4); });
    const MergeResult r = merge3(4, kBase, mine, theirs);
    check(!r.ok && std::count(r.conflicts.begin(), r.conflicts.end(), "length") == 1 && std::count(r.conflicts.begin(), r.conflicts.end(), "course") == 1,
          "both changed the finish line and a wall differently: conflicts, named");
    check(r.text.find("length 31") != std::string::npos, "...the text keeps mine");
    const std::string same = edited(kBase, [](Course& c) { setLength(c, 31); });
    check(merge3(4, kBase, same, same).ok, "both made the same change: no conflict");
    // each fine alone, not together: my saw where their gauntlet... (a saw past the finish they shortened the course to)
    const std::string longer = edited(kBase, [](Course& c) { setLength(c, 60); });
    const std::string saw = edited(longer, [](Course& c) { addItem(c, kSawHazard, {50}); });
    const std::string shorter = edited(longer, [](Course& c) { deleteCols(c, 60, 99); setLength(c, 31); });
    const MergeResult t = merge3(4, longer, saw, shorter);
    check(!t.ok && !t.conflicts.empty() && t.conflicts.back().rfind("together", 0) == 0, "each fine, together not: a conflict saying why");
    const MergeResult bad = merge3(4, kBase, "length nope\n", theirs);
    check(!bad.ok && !bad.error.empty(), "a side that doesn't read: why");
}

void hazardsAsSets() {
    const std::string mine = edited(kBase, [](Course& c) { removeItem(c, itemNear(c, &kSawHazard, 14.1, 0, 0.1)); });
    const std::string theirs = edited(kBase, [](Course& c) { removeItem(c, itemNear(c, &kSawHazard, 14.1, 0, 0.1)); addItem(c, kDiscoHazard, {5, 2}); });
    const MergeResult r = merge3(4, kBase, mine, theirs);
    const auto d = parseLevel(4, r.text);
    check(r.ok && d && d->xsOf(kSawHazard) == std::vector<double>{22.5} && d->countOf(kDiscoHazard) == 2, "both took the same saw out (once), theirs put a disco ball in");
}

void refits() {
    const std::string level = "length 200\ndifficulty 0 0.35\nraids auto\ncourse\n20 0 crate\n168 0 gauntlet tomb\n200 0 crate\nsaws\n5 150.5\ndiscos\n150 2\n";
    const std::string wider = refit(level, "tomb", 172);
    check(wider.find("\n172 0 gauntlet tomb\n") != std::string::npos, "refitted: its span as wide as its grid");
    check(wider.find("length 202.4\n") != std::string::npos && wider.find("saws\n5 152.9\n") != std::string::npos && wider.find("discos\n152.4 2\n") != std::string::npos,
          "...what's after it moved on (the finish line, saws, disco balls), what's before it not");
    check(refit(level, "tomb", 168) == level && refit(level, "gallery", 100) == level, "as wide already, or not there: as it was");
    const std::string extra = level + "beacons\n150 3 2\n";
    check(refit(extra, "tomb", 172).find("beacons\n150 3 2\n") != std::string::npos, "a section this build doesn't know: as it was");
}

} // namespace

int main() {
    clean();
    conflicts();
    hazardsAsSets();
    refits();
    return test::finish("LevelMerge");
}
