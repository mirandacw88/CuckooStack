// HUD and overlays, rebuilt from the web build's DOM + CSS (cuckoo-stack.html, <style> and the overlay markup).
// Paint order follows DOM stacking: .scan, #pops, #flash, .hud, .meter, overlays, then .mute (z-index 5).
// Units are CSS px (= logical points). Colours are CSS sRGB values used as-is: the HUD is composited after tone
// mapping into a UNORM swapchain, exactly like the browser composites the DOM over the WebGL canvas.
#include "Game.h"
#include "Materials.h"

#include <cstdio>
#include <ctime>

namespace cs {

namespace {

glm::vec3 css(std::string_view hex) { return srgbColor(hex); }
// :root custom properties
const glm::vec3 kNeon = css("#29e7ff"), kHot = css("#ff2bd6"), kVolt = css("#f4ff5a"), kInk = css("#07060f"), kText = css("#eafaff");

TextShadow hard(float dx, glm::vec3 c, float a = 1.f) { return {{dx, 0.f}, 0.f, c, a}; }
TextShadow blur(float r, glm::vec3 c, float a) { return {{0.f, 0.f}, r, c, a}; }

// cubic-bezier(.2,.8,.3,1) is a strong ease-out; a cubic ease-out matches it within a pixel or two
float easeOut(float t) { t = clampf(t, 0.f, 1.f); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

// .overlay>* { animation: up .55s cubic-bezier(.2,.8,.3,1) both }  (opacity 0 -> 1, translateY 18px -> 0)
struct Up { float alpha, dy; };
Up up(float t, float delay) { const float e = easeOut((t - delay) / 0.55f); return {e, 18.f * (1.f - e)}; }

TextStyle style(FontId font, float size, glm::vec3 color, float letterSpacing = 0.f, bool upper = false, float lineHeight = 0.f) {
    TextStyle s;
    s.font = font; s.size = size; s.color = color; s.letterSpacing = letterSpacing; s.uppercase = upper; s.lineHeight = lineHeight;
    return s;
}

} // namespace

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
glm::vec2 Game::hudButton(std::string_view label, glm::vec2 c, glm::vec3 color, float alpha, bool glow) {
    const TextStyle s = style(FontId::BodyBold, 15, kText, 0.16f, true);
    const float w = text_->measure(toUpperAscii(label), s) + 52 + 4, h = text_->lineBox(s) + 24 + 4;
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
    return {w, h};
}

// ---------------------------------------------------------------- lives badge
// One heart sprite (assets/hud/lives_icons.png, frames = LivesFrame) and the count next to it.

namespace {
constexpr float kSheetFrames = 6.f; // frames in assets/hud/lives_icons.png (LivesFrame)
float easeOutBack(float t, float k = 1.9f) { t = clampf(t, 0.f, 1.f); const float c = k + 1; return 1 + c * std::pow(t - 1, 3.f) + k * std::pow(t - 1, 2.f); }
float seg(float t, float a, float b) { return clampf((t - a) / (b - a), 0.f, 1.f); } // 0..1 progress of t through [a, b]
// life-lost timeline (seconds)
constexpr float SWELL = 0.14f, CRACK_IN = 0.10f, SPLIT = 0.26f, SPLIT_LEN = 0.62f, ROLL_OUT = 0.18f, ROLL_IN = 0.32f,
                POP = 0.64f, POP_LEN = 0.30f, HOLD_END = 1.3f, FADE_END = 1.7f;
} // namespace

void Game::hudSprite(LivesFrame frame, glm::vec2 c, float size, float rot, glm::vec3 tint, float alpha) {
    if (!hudIcons_.built() || alpha <= 0.003f || size <= 0.01f) return;
    const float i = float(int(frame));
    Instance inst;
    inst.model = compose({c, 0.f}, {0.f, 0.f, rot}, {size, -size, 1.f}); // HUD space is Y-down
    inst.color = glm::vec4(tint, alpha);
    inst.emissive = glm::vec4(i / kSheetFrames, 0.f, (i + 1.f) / kSheetFrames, 1.f); // atlas UV rect
    list_.hud.push_back(inst);
    list_.hudKind.push_back(HudSprite);
}

float Game::livesBadgeWidth(float iconSize) const {
    TextStyle n = style(FontId::Display, iconSize * 0.8f, kText, 0.f, false, 1.f);
    return iconSize * 0.86f + 8.f + text_->measure(std::to_string(std::max(lives_, lifeFrom_)), n);
}

void Game::updateLivesAnim(float rdt) {
    if (gainAnimT_ >= 0) { gainAnimT_ += rdt; if (gainAnimT_ > 0.9f) gainAnimT_ = -1; }
    for (HudShard& h : shards_) {
        if (h.life <= 0) continue;
        h.life -= rdt;
        h.v.y += 900.f * rdt; // HUD points / s^2, Y down
        h.v *= 1.f - std::min(1.f, rdt * 1.2f);
        h.p += h.v * rdt;
        h.rot += h.spin * rdt;
    }
    if (lifeAnimT_ < 0) return;
    const float prev = lifeAnimT_;
    lifeAnimT_ += rdt;
    if (prev < SPLIT && lifeAnimT_ >= SPLIT && !lifeSplitDone_) { // the heart breaks: shards + sound + haptic
        lifeSplitDone_ = true;
        sfx(Sfx::LifeLost);
        buzz(25);
        const glm::vec3 cols[] = {kHot, css("#ff9cee"), kText, css("#ff3b5c")};
        for (size_t k = 0; k < shards_.size(); ++k) {
            HudShard& h = shards_[k];
            const float a = -kPi / 2 + rng_.range(-1.5f, 1.5f), sp = rng_.range(140.f, 340.f);
            h.p = badgeIconCenter_ + glm::vec2(rng_.range(-6.f, 6.f), rng_.range(-6.f, 6.f));
            h.v = {std::cos(a) * sp, std::sin(a) * sp};
            h.rot = rng_.range(0.f, 6.28f); h.spin = rng_.range(-14.f, 14.f);
            h.maxLife = h.life = rng_.range(0.45f, 0.8f);
            h.size = rng_.range(3.f, 7.5f);
            h.heart = k % 4 == 0;
            h.color = cols[k % 4];
        }
    }
    if (lifeAnimT_ > FADE_END) lifeAnimT_ = -1;
}

void Game::hudLivesBadge(glm::vec2 left, float S, float alpha) {
    if (alpha <= 0.003f) return;
    const float t = lifeAnimT_;
    const bool losing = t >= 0;
    const float iconW = S * 0.86f;
    glm::vec2 ic{left.x + iconW / 2, left.y};
    badgeIconCenter_ = ic;

    // anticipation + impact: swell, then shake while the crack spreads
    float scale = 1.f;
    if (losing) {
        scale += 0.35f * easeOutBack(seg(t, 0, SWELL), 1.2f) * (1.f - seg(t, SWELL, 0.5f));
        const float shake = (1.f - seg(t, CRACK_IN, SPLIT + 0.1f)) * (t >= CRACK_IN ? 1.f : 0.f);
        ic.x += std::sin(t * 75.f) * 4.5f * shake;
        ic.y += std::cos(t * 63.f) * 2.f * shake;
    }
    if (gainAnimT_ >= 0) scale += 0.45f * std::sin(seg(gainAnimT_, 0, 0.5f) * kPi) * (1.f - seg(gainAnimT_, 0.5f, 0.9f));

    // red shockwave glow behind the heart at the moment of impact
    if (losing && t < 0.7f) {
        const float g = seg(t, 0, 0.18f) * (1.f - seg(t, 0.18f, 0.7f));
        hudRect(ic, glm::vec2(S * (1.6f + 1.6f * seg(t, 0, 0.7f))), css("#ff2b4a"), 0.75f * g * alpha, Shape::RadialGlow);
    }
    if (gainAnimT_ >= 0 && gainAnimT_ < 0.6f)
        hudRect(ic, glm::vec2(S * (1.4f + 1.8f * seg(gainAnimT_, 0, 0.6f))), css("#2bff9a"), 0.6f * (1.f - seg(gainAnimT_, 0, 0.6f)) * alpha, Shape::RadialGlow);

    // dark rounded backing (like the mute button) so the badge reads over bright windows
    {
        const float bw = livesBadgeWidth(S) + 18.f, bh = S + 10.f;
        const glm::vec2 bc{left.x - 9.f + bw / 2, left.y};
        hudRect(bc, {bw, bh}, css("#0a0818"), 0.55f * alpha, Shape::PillOutline, 1.f);
        hudRect(bc, {bw, bh}, kNeon, 0.3f * alpha, Shape::PillOutline, 1.f / bh);
    }

    const glm::vec3 white(1.f), dim(0.55f);
    if (!losing) {
        hudSprite(lives_ > 0 ? LivesFrame::Full : LivesFrame::Empty, ic, S * scale, 0, white, alpha);
    } else if (t < SPLIT) {
        hudSprite(LivesFrame::Full, ic, S * scale, 0, white, alpha);
        hudSprite(LivesFrame::Crack, ic, S * scale, 0, white, alpha * seg(t, CRACK_IN, SPLIT - 0.02f));
    } else {
        // the two halves tumble apart under gravity and fade
        const float u = seg(t, SPLIT, SPLIT + SPLIT_LEN), fall = u * u;
        const float ha = alpha * (1.f - seg(u, 0.55f, 1.f));
        hudSprite(LivesFrame::LeftHalf, ic + glm::vec2(-S * 0.55f * u, S * 1.6f * fall), S * scale, -0.9f * u, white, ha);
        hudSprite(LivesFrame::RightHalf, ic + glm::vec2(S * 0.55f * u, S * 1.4f * fall), S * scale, 0.75f * u, white, ha);
        // a fresh heart pops back in (an empty glass one at 0 lives)
        const float p = seg(t, POP, POP + POP_LEN);
        if (p > 0) hudSprite(lifeTo_ > 0 ? LivesFrame::Full : LivesFrame::Empty, ic, S * easeOutBack(p), 0, lifeTo_ > 0 ? white : dim + 0.45f, alpha * std::min(1.f, p * 3.f));
    }
    // shards burst out of the break: tiny hearts and glints
    for (const HudShard& h : shards_) {
        if (h.life <= 0) continue;
        const float k = h.life / h.maxLife;
        if (h.heart) hudSprite(LivesFrame::Full, h.p, h.size * 3.2f, h.rot * 0.3f, white, alpha * k);
        else hudRect(h.p, glm::vec2(h.size, h.size * 0.45f), h.color, alpha * k, Shape::Solid, 0, h.rot);
    }

    // the count: the old number rolls down and fades red, the new one drops in with a bounce
    TextStyle n = style(FontId::Display, S * 0.8f, kText, 0.f, false, 1.f);
    n.shadows = {hard(-2, kHot), hard(2, kNeon), blur(14, kHot, 0.5f)};
    const float nx = left.x + iconW + 8.f, box = text_->lineBox(n);
    const glm::vec3 red = css("#ff3b5c");
    if (!losing) {
        TextStyle c = n;
        c.opacity = alpha;
        c.size *= 1.f + (scale - 1.f) * 0.6f;
        if (lives_ == 0) { c.color = red; c.shadows = {blur(12, red, 0.7f)}; } // offset shadows would block in the slashed 0
        hudText(std::to_string(lives_), c, nx, left.y - text_->lineBox(c) / 2, TextAlign::Left);
        return;
    }
    {
        const float e = seg(t, ROLL_OUT, ROLL_OUT + 0.3f);
        TextStyle o = n;
        o.color = glm::mix(kText, red, seg(t, 0.05f, ROLL_OUT));
        o.opacity = alpha * (1.f - e);
        o.shadows = {hard(-2, red), blur(12, red, 0.6f)};
        if (o.opacity > 0.01f) hudText(std::to_string(lifeFrom_), o, nx, left.y - box / 2 + 22.f * e * e, TextAlign::Left);
    }
    {
        const float e = seg(t, ROLL_IN, ROLL_IN + 0.32f);
        if (e > 0) {
            TextStyle c = n;
            c.opacity = alpha * std::min(1.f, e * 2.5f);
            c.color = lifeTo_ > 0 ? glm::mix(red, kText, seg(t, 0.7f, 1.05f)) : red;
            if (lifeTo_ == 0) c.shadows = {blur(12, red, 0.7f)};
            hudText(std::to_string(lifeTo_), c, nx, left.y - box / 2 - 22.f * (1.f - easeOutBack(e, 2.4f)), TextAlign::Left);
        }
    }
}

// Out of lives: modal offer over the game-over (or title) screen. The rewarded ad is the way back in.
void Game::hudOffer() {
    const float W = viewW_, H = viewH_;
    const float k = easeOut(offerT_ / 0.35f);
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.72f * k); // dim everything behind
    const float pw = std::min(W - 48.f, 340.f);

    // content, measured first so the panel hugs it with the same padding at the top and the bottom
    TextStyle tag = style(FontId::BodyBold, 13, kHot, 0.26f, true);
    tag.shadows = {blur(10, kHot, 0.7f)};
    tag.opacity = k;
    TextStyle head = style(FontId::Display, 30, kText, 0.f, false, 1.05f);
    head.shadows = {hard(-2, kHot), hard(2, kNeon)};
    head.opacity = k;
    TextStyle body = style(FontId::Body, 15, kText, 0.f, false, 1.4f);
    body.opacity = 0.85f * k;
    const std::vector<std::string> bodyLines =
        text_->wrap("Watch a short video to get " + std::to_string(lives::REWARD) + " more lives and keep playing.", body, pw - 48);
    constexpr float PAD = 28.f;              // panel padding, top and bottom
    constexpr float HEART = 88.f;            // sprite frame size; the heart itself fills ~72% of it
    const float heartBlock = HEART * 0.86f + 30.f;
    const float buttonH = text_->lineBox(style(FontId::BodyBold, 15, kText)) + 28.f; // matches hudButton()
    const float contentH = text_->lineBox(tag) + 10 + text_->lineBox(head) + 8 + text_->lineBox(body) * bodyLines.size() + 14 +
                           heartBlock + buttonH;
    const float ph = contentH + 2 * PAD;

    const glm::vec2 c{W / 2, H / 2 + 24.f * (1.f - k)};
    hudRect(c, {pw + 16, ph + 16}, kHot, 0.10f * k, Shape::PillOutline, 8.f / (ph + 16));
    hudRect(c, {pw, ph}, css("#0e0a22"), 0.97f * k, Shape::PillOutline, 1.f); // outline thicker than the shape = rounded fill
    hudRect(c, {pw, ph}, kHot, 0.9f * k, Shape::PillOutline, 1.5f / ph);
    float y = c.y - ph / 2 + PAD;

    hudText("Out of lives", tag, c.x, y);
    y += text_->lineBox(tag) + 10;
    hudText("+" + std::to_string(lives::REWARD) + " lives", head, c.x, y);
    y += text_->lineBox(head) + 8;
    for (const std::string& line : bodyLines) {
        hudText(line, body, c.x, y);
        y += text_->lineBox(body);
    }
    y += 14;
    {   // the reward: the lives heart, popping in with the panel and gently pulsing
        const float pop = easeOutBack(offerT_ / 0.45f);
        const float pulse = 1.f + 0.05f * std::sin(offerT_ * 4.2f);
        const glm::vec2 hc{c.x, y + HEART / 2};
        hudRect(hc, glm::vec2(HEART * 2.1f), kHot, (0.35f + 0.1f * std::sin(offerT_ * 4.2f)) * k, Shape::RadialGlow);
        hudSprite(LivesFrame::Full, hc, HEART * pop * pulse, 0, glm::vec3(1.f), k);
        y += heartBlock;
    }
    const RewardedState st = svc_.ads->rewardedState();
    std::string label;
    glm::vec3 color = kHot;
    float alpha = k;
    if (st == RewardedState::Ready) label = "Watch ad \u00b7 +" + std::to_string(lives::REWARD) + " lives";
    else if (offerPlayAnyway()) { label = "Play anyway"; color = kNeon; }
    else { label = "Loading ad\u2026"; alpha = 0.5f * k; }
    const glm::vec2 bc{c.x, y + buttonH / 2};
    const glm::vec2 bs = hudButton(label, bc, color, alpha, st == RewardedState::Ready);
    offerButton_ = {bc, bs * 0.5f + glm::vec2(6.f)};

    // close: a chunky cartoon button sitting on the panel's top-right corner. It pops in just after the panel,
    // then breathes gently. Closing returns to the game-over screen, which keeps a "Get 3 lives" button.
    {
        constexpr float CLOSE = 66.f; // sprite frame size; the visible button is ~44 pt across
        const glm::vec2 xc{c.x + pw / 2 - 16.f, c.y - ph / 2 + 16.f};
        const float pop = easeOutBack(seg(offerT_, 0.15f, 0.5f), 2.4f);
        const float breathe = 1.f + 0.03f * std::sin(offerT_ * 3.1f);
        hudSprite(LivesFrame::Close, xc, CLOSE * pop * breathe, 0.08f * std::sin(offerT_ * 2.3f), glm::vec3(1.f), k);
        offerClose_ = {xc, {30, 30}}; // comfortably above the 44 pt minimum touch target
    }
}

void Game::hudTitleOverlay() {
    const float W = viewW_, H = viewH_, t = titleT_;
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.78f, Shape::BottomFade);   // linear-gradient(to bottom, transparent 40%, ink .78)
    float bottom = H - safeBottom_ - 96;                                // padding-bottom: safe-area + 96px
    const float maxW = W - 32;                                          // 16px side padding

    hudPill(lives_ > 0 ? "Tap to jack in" : "Get " + std::to_string(lives::REWARD) + " lives", bottom, 1.f);
    bottom -= text_->lineBox(style(FontId::BodyBold, 15, kText)) + 28 + 8 + 14; // pill height + margin-top 8 + gap 14
    hudLivesBadge({W / 2 - livesBadgeWidth(30.f) / 2, bottom - 20}, 30.f, 1.f);
    bottom -= 40 + 10;

    // p.daily (4th child, delay .24s)
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
    // header pinned to the top (below the privacy / mute buttons) so it stays clear of the hen
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
}

// Space kept free below "Tap to reboot" for extra game-over buttons: one pill-height row (~48 pt) plus gaps.
constexpr float OVER_BUTTON_ROW = 72.f;

void Game::hudOverOverlay() {
    const float W = viewW_, H = viewH_, t = overT_;
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.78f, Shape::BottomFade);
    // the free button row sits where the overlay used to end; everything else moves up by its height
    overButtonRowBottom_ = H - safeBottom_ - 96;
    float bottom = overButtonRowBottom_ - OVER_BUTTON_ROW;
    const float maxW = W - 32;

    hudPill(lives_ > 0 ? "Tap to reboot" : "Get " + std::to_string(lives::REWARD) + " lives", bottom, 1.f);
    bottom -= text_->lineBox(style(FontId::BodyBold, 15, kText)) + 28 + 8 + 14;

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

    // lives during a run: top-left, opposite the mute button
    // lives during a run (top-left, opposite the mute button); after a death it stays just long enough to play the
    // life-lost animation, then fades out (no lives on the game-over screen)
    if (fonts_.built() && (state_ == State::Play || (state_ == State::Dead && lifeAnimT_ >= 0))) {
        const float fade = state_ == State::Dead ? 1.f - seg(lifeAnimT_, HOLD_END, FADE_END) : 1.f;
        hudLivesBadge({16.f, safeTop_ + 16 + 22}, 34.f, fade);
    }

    // overlays rise from the bottom third, where the thumb rests
    if (fonts_.built()) {
        if (state_ == State::Title) hudTitleOverlay();
        else if (state_ == State::Dead && overDelay_ <= 0) hudOverOverlay();
        if (offerOpen_) hudOffer(); // modal, above the overlay
    }

    // 'Privacy settings' (Google UMP requires an in-app way to change ad consent where it applies): title screen,
    // top-left, styled like the mute button
    if (privacyButton_ && state_ == State::Title && fonts_.built()) {
        TextStyle ps = style(FontId::BodyBold, 12, kNeon, 0.14f, true);
        const float w = text_->measure("PRIVACY SETTINGS", ps) + 24, cy = safeTop_ + 16 + 22;
        hudRect({16 + w / 2, cy}, {w, 32}, css("#0a0818"), 0.55f);
        hudRect({16 + w / 2, cy}, {w, 32}, kNeon, 0.45f, Shape::PillOutline, 1.f / 32.f);
        hudText("Privacy settings", ps, 16 + w / 2, cy - text_->lineBox(ps) / 2);
    }

    // .mute (z-index 5): 44x44, radius 8, rgba(10,8,24,.55) + 1px rgba(neon,.45), music-note glyph, slash when off
    {
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
