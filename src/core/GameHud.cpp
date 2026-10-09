// HUD and overlays, rebuilt from the web build's DOM + CSS (cuckoo-stack.html, <style> and the overlay markup).
// Paint order follows DOM stacking: .scan, #pops, #flash, .hud, .meter, overlays, then .mute (z-index 5).
// Units are CSS px (= logical points). Colours are CSS sRGB values used as-is: the HUD is composited after tone
// mapping into a UNORM swapchain, exactly like the browser composites the DOM over the WebGL canvas.
#include "Game.h"
#include "HudStyle.h"
#include "Materials.h"

#include <cstdio>
#include <ctime>

namespace cs {

using namespace hud;

std::string Game::dayLabel() const {
    // d.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' })  ->  "Tue, Oct 6"
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return std::string(kDays[t->tm_wday]) + ", " + kMonths[t->tm_mon] + " " + std::to_string(t->tm_mday);
}

void Game::hudRect(glm::vec2 c, glm::vec2 size, glm::vec3 color, float alpha, Shape shape, float param, float rot) {
    // negative Y scale: HUD space is Y-down, so this keeps uv.y = 1 at the top of the quad on screen
    Instance i = makeUnlit(compose({c, 0}, {0, 0, rot}, {size.x, -size.y, 1}), color, alpha, shape, param);
    i.params.x = size.x; i.params.y = size.y;
    list_.hud.push_back(i);
    list_.hudKind.push_back(HudShape);
}

void Game::hudText(std::string_view text, const TextStyle& s, float x, float top, TextAlign align) {
    if (!fonts_.built()) return;
    text_->drawLine(list_.hud, text, s, x, top, align, true);
    list_.hudKind.resize(list_.hud.size(), HudText);
}

// .overlay .pill: padding 12px 26px, 2px solid var(--hot), radius 6px, 15px/700, letter-spacing .16em, uppercase,
// box-shadow glow pulsing over 1.6s. `bottom` is the pill's bottom edge; returns via the caller's layout maths.
void Game::hudPill(std::string_view label, float bottom, float alpha) {
    const TextStyle s = style(FontId::BodyBold, 15, kText, 0.16f, true);
    const float w = text_->measure(toUpperAscii(label), s) + 52 + 4, h = text_->lineBox(s) + 24 + 4;
    const glm::vec2 c{viewW_ / 2, bottom - h / 2};
    const float pulse = 0.5f - 0.5f * std::cos(time_ / 1.6f * 2 * kPi); // keyframes pulse { 50% { stronger glow } }
    hudRect(c, {w + 18, h + 18}, kHot, alpha * (0.08f + 0.08f * pulse), Shape::PillOutline, 8.f / (h + 18));
    hudRect(c, {w + 8, h + 8}, kHot, alpha * (0.22f + 0.14f * pulse), Shape::PillOutline, 4.f / (h + 8));
    hudRect(c, {w, h}, kHot, alpha, Shape::PillOutline, 2.f / h);
    TextStyle t = s;
    t.opacity = alpha;
    hudText(label, t, c.x, c.y - text_->lineBox(s) / 2);
}

// A pill button centred on `center` (same look as .pill); returns its size for hit-testing.
glm::vec2 Game::hudButton(std::string_view label, glm::vec2 c, glm::vec3 color, float alpha, bool glow, std::function<void()> fn, float scale) {
    const TextStyle s = style(FontId::BodyBold, 15 * scale, kText, 0.16f, true);
    const float w = text_->measure(toUpperAscii(label), s) + (52 + 4) * scale, h = text_->lineBox(s) + (24 + 4) * scale;
    if (glow) {
        const float pulse = 0.5f - 0.5f * std::cos(time_ / 1.6f * 2 * kPi);
        hudRect(c, {w + 18, h + 18}, color, alpha * (0.08f + 0.08f * pulse), Shape::PillOutline, 8.f / (h + 18));
        hudRect(c, {w + 8, h + 8}, color, alpha * (0.22f + 0.14f * pulse), Shape::PillOutline, 4.f / (h + 8));
    }
    hudRect(c, {w, h}, css("#0a0818"), 0.85f * alpha, Shape::PillOutline, 1.f);
    hudRect(c, {w, h}, color, alpha, Shape::PillOutline, 2.f / h);
    TextStyle t = s;
    t.opacity = alpha;
    hudText(label, t, c.x, c.y - text_->lineBox(s) / 2);
    if (fn && alpha > 0.3f) { nextHitLabel_ = std::string(label); uiHit(c, {w + 12, std::max(h + 8, 44.f)}, std::move(fn)); }
    return {w, h};
}

// ---------------------------------------------------------------- sprites (assets/hud/hud_icons.png, HudIcons.h)

// `size` is the height; wide sprites (iconSpan, e.g. the coin piles) are that many times wider
void Game::hudSprite(Icon icon, glm::vec2 c, float size, float rot, glm::vec3 tint, float alpha, int frame) {
    if (!hudIcons_.built() || alpha <= 0.003f || size <= 0.01f) return;
    const int i = int(icon) + frame * iconSpan(icon), span = iconSpan(icon), rows = iconRowSpan(icon);
    const float col = float(i % kIconCols), row = float(i / kIconCols);
    hudAtlasQuad({col / kIconCols, row / kIconRows, (col + span) / kIconCols, (row + rows) / kIconRows}, c, {size * span, size * rows}, rot, tint, alpha);
}

void Game::hudAtlasQuad(glm::vec4 uv, glm::vec2 c, glm::vec2 size, float rot, glm::vec3 tint, float alpha) {
    if (!hudIcons_.built() || alpha <= 0.003f || size.x <= 0.01f || size.y <= 0.01f) return;
    Instance inst;
    inst.model = compose({c, 0.f}, {0.f, 0.f, rot}, {size.x, -size.y, 1.f}); // HUD space is Y-down
    inst.color = glm::vec4(tint, alpha);
    inst.emissive = uv; // atlas UV rect
    list_.hud.push_back(inst);
    list_.hudKind.push_back(HudSprite);
}

// Frosted glass behind a HUD panel: the composite pass blurs the scene inside this rounded rect (one per frame).
void Game::hudFrost(glm::vec2 c, glm::vec2 size, float radius, float amount) {
    list_.frame.frostRect = {c, size};
    list_.frame.frostRadius = radius;
    list_.frame.frostAmount = std::clamp(amount, 0.f, 1.f);
}

// A frame cell (FrameTech, FrameGlass, ...) stretched to any size: the corners keep `corner` points, the edges and
// centre stretch. `slice` is the corner's share of the cell height (0.25 = 48 px of 192; 0.5 for round-ended pills
// and the two-cell sheet sprites like BtnBlue / FrameFree, drawn with corner = height / 2 so only the middle widens).
void Game::hudNineSlice(Icon frame, glm::vec2 c, glm::vec2 size, float corner, glm::vec3 tint, float alpha, float slice) {
    const int i = int(frame), span = iconSpan(frame);
    const float cw = 1.f / kIconCols, ch = 1.f / kIconRows;
    const float u0 = (i % kIconCols) * cw, v0 = (i / kIconCols) * ch;
    const float us[4] = {u0, u0 + cw * slice, u0 + cw * (span - slice), u0 + cw * span};
    const float vs[4] = {v0, v0 + ch * slice, v0 + ch * (1 - slice), v0 + ch};
    corner = std::min({corner, size.x / 2, size.y / 2});
    const float xs[4] = {c.x - size.x / 2, c.x - size.x / 2 + corner, c.x + size.x / 2 - corner, c.x + size.x / 2};
    const float ys[4] = {c.y - size.y / 2, c.y - size.y / 2 + corner, c.y + size.y / 2 - corner, c.y + size.y / 2};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            const float w = xs[x + 1] - xs[x], h = ys[y + 1] - ys[y];
            if (w <= 0.01f || h <= 0.01f) continue;
            hudAtlasQuad({us[x], vs[y], us[x + 1], vs[y + 1]}, {(xs[x] + xs[x + 1]) / 2, (ys[y] + ys[y + 1]) / 2}, {w, h}, 0, tint, alpha);
        }
}

// The coin: an 8-frame spin. spin < 0 turns it slowly with time (a lazy wobble that rests face-on most of the time).
void Game::hudCoin(glm::vec2 c, float size, float alpha, float spin) {
    float a = spin;
    if (a < 0) {
        const float t = std::fmod(time_ * 0.6f + c.x * 0.013f, 3.f); // a quick turn every 3 s, staggered by position
        a = t < 0.7f ? t / 0.7f : 0.f;
    }
    const int frame = int(std::fmod(a, 1.f) * 16.f) % 16;             // 16 steps over a full turn; frames repeat after 180 degrees
    hudSprite(Icon::Coin, c, size, 0, glm::vec3(1.f), alpha, frame % kCoinFrames);
}

float Game::coinAmountWidth(int amount, float size, bool plus) const {
    TextStyle n = style(FontId::Display, size * 0.78f, kText, 0.f, false, 1.f);
    return size * 0.92f + 5.f + text_->measure((plus ? "+" : "") + grouped(amount), n);
}

// coin + amount; `p` is the left edge (or centre / right edge per align) at the vertical centre. Returns the size.
glm::vec2 Game::hudCoinAmount(int amount, glm::vec2 p, float size, float alpha, TextAlign align, bool plus) {
    const float w = coinAmountWidth(amount, size, plus);
    float left = align == TextAlign::Left ? p.x : align == TextAlign::Center ? p.x - w / 2 : p.x - w;
    hudCoin({left + size * 0.46f, p.y}, size, alpha, size >= 24.f ? -1.f : 0.f); // small inline coins stay face-on
    TextStyle n = style(FontId::Display, size * 0.78f, kGold, 0.f, false, 1.f);
    n.shadows = {hard(-1.5f, kHot, 0.9f), blur(10, kGold, 0.45f)};
    n.opacity = alpha;
    hudText((plus ? "+" : "") + grouped(amount), n, left + size * 0.92f + 5.f, p.y - text_->lineBox(n) / 2, TextAlign::Left);
    return {w, size};
}

void Game::hudTitleOverlay() {
    const float W = viewW_, H = viewH_, t = titleT_;
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.78f, Shape::BottomFade);   // linear-gradient(to bottom, transparent 40%, ink .78)
    float bottom = H - safeBottom_ - 10;
    const float maxW = W - 32;                                          // 16px side padding

    hudTitleMenu(bottom); // menu row, "Tap to jack in", boost chips (GameUi.cpp); moves `bottom` above them

    // p.daily (4th child, delay .24s); dropped on short screens, where the hen needs the room
    if (H < 720) return hudTitleHeader();
    TextStyle daily = style(FontId::BodyBold, 13, kNeon, 0.12f, true);
    daily.opacity = 0.95f;
    const std::string note = day_.attempts
        ? "Today's best " + std::to_string(day_.best) + " · " + std::to_string(day_.attempts) + (day_.attempts == 1 ? " attempt" : " attempts") +
              " · new course at midnight"
        : "Same course every attempt today. A new one drops at midnight.";
    {
        const auto lines = text_->wrap(toUpperAscii(note), daily, std::min(maxW, 34 * text_->ch(daily)));
        const Up a = up(t, 0.24f);
        daily.opacity *= a.alpha;
        const float h = text_->lineBox(daily) * lines.size();
        float y = bottom - h + a.dy;
        for (const auto& l : lines) { hudText(l, daily, W / 2, y); y += text_->lineBox(daily); }
        bottom -= h + 14;
    }
    hudTitleHeader();
}

void Game::hudTitleHeader() {
    const float W = viewW_, t = titleT_;
    const float maxW = W - 32;
    float top = safeTop_ + 16 + 44 + 28;
    // .tag (1st child, no delay): "Daily Run · Tue, Oct 6"
    {
        TextStyle tag = style(FontId::BodyBold, 13, kNeon, 0.26f, true);
        tag.shadows = {blur(10, kNeon, 0.7f)};
        const Up a = up(t, 0.f);
        tag.opacity = a.alpha;
        hudText("Daily Run · " + dayLabel(), tag, W / 2, top + a.dy);
        top += text_->lineBox(tag) + 10;
    }
    // h1 (2nd child): glitchIn .7s steps(1) .08s, then glitch every 5s from 1.2s
    {
        TextStyle h1 = style(FontId::Display, clampf(W * 0.125f, 40, 72), kText, 0.02f, false, 0.95f);
        h1.shadows = {hard(-3, kHot), hard(3, kNeon), blur(34, kHot, 0.45f)};
        const auto lines = text_->wrap("Cuckoo Stack", h1, maxW, true); // text-wrap: balance
        float dx = 0.f, alpha = 1.f;
        const float g = (t - 0.08f) / 0.7f;
        if (g < 0.1f) alpha = 0.f;
        else if (g < 0.25f) dx = -10;
        else if (g < 0.40f) dx = 8;
        else if (g < 0.55f) dx = -4;
        else if (g < 0.70f) dx = 2;
        if (t > 1.2f) {
            const float c = std::fmod(t - 1.2f, 5.f) / 5.f;
            dx = c >= 0.92f && c < 0.94f ? -5.f : c >= 0.94f && c < 0.96f ? 4.f : c >= 0.96f ? -2.f : 0.f;
        }
        h1.opacity = alpha;
        for (const auto& l : lines) { hudText(l, h1, W / 2 + dx, top); top += text_->lineBox(h1); }
    }
    // live event banner (Remote Config event_coin_mult)
    if (tune_.eventCoinMult > 1.01f) {
        const Up a = up(t, 0.4f);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%gx coins event", std::round(tune_.eventCoinMult * 10.f) / 10.f);
        TextStyle e = style(FontId::BodyBold, 13, css("#1a0420"), 0.18f, true);
        e.opacity = a.alpha;
        const float pulse = 1.f + 0.05f * std::sin(time_ * 5.f);
        const float w = text_->measure(buf, e) + 56, y = top + 12 + 16 + a.dy;
        hudRect({W / 2, y}, glm::vec2(w + 14, 46) * pulse, kVolt, 0.25f * a.alpha, Shape::PillOutline, 7.f / 46);
        hudRect({W / 2, y}, glm::vec2(w, 32) * pulse, kVolt, a.alpha, Shape::PillOutline, 1.f);
        hudCoin({W / 2 - w / 2 + 20, y}, 26, a.alpha);
        hudText(buf, e, W / 2 + 12, y - text_->lineBox(e) / 2);
    }
}


// The game-over button row (shop / locker / missions / share) sits under "Tap to reboot".
constexpr float OVER_BUTTON_ROW = 84.f;

void Game::hudOverOverlay() {
    const float W = viewW_, H = viewH_, t = overT_;
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.84f, Shape::BottomFade);
    overButtonRowBottom_ = H - safeBottom_ - 10;
    float bottom = overButtonRowBottom_ - OVER_BUTTON_ROW;
    const float maxW = W - 32;
    hudOverMenu(overButtonRowBottom_);

    hudPill("Tap to reboot", bottom, std::min(1.f, overT_ / 0.4f));
    bottom -= text_->lineBox(style(FontId::BodyBold, 15, kText)) + 28 + 8 + 16;
    hudOverRewards(bottom); // coins from the run (+ x2 offer), level bar (GameUi.cpp)

    // p.daily#overNote (3rd child, delay .16s)
    {
        TextStyle daily = style(FontId::BodyBold, 13, kNeon, 0.12f, true);
        const Up a = up(t, 0.16f);
        daily.opacity = 0.95f * a.alpha;
        const auto lines = text_->wrap(toUpperAscii(overNote_), daily, std::min(maxW, 34 * text_->ch(daily)));
        const float h = text_->lineBox(daily) * lines.size();
        float y = bottom - h + a.dy;
        for (const auto& l : lines) { hudText(l, daily, W / 2, y); y += text_->lineBox(daily); }
        bottom -= h + 14;
    }
    // .stats grid (2nd child, delay .08s): 2 columns, gap 14px 40px; each cell = <b> + <span>, gap 4px
    {
        const Up a = up(t, 0.08f);
        const float k = std::min(1.f, t / 0.7f), e = 1.f - std::pow(1.f - k, 3.f); // countUp(…, 700)
        TextStyle b = style(FontId::Display, 34, kText, 0.f, false, 1.f);
        b.shadows = {hard(-2, kHot), hard(2, kNeon)};
        TextStyle rb = style(FontId::Display, 26, kText, 0.f, false, 1.f);
        rb.shadows = {hard(-1, kHot), hard(1, kNeon)};
        TextStyle span = style(FontId::BodyBold, 12, kNeon, 0.18f, true);
        TextStyle rspan = span;
        rspan.opacity = 0.75f;
        for (TextStyle* s : {&b, &rb, &span}) s->opacity *= a.alpha;
        rspan.opacity *= a.alpha;
        const std::string cells[2][2][2] = {
            {{std::to_string(static_cast<int>(std::round(finalScore_ * e))), "Score"},
             {std::to_string(static_cast<int>(std::round(finalDist_ * e))) + " m", "Distance"}},
            {{std::to_string(day_.best), "Today's best"}, {std::to_string(best_), "All-time best"}}};
        float colW[2] = {0, 0};
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                colW[c] = std::max({colW[c], text_->measure(cells[r][c][0], r ? rb : b), text_->measure(toUpperAscii(cells[r][c][1]), span)});
        const float total = colW[0] + 40 + colW[1];
        const float rowH[2] = {text_->lineBox(b) + 4 + text_->lineBox(span), text_->lineBox(rb) + 4 + text_->lineBox(span)};
        const float gridH = rowH[0] + 14 + rowH[1];
        float y = bottom - gridH + a.dy;
        for (int r = 0; r < 2; ++r) {
            float x = W / 2 - total / 2;
            for (int c = 0; c < 2; ++c) {
                const float cx = x + colW[c] / 2;
                hudText(cells[r][c][0], r ? rb : b, cx, y);
                hudText(cells[r][c][1], r ? rspan : span, cx, y + text_->lineBox(r ? rb : b) + 4);
                x += colW[c] + 40;
            }
            y += rowH[r] + 14;
        }
        bottom -= gridH + 14;
    }
    // .tag#overTag (1st child)
    {
        TextStyle tag = style(FontId::BodyBold, 13, kNeon, 0.26f, true);
        tag.shadows = {blur(10, kNeon, 0.7f)};
        const Up a = up(t, 0.f);
        tag.opacity = a.alpha;
        hudText(overTag_, tag, W / 2, bottom - text_->lineBox(tag) + a.dy);
    }
}

void Game::worldGateLabel() {
    // today's-best gate: a 512x160 canvas ("TODAY'S BEST" 700 40px #29e7ff at y=62, "<n> m" 900 64px white at y=128)
    // on a 2.4 x 0.75 m plane, MeshBasicMaterial colour glow('#ffffff', 1.05)
    if (!gateVisible_ || gateOpacity_ <= 0.f || !fonts_.built()) return;
    const float labelY = std::max(camY_ + 2.6f, level_.heightAt(gateX_) * Uf + 2.f);
    const float k = 2.4f / 512.f;
    const glm::mat4 plane = compose({float(gateX_), labelY, -0.8f}, glm::vec3(k, k, 1.f)); // local units = canvas px, Y up
    TextStyle a = style(FontId::BodyBold, 40, hexColor("#29e7ff") * 1.05f);
    a.opacity = gateOpacity_;
    text_->drawBaseline(list_.worldText, "TODAY'S BEST", a, 0.f, 80.f - 62.f, TextAlign::Center, false, plane);
    TextStyle b = style(FontId::Display, 56, glm::vec3(1.05f));
    b.opacity = gateOpacity_;
    text_->drawBaseline(list_.worldText, std::to_string(static_cast<int>(std::floor(gateX_))) + " m", b, 0.f, 80.f - 128.f, TextAlign::Center, false, plane);
}

void Game::worldMarkerLabel(double x, float opacity, std::string_view top, std::string_view bottom, glm::vec3 topColor) {
    if (opacity <= 0.f || !fonts_.built()) return;
    const float labelY = std::max(camY_ + 3.4f, level_.heightAt(x) * Uf + 2.8f); // above the best-distance label
    const float k = 2.4f / 512.f;
    const glm::mat4 plane = compose({float(x), labelY, -0.8f}, glm::vec3(k, k, 1.f));
    TextStyle a = style(FontId::BodyBold, 40, topColor * 1.1f);
    a.opacity = opacity;
    text_->drawBaseline(list_.worldText, top, a, 0.f, 80.f - 62.f, TextAlign::Center, false, plane);
    TextStyle b = style(FontId::Display, 56, glm::vec3(1.05f));
    b.opacity = opacity;
    text_->drawBaseline(list_.worldText, bottom, b, 0.f, 80.f - 128.f, TextAlign::Center, false, plane);
}

void Game::buildHud() {
    const float W = viewW_, H = viewH_;

    // .scan: 1px lines every 3px over the whole page
    hudRect({W / 2, H / 2}, {W, H}, kText, 0.035f, Shape::Scanlines);

    // #pops: .pop / .pop.sector / .pop.perfect, keyframes rise
    if (fonts_.built())
        for (const Popup& p : popups_) {
            glm::vec2 px;
            if (!camera_.project(p.world, px)) continue;
            TextStyle s;
            if (p.kind == PopKind::Sector) { s = style(FontId::Display, 28, kNeon); s.shadows = {blur(18, kNeon, 0.9f), hard(-2, kHot)}; }
            else if (p.kind == PopKind::Perfect) { s = style(FontId::Display, 24, kText); s.shadows = {hard(-2, kHot), hard(2, kNeon), blur(18, kHot, 0.9f)}; }
            else if (p.kind == PopKind::Surge) { s = style(FontId::Display, 56, kText); s.shadows = {hard(-3, kHot), hard(3, kNeon), blur(30, kHot, 0.95f)}; }
            else if (p.kind == PopKind::Smashed) { s = style(FontId::Display, 26, kVolt); s.shadows = {hard(-2, kHot), blur(16, kVolt, 0.8f)}; }
            else if (p.kind == PopKind::ChainLost) { s = style(FontId::BodyBold, 15, css("#9aa3b8"), 0.16f); s.shadows = {blur(6, css("#000000"), 0.6f)}; }
            else { s = style(FontId::Display, 22, kVolt); s.shadows = {blur(12, kVolt, 0.8f), hard(-1, kHot)}; }
            const float box = text_->lineBox(s);
            const float k = p.age / p.dur;
            float op, yPct, sc;
            if (k < 0.15f) { const float e = easeOut(k / 0.15f); op = e; yPct = -50 - 10 * e; sc = 0.5f + 0.7f * e; }
            else if (k < 0.30f) { const float e = easeOut((k - 0.15f) / 0.15f); op = 1; yPct = -60 - 10 * e; sc = 1.2f - 0.2f * e; }
            else { const float e = easeOut((k - 0.30f) / 0.70f); op = 1 - e; yPct = -70 - 190 * e; sc = 1; }
            const float centerY = px.y + (yPct + 50.f) / 100.f * box; // translate(-50%, y%) around the anchor point
            s.size *= sc;
            s.opacity = op;
            hudText(p.text, s, px.x, centerY - text_->lineBox(s) / 2);
        }

    // #flash
    if (flashT_ < 0.5f) hudRect({W / 2, H / 2}, {W, H}, flashColor_, flashAlpha_ * (1 - flashT_ / 0.5f));

    // .hud: #score + #corn, visible from the first run until the next title (it stays up behind the game-over overlay)
    if (state_ != State::Title && fonts_.built()) {
        const float top = safeTop_ + 18;
        TextStyle score = style(FontId::Display, clampf(W * 0.14f, 46, 70), kText, 0.f, false, 1.f);
        score.shadows = {hard(-2, kHot), hard(2, kNeon), blur(26, kNeon, 0.55f)};
        const float box = text_->lineBox(score);
        // #score.bump: scale 1.35 -> 1 over .32s, cubic-bezier(.2,1.6,.4,1) (overshoots slightly)
        const float bt = 1.f - scoreBump_ / 0.32f;
        const float bump = scoreBump_ > 0 ? 1.f + 0.35f * (1.f - easeOut(bt)) - 0.04f * std::sin(bt * kPi) : 1.f;
        TextStyle scaled = score;
        scaled.size *= bump;
        hudText(std::to_string(score_), scaled, W / 2, top + box / 2 - text_->lineBox(scaled) / 2);
        TextStyle corn = style(FontId::BodyBold, 13, kVolt, 0.18f, true);
        corn.shadows = {blur(10, kVolt, 0.6f)};
        if (surging_) {
            // "SURGE · 3.2s" + a thin timer bar draining from full to empty (pink -> yellow -> cyan)
            char buf[32];
            std::snprintf(buf, sizeof buf, "Surge \u00b7 %.1fs", std::max(0.f, surgeT_));
            TextStyle sg = style(FontId::BodyBold, 13, kHot, 0.18f, true);
            sg.shadows = {blur(10, kHot, 0.8f)};
            hudText(buf, sg, W / 2, top + box + 4);
            constexpr float BW = 150.f, BH = 4.f;
            constexpr int SLICES = 30;
            const float barY = top + box + 4 + text_->lineBox(sg) + 6 + BH / 2;
            hudRect({W / 2, barY}, {BW + 4, BH + 4}, css("#0a0818"), 0.7f);
            const float frac = clampf(surgeT_ / surge::TIME, 0.f, 1.f);
            const int filled = static_cast<int>(std::ceil(frac * SLICES));
            for (int i = 0; i < filled; ++i) {
                const float u = (i + 0.5f) / SLICES;
                const glm::vec3 c = u < 0.5f ? glm::mix(kHot, kVolt, u * 2.f) : glm::mix(kVolt, kNeon, (u - 0.5f) * 2.f);
                const float w = i == filled - 1 ? (frac * SLICES - i) * BW / SLICES : BW / SLICES;
                hudRect({W / 2 - BW / 2 + i * BW / SLICES + w / 2, barY}, {w + 0.5f, BH}, c, 1.f);
            }
        } else {
            hudText("Sector " + std::to_string(sector_) + " \u00b7 disco chain " + std::to_string(chain_) + "/" + std::to_string(surge::NEED),
                    corn, W / 2, top + box + 4);
        }
    }

    // .meter: egg supply (hidden once the game-over overlay appears)
    if (state_ == State::Play || (state_ == State::Dead && overDelay_ > 0)) {
        constexpr float CELL = 16, GAP = 6, MW = MAXE * CELL + (MAXE - 1) * GAP + 10, MH = 30;
        const float shake = meterShake_ > 0 ? std::sin(meterShake_ * 60) * 6 : 0.f;
        const glm::vec2 c{W / 2 + shake, H - safeBottom_ - 22 - (MH + 10) / 2};
        hudRect(c, {MW + 24 + 18, MH + 10 + 18}, kNeon, 0.06f, Shape::PillOutline, 0.45f); // box-shadow 0 0 18px rgba(neon,.18)
        hudRect(c, {MW + 24, MH + 10}, css("#0a0818"), 0.78f, Shape::Solid);
        hudRect(c, {MW + 24, MH + 10}, meterShake_ > 0 ? kHot : kNeon, meterShake_ > 0 ? 1.f : 0.35f, Shape::PillOutline, 1.f / (MH + 10));
        const int full = std::min(MAXE, static_cast<int>(std::floor(meter_)));
        const float part = std::round((meter_ - std::floor(meter_)) * 20) / 20;
        for (int i = 0; i < MAXE; ++i) {
            const float fill = i < full ? 1.f : i == full ? part : 0.f;
            const float tt = popT_[i] / 0.35f, sc = 1 + 0.45f * tt * tt;
            hudRect({c.x - MW / 2 + 5 + i * (CELL + GAP) + CELL / 2, c.y}, glm::vec2(CELL, 21) * sc, i < full ? kNeon : kNeon * 0.75f, 1, Shape::EggCell, fill);
        }
    }

    hits_.clear();
    // overlays rise from the bottom third, where the thumb rests
    if (fonts_.built()) {
        // under the frosted-glass shop only the blurred city should show: the title / game-over overlay still runs
        // (it places the menu row the shop redraws) but its sprites and buttons are dropped
        const size_t hudMark = list_.hud.size(), hitMark = hits_.size();
        if (state_ == State::Title && screen_ != Screen::AgeGate && screen_ != Screen::Locker) hudTitleOverlay();
        else if (state_ == State::Dead && overDelay_ <= 0) hudOverOverlay();
        if (screen_ == Screen::Shop) {
            list_.hud.erase(list_.hud.begin() + hudMark, list_.hud.end());
            list_.hudKind.erase(list_.hudKind.begin() + hudMark, list_.hudKind.end());
            hits_.erase(hits_.begin() + hitMark, hits_.end());
        }
        // run boost badge (top-left, under the score) so the player sees what's active
        if (state_ == State::Play && runBoost_ != Boost::None && run_.seconds < 6.f) {
            const float a = 1.f - seg(run_.seconds, 4.5f, 6.f);
            const glm::vec2 c{16 + 20, safeTop_ + 16 + 22};
            hudSprite(runBoost_ == Boost::Surge ? Icon::Rocket : Icon::EggBolt, c, 40 * (1.f + 0.08f * std::sin(time_ * 6)), 0, glm::vec3(1.f), a);
        }
        if (state_ != State::Play && screen_ != Screen::AgeGate) hudTopBar();
        if (screen_ != Screen::None) hudScreen();
        hudToasts(); // above panels, so news like "Beat Sam: 412 m" is never dimmed
        hudCoinFx();
    }

    // .mute (z-index 5): 44x44, radius 8, rgba(10,8,24,.55) + 1px rgba(neon,.45), music-note glyph, slash when off
    if (screen_ == Screen::None) {
        const glm::vec2 c{W - 16 - 22, safeTop_ + 16 + 22};
        hudRect(c, {44, 44}, css("#0a0818"), 0.55f);
        hudRect(c, {44, 44}, kNeon, 0.45f, Shape::PillOutline, 1.f / 44.f);
        const float a = muted_ ? 0.6f : 1.f;
        hudRect({c.x - 4.5f, c.y + 6.5f}, {6.5f, 5.5f}, kNeon, a, Shape::SoftDisc);
        hudRect({c.x + 5.5f, c.y + 4.5f}, {6.5f, 5.5f}, kNeon, a, Shape::SoftDisc);
        hudRect({c.x - 2.2f, c.y - 1.5f}, {2.2f, 14.5f}, kNeon, a);
        hudRect({c.x + 7.8f, c.y - 3.5f}, {2.2f, 14.5f}, kNeon, a);
        hudRect({c.x + 2.8f, c.y - 9.5f}, {12.f, 2.4f}, kNeon, a, Shape::Solid, 0, -0.22f);
        if (muted_) hudRect(c, {2.4f, 25}, kNeon, a, Shape::Solid, 0, kPi / 4);
    }
}

} // namespace cs
