// The meta-game helpers (Meta.h): calendar maths on "YYYY-MM-DD" strings, the FNV-1a string hash, and Record, the
// "k=v;k=v" record each meta module keeps under one storage key.
#include "TestUtil.h"
#include "core/Meta.h"

using namespace cs;
using namespace test;

namespace {

void calendar() {
    check(civilDay("1970-01-01") == 0 && civilDay("1970-01-02") == 1 && civilDay("1969-12-31") == -1, "civilDay: the epoch, either side");
    check(civilDay("2000-03-01") - civilDay("2000-02-28") == 2, "leap year divisible by 400 (2000)");
    check(civilDay("1900-03-01") - civilDay("1900-02-28") == 1, "not a leap year: divisible by 100 (1900)");
    check(civilDay("2024-12-31") - civilDay("2024-01-01") == 365, "a leap year has 366 days");
    bool roundTrip = true;
    for (int64_t d = civilDay("1899-12-25"); d < civilDay("2101-01-05"); d += 13) roundTrip = roundTrip && civilDay(civilDate(d)) == d;
    check(roundTrip, "civilDate inverts civilDay (1900-2100, every 13th day)");
    check(civilDate(0) == "1970-01-01" && civilDate(-1) == "1969-12-31" && civilDate(civilDay("2026-10-13")) == "2026-10-13",
          "civilDate: zero-padded, before the epoch too");
    // malformed: kNoDay, never a nearby real day
    for (const char* bad : {"", "bad", "2026-1-08", "2026-13-01", "2026-00-10", "2026-10-00", "2026-10-32", "26-10-2026", "2026/10/08"})
        check(civilDay(bad) == kNoDay, (std::string("malformed: \"") + bad + "\" -> kNoDay").c_str());
    check(civilDay("2026-02-29") == kNoDay && civilDay("2026-02-30") == kNoDay && civilDay("2026-04-31") == kNoDay,
          "impossible days (Feb 29 in a common year, Feb 30, Apr 31) -> kNoDay, not rolled into the next month");
    check(civilDay("2024-02-29") != kNoDay && civilDay("2026-01-31") != kNoDay && civilDay("2026-12-31") != kNoDay,
          "real month ends are fine (leap Feb 29, Jan 31, Dec 31)");
}

void fnv() {
    // FNV-1a 64-bit test vectors
    check(hashString("") == 0xcbf29ce484222325ull, "hashString: empty = the FNV offset basis");
    check(hashString("a") == 0xaf63dc4c8601ec8cull, "hashString: \"a\"");
    check(hashString("foobar") == 0x85944171f73967e8ull, "hashString: \"foobar\"");
    check(hashString("2026-10-13") != hashString("2026-10-14"), "hashString: neighbouring days differ");
}

void records() {
    FakeStorage st;
    {
        Record r(&st, "rec");
        check(!r.has("x") && r.i("x", 7) == 7 && r.l("x", -3) == -3 && r.s("x", "d") == "d", "Record: defaults when empty");
        r.set("n", 42);
        r.set("big", int64_t(1) << 40);
        r.set("name", "Sam K");
        r.set("neg", -5);
        r.save();
    }
    check(st.kv["rec"] == "big=1099511627776;n=42;name=Sam K;neg=-5;", "Record: saved as sorted k=v; pairs");
    {
        Record r(&st, "rec");
        check(r.i("n") == 42 && r.l("big") == (int64_t(1) << 40) && r.s("name") == "Sam K" && r.i("neg") == -5, "Record: reloads every type");
        r.erase("n");
        check(!r.has("n") && r.i("n", 9) == 9, "Record: erase");
        r.clear();
        check(!r.has("big") && !r.has("name"), "Record: clear");
        r.save();
    }
    check(st.kv["rec"].empty(), "Record: a cleared record saves empty");
    st.kv["junk"] = "a=1;noequals;b=;=c;d=4";
    Record j(&st, "junk");
    check(j.i("a") == 1 && j.i("d") == 4 && !j.has("noequals"), "Record: skips items without '='");
    check(j.has("b") && j.i("b", 8) == 8 && j.s("b", "x").empty(), "Record: an empty value reads as the number default, an empty string");
    check(j.s("") == "c", "Record: an empty key is kept as written");
    Record none(nullptr, "k");
    none.set("a", 1);
    none.save();
    check(none.i("a") == 1, "Record: works without storage (nothing persisted)");
}

} // namespace

int main() {
    calendar();
    fnv();
    records();
    return finish("Meta");
}
