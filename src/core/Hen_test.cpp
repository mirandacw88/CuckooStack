// The cyber-hen (Hen.cpp): the node hierarchy Game animates, the procedural and model bodies it draws, outfit skins
// reaching the materials, CPU skinning of the model by the animated nodes, and the glass shatter on a crash.
#include "TestUtil.h"
#include "core/Hen.h"

using namespace cs;
using namespace test;

namespace {

size_t count(const RenderList& l, Pass p) {
    size_t n = 0;
    for (const auto& v : l.buckets[size_t(p)]) n += v.size();
    return n;
}

void tree() {
    Hen h;
    bool ordered = true;
    for (size_t i = 0; i < h.nodes.size(); ++i) ordered = ordered && h.nodes[i].parent < int(i);
    check(ordered && h.nodes[size_t(h.root)].parent == -1, "nodes: one root, every parent before its children");
    bool handles = true;
    for (int n : {h.body, h.head, h.tail, h.wings[0], h.wings[1], h.legs[0], h.legs[1], h.knees[0], h.knees[1], h.feet[0], h.feet[1]})
        handles = handles && n > h.root && n < int(h.nodes.size());
    check(handles, "every animated handle points at a real node");
    check(h.nodes[size_t(h.head)].parent == h.body && h.nodes[size_t(h.wings[0])].parent == h.body && h.nodes[size_t(h.legs[0])].parent == h.root,
          "head and wings ride the body; legs hang from the root");
}

void procedural() {
    Hen h;
    h.useModel = false;
    RenderList l;
    h.emit(l);
    const size_t parts = count(l, Pass::Lit);
    check(parts > 20 && l.heroVerts.empty(), "procedural hen: built from primitive parts, nothing to skin");
    h.tipVisible = false;
    RenderList noTip;
    h.emit(noTip);
    check(count(noTip, Pass::Lit) == parts - 1, "tipVisible off: the antenna tip's light is hidden");
    h.skin.body = glm::vec3(0.1f, 0.2f, 0.3f);
    RenderList dyed;
    h.emit(dyed);
    bool found = false;
    for (const Instance& i : dyed.get(Pass::Lit, MeshId::Sphere)) found = found || glm::vec3(i.color) == h.skin.body;
    check(found, "the outfit's body colour reaches the feathers");
    h.skin.rainbow = true;
    auto accent = [&](float t) {
        h.time = t;
        RenderList r;
        h.emit(r);
        for (const Instance& i : r.get(Pass::Lit, MeshId::Cone)) if (glm::length(glm::vec3(i.emissive)) > 0.5f) return glm::vec3(i.color);
        return glm::vec3(-1);
    };
    check(accent(0.f) != accent(0.6f), "rainbow skin: the accent cycles with time");
    check(!h.canShatter(), "the procedural hen can't shatter (no pre-cut shards)");
    h.shatter({0, 0, 0}, {1, 0, 0}, 0, 1);
    check(!h.shattered(), "...so shatter does nothing");
}

void model() {
    Hen h;
    if (!h.useModel) { check(false, "the Classic model loads (embedded asset)"); return; }
    RenderList l;
    h.emit(l);
    const HenModel& m = h.model();
    check(l.heroVerts.size() == m.vertexCount && l.heroMeshes == m.regions && l.heroFirst == m.firstMesh, "model hen: every vertex posed for the renderer");
    check(l.get(Pass::Lit, MeshId::Cylinder).size() >= 2, "the robot legs stay procedural under the model");
    bool rest = true;
    size_t k = 0;
    for (int r = 0; r < m.regions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) rest = rest && glm::length(l.heroVerts[k++].pos - v.pos) < 1e-4f;
    check(rest, "at rest, skinning leaves the model as authored");
    // lift a wing: only vertices weighted to that wing move
    h.nodes[size_t(h.wings[1])].rot.x = 0.8f;
    RenderList w;
    h.emit(w);
    bool onlyWing = true, wingMoved = false;
    k = 0;
    for (int r = 0; r < m.regions; ++r)
        for (const HenModel::V& v : m.verts[size_t(r)]) {
            const bool moved = glm::length(w.heroVerts[k++].pos - v.pos) > 1e-4f;
            if (v.w[HenModel::BWing1] > 0.5f && moved) wingMoved = true;
            if (v.w[HenModel::BWing1] == 0.f && moved) onlyWing = false;
        }
    check(wingMoved && onlyWing, "a wing's rotation bends only the vertices weighted to it");
    bool unit = true;
    for (const Vertex& v : w.heroVerts) unit = unit && std::abs(glm::length(v.normal) - 1.f) < 1e-3f;
    check(unit, "posed normals stay unit length");
}

void shatter() {
    Hen h;
    if (!h.canShatter()) { check(false, "the Classic model can shatter"); return; }
    h.nodes[size_t(h.root)].pos = {5, 1, 0};
    h.shatter({5.5f, 1.5f, 0}, {8, 0, 0}, 0.f, 42);
    check(h.shattered(), "a crash breaks the model hen into shards");
    RenderList l;
    h.emit(l);
    check(count(l, Pass::LitTwoSided) == size_t(h.model().regions) && count(l, Pass::Lit) == 0, "shards: glassy two-sided pieces, no body");
    check(l.heroVerts.size() == h.model().vertexCount, "every vertex belongs to a shard");
    const glm::vec3 start = l.heroVerts[0].pos;
    float lowest = 1e9f;
    for (int i = 0; i < 60; ++i) {
        h.updateShatter(1.f / 60);
        RenderList f;
        h.emit(f);
        for (const Vertex& v : f.heroVerts) lowest = std::min(lowest, v.pos.y);
    }
    RenderList one;
    h.emit(one);
    check(glm::length(one.heroVerts[0].pos - start) > 0.5f, "the shards fly apart");
    check(lowest > -0.6f, "they bounce on the floor instead of falling through it");
    for (int i = 0; i < int(Hen::kShatterLife * 60) + 10; ++i) h.updateShatter(1.f / 60);
    RenderList gone;
    h.emit(gone);
    check(gone.heroVerts.empty() && count(gone, Pass::LitTwoSided) == 0, "after kShatterLife the shards have shrunk away");
    h.unshatter();
    RenderList back;
    h.emit(back);
    check(!h.shattered() && count(back, Pass::Lit) > 0, "unshatter: the hen is whole again (try-on after a crash)");

    Hen a, b;
    a.shatter({0, 1, 0}, {}, 0, 7); b.shatter({0, 1, 0}, {}, 0, 7);
    for (int i = 0; i < 20; ++i) { a.updateShatter(1.f / 60); b.updateShatter(1.f / 60); }
    RenderList la, lb;
    a.emit(la); b.emit(lb);
    check(la.heroVerts.size() == lb.heroVerts.size() && la.heroVerts[100].pos == lb.heroVerts[100].pos, "the same crash seed breaks the same way");
}

// burned up (lava): she chars black, her embers in her edges (her rim), not a glow all over her (a textured hen glows
// by its emissive wherever its texels are warm: all over)
void burn() {
    Hen h;
    if (!h.canShatter()) { check(false, "the Classic model can burn"); return; }
    h.nodes[size_t(h.root)].pos = {5, 1, 0};
    h.burn({8, 0, 0}, 1.f, 3);
    check(h.shattered() && h.breakup() == Hen::Breakup::Char, "burned: she breaks up charring");
    for (int i = 0; i < 60; ++i) h.updateShatter(1.f / 60);
    RenderList l;
    h.emit(l);
    float brightest = 0, emissive = 0, rim = 0;
    for (const auto& bucket : l.buckets[size_t(Pass::LitTwoSided)])
        for (const Instance& i : bucket) {
            brightest = std::max({brightest, i.color.r, i.color.g, i.color.b});
            emissive = std::max(emissive, glm::length(glm::vec3(i.emissive)));
            rim = std::max(rim, i.rim.w);
        }
    check(brightest < 0.15f, "...a second on: charred black");
    check(emissive < 0.3f && rim > 1.f, "...smouldering: her embers in her edges, not a glow all over her");
}

} // namespace

int main() {
    tree();
    procedural();
    model();
    shatter();
    burn();
    return finish("Hen");
}
