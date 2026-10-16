// Artist PNGs in the HUD atlas (HudImg.h): the sizes the game assumes match the PNGs and the padding they were packed
// with (make_hud_icons.py), the UV maths finds them exactly (the atlas pixels there are the PNG's), the 3-slice and
// 9-slice cuts tile their rect on screen and the image in the atlas, and the packer leaves no stray art in the cells
// no sprite owns.
#include "TestUtil.h"
#include "core/HudImage.h"
#include "core/HudImg.h"

#include <stb_image.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace cs;
using namespace test;

namespace {

struct Named { const char* file; HudImg img; };
const Named kImgs[] = {{"drop_frame", kDropFrame},       {"drop_tile_green", kDropTileGreen}, {"drop_tile_purple", kDropTilePurple},
                       {"drop_tile_blue", kDropTileBlue}, {"drop_tile_pink", kDropTilePink},   {"drop_claim", kDropClaim},
                       {"drop_claim2", kDropClaim2}};

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string png(const char* name) { return std::string(CS_SOURCE_DIR) + "/assets/hud/shop/" + name + ".png"; }
// (the engine's stb_image is built without file I/O: read the file, decode from memory)
std::string bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool pngInfo(const std::string& path, int& w, int& h) {
    const std::string b = bytes(path);
    int c = 0;
    return !b.empty() && stbi_info_from_memory(reinterpret_cast<const unsigned char*>(b.data()), int(b.size()), &w, &h, &c);
}
unsigned char* pngLoad(const std::string& path, int& w, int& h) {
    const std::string b = bytes(path);
    int c = 0;
    return b.empty() ? nullptr : stbi_load_from_memory(reinterpret_cast<const unsigned char*>(b.data()), int(b.size()), &w, &h, &c, 4);
}

void sizesAndPadding() {
    // the PNGs' sizes (extract_drop_sprites.py's output) ...
    bool sizes = true;
    for (const Named& n : kImgs) {
        int w = 0, h = 0;
        sizes = sizes && pngInfo(png(n.file), w, h) && float(w) == n.img.w && float(h) == n.img.h;
    }
    check(sizes, "each image's size in HudImg.h is its PNG's (re-cut art can't silently misplace it)");
    // ... and the padding make_hud_icons.py packed each with: ("DropX", [""], {... "paste": ("drop_x", pad, glow)})
    const std::string script = slurp(std::string(CS_SOURCE_DIR) + "/scripts/hud/make_hud_icons.py");
    bool pads = true;
    for (const Named& n : kImgs) {
        std::smatch m;
        const std::regex re("\"paste\": \\(\"" + std::string(n.file) + "\", (\\d+), ");
        pads = pads && std::regex_search(script, m, re) && std::stof(m[1]) == n.img.pad;
    }
    check(pads, "...and its padding is the one make_hud_icons.py packs it with");
}

void uvMaths() {
    // the whole image: inside its cell(s), centred, its aspect kept, as big as the padding allows
    bool inside = true, centred = true, aspect = true, fills = true;
    for (const Named& n : kImgs) {
        const HudImg& img = n.img;
        const glm::vec4 uv = hudImageUV(img, {0, 0, img.w, img.h});
        const int i = int(img.icon), span = iconSpan(img.icon), rows = iconRowSpan(img.icon);
        const glm::vec4 cell{float(i % kIconCols) / kIconCols, float(i / kIconCols) / kIconRows, float(i % kIconCols + span) / kIconCols,
                             float(i / kIconCols + rows) / kIconRows};
        const float pu = 1.f / (kIconCols * kAtlasCellPx), pv = 1.f / (kIconRows * kAtlasCellPx); // one atlas pixel
        inside = inside && uv.x >= cell.x + img.pad * pu - 1e-6f && uv.z <= cell.z - img.pad * pu + 1e-6f && uv.y >= cell.y + img.pad * pv - 1e-6f &&
                 uv.w <= cell.w - img.pad * pv + 1e-6f;
        centred = centred && std::abs((uv.x + uv.z) - (cell.x + cell.z)) < 1e-5f && std::abs((uv.y + uv.w) - (cell.y + cell.w)) < 1e-5f;
        const float wPx = (uv.z - uv.x) / pu, hPx = (uv.w - uv.y) / pv;
        aspect = aspect && std::abs(wPx / hPx - img.w / img.h) < 1e-3f;
        fills = fills && (std::abs(wPx - (kAtlasCellPx * span - 2 * img.pad)) < 0.01f || std::abs(hPx - (kAtlasCellPx * rows - 2 * img.pad)) < 0.01f);
    }
    check(inside && centred, "an image sits inside its cell(s), inside the padding, centred");
    check(aspect && fills, "...scaled to fit, its aspect kept");
    // a sub-rect maps linearly: the left half of an image is the left half of its UV rect
    const glm::vec4 all = hudImageUV(kDropClaim, {0, 0, kDropClaim.w, kDropClaim.h}), half = hudImageUV(kDropClaim, {0, 0, kDropClaim.w / 2, kDropClaim.h});
    check(std::abs(half.z - (all.x + all.z) / 2) < 1e-6f && half.y == all.y && half.w == all.w, "a part of the image maps to that part of its UV rect");
}

void atlasPixels() {
    // the atlas holds each PNG where the maths says (both premultiplied; a grid of samples well inside the image)
    HudImage atlas;
    check(atlas.buildIcons(), "the atlas decodes");
    const auto& A = atlas.pixels();
    bool match = true;
    for (const Named& n : kImgs) {
        int w = 0, h = 0;
        unsigned char* src = pngLoad(png(n.file), w, h);
        if (!src) { match = false; continue; }
        const glm::vec4 uv = hudImageUV(n.img, {0, 0, n.img.w, n.img.h});
        double err = 0;
        int samples = 0;
        for (int gy = 1; gy < 10; ++gy)
            for (int gx = 1; gx < 10; ++gx) {
                const float fx = gx / 10.f, fy = gy / 10.f;
                const int sx = int(fx * w), sy = int(fy * h);
                const int ax = int((uv.x + fx * (uv.z - uv.x)) * atlas.width()), ay = int((uv.y + fy * (uv.w - uv.y)) * atlas.height());
                const unsigned char* s = src + (size_t(sy) * w + sx) * 4;
                const unsigned char* a = A.data() + (size_t(ay) * atlas.width() + ax) * 4;
                for (int k = 0; k < 4; ++k) {
                    const float sv = k < 3 ? s[k] * s[3] / 255.f : s[3]; // premultiply the PNG like the atlas
                    err += std::abs(sv - float(a[k]));
                }
                ++samples;
            }
        stbi_image_free(src);
        const double mean = err / (samples * 4);
        match = match && mean < 14.0; // (resampled to the atlas's scale: close, not identical)
        if (mean >= 14.0) std::printf("  %s: mean difference %.1f\n", n.file, mean);
    }
    check(match, "the atlas pixels at each image's UV rect are that PNG's");
}

void threeSlice() {
    // a button stretched to 3x its width: three pieces, edge to edge, covering the rect; the caps keep their shape
    std::array<HudQuad, 3> q;
    const glm::vec2 c{200, 100}, size{3 * kDropClaim.w * 0.4f, kDropClaim.h * 0.4f};
    const int n = hudImageThreeSlice(kDropClaim, c, size, 0, q);
    const bool joined = n == 3 && std::abs((q[0].c.x + q[0].size.x / 2) - (q[1].c.x - q[1].size.x / 2)) < 1e-3f &&
                        std::abs((q[1].c.x + q[1].size.x / 2) - (q[2].c.x - q[2].size.x / 2)) < 1e-3f;
    const bool covers = std::abs((q[0].c.x - q[0].size.x / 2) - (c.x - size.x / 2)) < 1e-3f && std::abs((q[2].c.x + q[2].size.x / 2) - (c.x + size.x / 2)) < 1e-3f;
    check(joined && covers && q[0].size.y == size.y, "3-slice: three pieces, edge to edge, covering the rect");
    const float capAspect = q[0].size.x / q[0].size.y, srcAspect = (kDropClaim.h / 2) / kDropClaim.h;
    check(std::abs(capAspect - srcAspect) < 1e-4f && std::abs(q[2].size.x - q[0].size.x) < 1e-4f, "...the caps keep their shape (only the middle stretches)");
    const bool uvJoined = std::abs(q[0].uv.z - q[1].uv.x) < 1e-7f && std::abs(q[1].uv.z - q[2].uv.x) < 1e-7f;
    const glm::vec4 whole = hudImageUV(kDropClaim, {0, 0, kDropClaim.w, kDropClaim.h});
    check(uvJoined && q[0].uv.x == whole.x && q[2].uv.z == whole.z, "...and sample the whole image, piece after piece");
    // a wider left cap (the x2 button's TV icon mustn't stretch)
    hudImageThreeSlice(kDropClaim2, c, {400, 60}, 112, q);
    check(std::abs(q[0].size.x / q[0].size.y - 112.f / kDropClaim2.h) < 1e-4f && std::abs(q[0].uv.z - hudImageUV(kDropClaim2, {0, 0, 112, kDropClaim2.h}).z) < 1e-7f,
          "...a custom left cap keeps its width (in image pixels) and shape");
    // narrower than its two caps: no middle, the caps split the width
    const int n2 = hudImageThreeSlice(kDropClaim, c, {30, 60}, 0, q);
    check(n2 == 2 && std::abs(q[0].size.x + q[1].size.x - 30) < 1e-3f, "...too narrow for a middle: just the two caps, splitting the width");
}

void nineSlice() {
    std::array<HudQuad, 9> q;
    const glm::vec2 c{215, 400}, size{390, 400};
    const int n = hudImageNineSlice(kDropFrame, c, size, {200, 89}, {106, 48}, q);
    check(n == 9, "9-slice: nine pieces");
    float area = 0;
    bool rowsJoin = true, colsJoin = true;
    for (int i = 0; i < 9; ++i) area += q[i].size.x * q[i].size.y;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 2; ++k) {
            const HudQuad &a = q[r * 3 + k], &b = q[r * 3 + k + 1];
            rowsJoin = rowsJoin && std::abs((a.c.x + a.size.x / 2) - (b.c.x - b.size.x / 2)) < 1e-3f && std::abs(a.uv.z - b.uv.x) < 1e-7f;
            const HudQuad &u = q[k * 3 + r], &d = q[(k + 1) * 3 + r];
            colsJoin = colsJoin && std::abs((u.c.y + u.size.y / 2) - (d.c.y - d.size.y / 2)) < 1e-3f && std::abs(u.uv.w - d.uv.y) < 1e-7f;
        }
    check(std::abs(area - size.x * size.y) < 0.5f && rowsJoin && colsJoin, "...tiling the rect on screen and the image in the atlas, no gaps or overlaps");
    check(std::abs(q[0].size.x - 106) < 1e-3f && std::abs(q[0].size.y - 48) < 1e-3f && q[4].size.x > q[0].size.x, "...corners at their size, the middle stretched");
    const glm::vec4 whole = hudImageUV(kDropFrame, {0, 0, kDropFrame.w, kDropFrame.h});
    check(q[0].uv.x == whole.x && q[0].uv.y == whole.y && q[8].uv.z == whole.z && q[8].uv.w == whole.w, "...the whole image used, corner to corner");
    hudImageNineSlice(kDropFrame, c, {100, 60}, {200, 89}, {106, 48}, q);
    check(std::abs(q[0].size.x - 50) < 1e-3f && std::abs(q[0].size.y - 30) < 1e-3f, "...corners never bigger than half the rect");
}

void unownedCellsBlank() {
    // every cell of the sprite rows that no sprite owns (HudIcons.h) is empty in the atlas: no stray old art
    const std::string header = slurp(std::string(CS_SOURCE_DIR) + "/src/core/HudIcons.h");
    std::map<std::string, int> index, span, frames;
    const std::regex entry(R"((\w+) = (\d+),)"), spanRe(R"(case Icon::(\w+): return (\d+);)"), framesRe(R"(k(\w+)Frames = (\d+);)");
    const auto enumStart = header.find("enum class Icon"), spanStart = header.find("iconSpan"), rowStart = header.find("iconRowSpan");
    const std::string enumBody = header.substr(enumStart, header.find("};", enumStart) - enumStart);
    for (std::sregex_iterator it(enumBody.begin(), enumBody.end(), entry), e; it != e; ++it) index[(*it)[1]] = std::stoi((*it)[2]);
    const std::string spans = header.substr(spanStart, rowStart - spanStart);
    for (std::sregex_iterator it(spans.begin(), spans.end(), spanRe), e; it != e; ++it) span[(*it)[1]] = std::stoi((*it)[2]);
    for (std::sregex_iterator it(header.begin(), header.end(), framesRe), e; it != e; ++it) frames[(*it)[1]] = std::stoi((*it)[2]);
    const std::string rowsPart = header.substr(rowStart);
    std::set<std::string> tall;
    for (std::sregex_iterator it(rowsPart.begin(), rowsPart.end(), spanRe), e; it != e; ++it) tall.insert((*it)[1]);
    // the sprite rows: up to the first tall block (outfit tiles and big sprites live below, packed differently)
    int firstTall = kIconCols * kIconRows;
    for (const auto& [name, i] : index) if (tall.count(name)) firstTall = std::min(firstTall, i);
    const int spriteRows = firstTall / kIconCols;
    std::set<int> owned;
    for (const auto& [name, i] : index) {
        const int sp = span.count(name) ? span[name] : 1, fr = frames.count(name) ? frames[name] : 1;
        for (int k = 0; k < sp * fr; ++k) owned.insert(i + k);
    }
    HudImage atlas;
    atlas.buildIcons();
    const auto& A = atlas.pixels();
    const int cell = atlas.width() / kIconCols;
    int stray = 0, empties = 0;
    for (int c = 0; c < spriteRows * kIconCols; ++c) {
        if (owned.count(c)) continue;
        ++empties;
        bool any = false;
        for (int y = 0; y < cell && !any; ++y)
            for (int x = 0; x < cell && !any; ++x)
                any = A[(size_t((c / kIconCols) * cell + y) * atlas.width() + size_t((c % kIconCols) * cell + x)) * 4 + 3] != 0;
        stray += any;
    }
    check(!index.empty() && empties > 0 && stray == 0, "the atlas cells no sprite owns are empty (no leftover art)");
}

} // namespace

int main() {
    sizesAndPadding();
    uvMaths();
    atlasPixels();
    threeSlice();
    nineSlice();
    unownedCellsBlank();
    return finish("HudImg");
}
