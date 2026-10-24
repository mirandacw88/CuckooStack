#include "Hazards.h"

#include "Json.h"

#include <algorithm>
#include <cstdio>

namespace cs {

namespace {

std::vector<const HazardType*>& all() {
    static std::vector<const HazardType*> types = {&kSawHazard, &kDiscoHazard, &kLaserHazard, &kLavaHazard, &kSpringHazard};
    return types;
}

std::string hex(uint32_t c) { char b[8]; std::snprintf(b, sizeof b, "#%06x", c & 0xffffff); return b; }

} // namespace

const std::vector<const HazardType*>& hazardTypes() { return all(); }

const HazardType* hazardType(const std::string& id) {
    for (const HazardType* t : all()) if (id == t->id) return t;
    return nullptr;
}

const HazardType* hazardSection(const std::string& section) {
    for (const HazardType* t : all()) if (section == t->section) return t;
    return nullptr;
}

void registerHazard(const HazardType* t) {
    if (std::find(all().begin(), all().end(), t) == all().end()) all().push_back(t);
}

void unregisterHazard(const HazardType* t) { all().erase(std::remove(all().begin(), all().end(), t), all().end()); }

const std::vector<WallKindInfo>& wallKinds() {
    static const std::vector<WallKindInfo> kinds = {
        {WallKind::Crate, "crate", "Crates", "1", "A wall of crates. A saw on its top is a floor saw.", 0x13243c},
        {WallKind::Hay, "hay", "Hay", "2", "A wall of hay bales. A saw on its top is a cube saw.", 0x4a3510},
        {WallKind::Mix, "mix", "Hay-topped", "3", "Crates with a hay bale on top. A saw on its top is a cube saw.", 0x2a2c26},
        {WallKind::Bumper, "bumper", "Bumper", "u", "A bumper wall: running into it at her body bounces her back the way she came; stacked over it, "
         "she climbs onto it. A gate or bumper before it bounces her back again: she stacks while she bounces to climb out.", 0xff2bd6, true},
        {WallKind::GateR, "gate>", "Gate →", "y", "A one-way gate: she runs through it heading right; heading left it bounces her, like a bumper. "
         "Never a floor (she drops through it).", 0x2bff9a, true},
        {WallKind::GateL, "gate<", "Gate ←", "", "A one-way gate: she runs through it heading left; heading right it bounces her, like a bumper. "
         "Never a floor.", 0x2bff9a, true},
    };
    return kinds;
}

const WallKindInfo* wallKindNamed(const std::string& id) {
    for (const WallKindInfo& k : wallKinds()) if (id == k.id) return &k;
    return nullptr;
}

const WallKindInfo& wallKindInfo(WallKind kind) {
    for (const WallKindInfo& k : wallKinds()) if (k.kind == kind) return k;
    return wallKinds().front();
}

std::string registryJson() {
    std::string o = "{\"hazards\":[";
    for (size_t i = 0; i < all().size(); ++i) {
        const HazardType& t = *all()[i];
        o += std::string(i ? "," : "") + "{\"id\":\"" + jsonEsc(t.id) + "\",\"section\":\"" + jsonEsc(t.section) + "\",\"label\":\"" + jsonEsc(t.label) +
             "\",\"plural\":\"" + jsonEsc(t.plural) + "\",\"key\":\"" + jsonEsc(t.key) + "\",\"tip\":\"" + jsonEsc(t.tip) + "\",\"icon\":\"" + jsonEsc(t.icon) +
             "\",\"glyph\":\"" + jsonEsc(t.glyph) + "\",\"color\":\"" + hex(t.color) + "\",\"anchor\":\"" + (t.anchor == Anchor::Free ? "free" : t.anchor == Anchor::Face ? "face" : "surface") +
             "\",\"perLine\":" + std::to_string(t.perLine) + ",\"spanParam\":" + std::to_string(t.spanParam) +
             ",\"generated\":" + (t.generated ? "true" : "false") + ",\"group\":\"" + jsonEsc(t.group) + "\",\"snapGap\":" + jsonNum(t.snapGap) + ",\"minGap\":" + jsonNum(t.minGap) +
             ",\"offGauntlets\":" + (t.offGauntlets ? "true" : "false") + ",\"params\":[";
        for (size_t k = 0; k < t.params.size(); ++k) {
            const ParamSpec& p = t.params[k];
            o += std::string(k ? "," : "") + "{\"key\":\"" + jsonEsc(p.key) + "\",\"label\":\"" + jsonEsc(p.label) + "\",\"unit\":\"" + jsonEsc(p.unit) +
                 "\",\"min\":" + jsonNum(p.min) + ",\"max\":" + jsonNum(p.max) + ",\"step\":" + jsonNum(p.step) + ",\"default\":" + jsonNum(p.def) + "}";
        }
        o += "]}";
    }
    o += "],\"kinds\":[";
    for (size_t i = 0; i < wallKinds().size(); ++i) {
        const WallKindInfo& k = wallKinds()[i];
        o += std::string(i ? "," : "") + "{\"id\":\"" + jsonEsc(k.id) + "\",\"label\":\"" + jsonEsc(k.label) + "\",\"key\":\"" + jsonEsc(k.key) +
             "\",\"tip\":\"" + jsonEsc(k.tip) + "\",\"color\":\"" + hex(k.color) + "\",\"bouncer\":" + (k.bouncer ? "true" : "false") + "}";
    }
    return o + "]}";
}

} // namespace cs
