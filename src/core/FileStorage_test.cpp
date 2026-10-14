// FileStorage (FileStorage.cpp): the IStorage every platform saves the player's progress in, one "key=value" text
// file written through a temporary file and a rename. Runs in a scratch directory under the build folder.
#include "TestUtil.h"
#include "core/FileStorage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace cs;
using namespace test;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

void roundTrip(const fs::path& dir) {
    const std::string path = (dir / "save.txt").string();
    {
        FileStorage s(path);
        check(!s.get("coins"), "a missing file: empty storage, no crash");
        s.set("coins", "120");
        s.set("cluckstack-profile", "aud=2;f_replay_asked=1;");
        s.set("name", "Sam K the 2nd");
        check(s.get("coins") == "120", "get returns what was set");
    }
    check(fs::exists(path) && !fs::exists(path + ".tmp"), "written through a temporary file, renamed into place");
    {
        FileStorage s(path);
        check(s.get("coins") == "120" && s.get("name") == "Sam K the 2nd", "a relaunch reads everything back (spaces kept)");
        check(s.get("cluckstack-profile") == "aud=2;f_replay_asked=1;", "values may contain '=' and ';' (meta records)");
        s.set("coins", "95");
        check(s.get("coins") == "95", "overwrite");
        s.set("empty", "");
    }
    FileStorage again(path);
    check(again.get("coins") == "95" && again.get("empty") == "", "the overwrite and an empty value persist");
    check(slurp(path).find("coins=95\n") != std::string::npos && slurp(path).find("coins=120") == std::string::npos,
          "one line per key, the old value gone");
}

void survivesJunk(const fs::path& dir) {
    const fs::path p = dir / "junk.txt";
    { std::ofstream f(p); f << "ok=1\nno equals sign\n=blankkey\nlast=2"; }
    FileStorage s(p.string());
    check(s.get("ok") == "1" && s.get("last") == "2", "a hand-edited file: good lines load");
    check(!s.get("no equals sign"), "lines without '=' are skipped");
}

void unwritable(const fs::path& dir) {
    FileStorage s((dir / "no-such-dir" / "save.txt").string());
    s.set("k", "v");
    check(s.get("k") == "v", "an unwritable path: still works for this session (nothing thrown)");
}

} // namespace

int main() {
    const fs::path dir = fs::path("FileStorage_test.tmp");
    fs::remove_all(dir);
    fs::create_directories(dir);
    roundTrip(dir);
    survivesJunk(dir);
    unwritable(dir);
    fs::remove_all(dir);
    return finish("FileStorage");
}
