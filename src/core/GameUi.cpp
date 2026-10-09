// Meta-game UI: top bar (level, streak, coins), the title / game-over menus, every modal screen (age gate, Continue,
// shop, locker, missions, daily drop, settings, level-up chest, streak save, parental gate, offers), and the
// feedback layer (flying coins, the counter's count-up, toasts, sparks).
//
// Immediate mode: everything is drawn from state every frame, and tappable things register a hit rect (uiHit) that
// pressAt() checks topmost-first. Entrance animations run off screenT_ (seconds since the screen opened).
#include "Game.h"
#include "HudStyle.h"
#include "Links.h"

#include <cstdio>

namespace cs {

using namespace hud;

namespace {
constexpr float kPad = 22.f;      // panel padding
constexpr float kTopBarY = 38.f;  // top bar centre below the safe area (16 margin + 22 half height)
const char* iconName(Icon i) {
    switch (i) {
    case Icon::Shop: return "Shop";
    case Icon::Locker: return "Locker";
    case Icon::Missions: return "Missions";
    case Icon::Gift: return "Daily drop";
    case Icon::Settings: return "Settings";
    case Icon::Trophy: return "Leaders";
    case Icon::Share: return "Share";
    default: return "";
    }
}
// the artist's Locker tile for an outfit (name and portrait baked in); Icon::Count-free sentinel: Close = none
Icon outfitTile(const std::string& id) {
    static const std::pair<const char*, Icon> tiles[] = {
        {"hen_classic", Icon::OutfitClassic}, {"hen_midnight", Icon::OutfitMidnight}, {"hen_vapor", Icon::OutfitVapor},
        {"hen_toxic", Icon::OutfitToxic}, {"hen_ice", Icon::OutfitIce}, {"hen_lava", Icon::OutfitLava},
        {"hen_gold", Icon::OutfitGold}, {"hen_chrome", Icon::OutfitChrome}, {"hen_tiger", Icon::OutfitTiger},
        {"hen_holo", Icon::OutfitHolo}, {"hen_sakura", Icon::OutfitSakura}, {"hen_glitch", Icon::OutfitGlitch}};
    for (const auto& [name, icon] : tiles) if (id == name) return icon;
    return Icon::Close;
}
std::string mmss(int64_t s) {
    char b[16];
    if (s >= 3600) std::snprintf(b, sizeof b, "%dh %02dm", int(s / 3600), int(s % 3600 / 60));
    else std::snprintf(b, sizeof b, "%d:%02d", int(s / 60), int(s % 60));
    return b;
}
} // namespace

// ================================================================ widgets

// accent colour of a menu button (mockup: replay pink, shop magenta, locker cyan, missions violet)
glm::vec3 Game::iconTint(Icon icon) const {
    switch (icon) {
    case Icon::Replay: case Icon::Share: return css("#ff5ac8");
    case Icon::Shop: return css("#e05cff");
    case Icon::Locker: return kNeon;
    case Icon::Missions: return css("#a66bff");
    case Icon::Gift: return kGreen;
    case Icon::Trophy: return kGold;
    default: return kNeon;
    }
}

// the artist's framed button for a menu icon (Icon::MenuFrame + the icon for the ones the sheet has no art for)
static Icon menuArt(Icon icon) {
    switch (icon) {
    case Icon::Replay: return Icon::MenuReplay;
    case Icon::Shop: return Icon::MenuShop;
    case Icon::Locker: return Icon::MenuLocker;
    case Icon::Missions: return Icon::MenuMissions;
    default: return Icon::MenuFrame;
    }
}

void Game::hudIconButton(Icon icon, glm::vec2 c, float size, float alpha, std::function<void()> fn, int badge, bool pulse) {
    const float box = size + 22.f;
    const glm::vec3 tint = iconTint(icon);
    const float breathe = pulse ? 1.f + 0.04f * std::sin(time_ * 5.f) : 1.f;
    if (pulse) hudRect(c, glm::vec2(box * 2.f), tint, (0.32f + 0.12f * std::sin(time_ * 5.f)) * alpha, Shape::RadialGlow);
    const Icon art = menuArt(icon);
    // the sheet art fills ~85% of its cell; scale so the frame itself is `box` across
    hudSprite(art, c, box * 1.16f * breathe * (pulse ? 1.06f : 1.f), 0, glm::vec3(pulse ? 1.f : 0.86f), alpha);
    if (art == Icon::MenuFrame) hudSprite(icon, c, size * 1.15f * breathe, 0, glm::vec3(1.f), alpha);
    if (badge != 0) {
        const glm::vec2 b{c.x + box * 0.42f, c.y - box * 0.42f};
        if (badge < 0) {
            hudRect(b, {13, 13}, kHot, alpha, Shape::SoftDisc);
            hudRect(b, {9, 9}, css("#ffffff"), alpha, Shape::SoftDisc);
        } else {
            TextStyle n = style(FontId::BodyBold, 11, css("#ffffff"));
            n.opacity = alpha;
            const float w = std::max(18.f, text_->measure(std::to_string(badge), n) + 10);
            hudRect(b, {w, 18}, kHot, alpha, Shape::PillOutline, 1.f);
            hudText(std::to_string(badge), n, b.x, b.y - text_->lineBox(n) / 2);
        }
    }
    if (fn && alpha > 0.3f) { if (nextHitLabel_.empty()) nextHitLabel_ = iconName(icon); uiHit(c, {box + 8, box + 8}, std::move(fn)); }
}

// Menu-row navigation: from one panel straight to another (no stack of panels behind)
void Game::switchScreen(Screen s) {
    if (screen_ == s) return;
    if (screen_ == Screen::Locker) { lockerPreview_.clear(); applyCosmetics(); }
    screenStack_.clear();
    screen_ = Screen::None;
    openScreen(s);
}

glm::vec2 Game::hudTile(glm::vec2 c, glm::vec2 size, glm::vec3 accent, float alpha, bool selected, std::function<void()> fn) {
    if (selected) hudRect(c, size + glm::vec2(14.f), accent, 0.22f * alpha, Shape::PillOutline, 7.f / (size.y + 14));
    hudRect(c, size, css("#140d30"), 0.95f * alpha, Shape::PillOutline, 1.f);
    hudRect(c, size, accent, (selected ? 0.95f : 0.35f) * alpha, Shape::PillOutline, (selected ? 2.2f : 1.2f) / size.y);
    if (fn && alpha > 0.3f) uiHit(c, size, std::move(fn));
    return size;
}

// [icon] LABEL [coin cost]  inside one pill. icon: Icon::Count-like sentinel = none (pass Icon::Close + noIcon=true).
glm::vec2 Game::hudActionButton(std::string_view label, const Icon* icon, int coinCost, glm::vec2 c, glm::vec3 color, float alpha, bool glow,
                                std::function<void()> fn, float scale) {
    const TextStyle s = style(FontId::BodyBold, 14 * scale, kText, 0.14f, true);
    const float iconW = icon ? 26.f * scale + 8 : 0.f;
    const float costW = coinCost > 0 ? coinAmountWidth(coinCost, 20 * scale) + 10 : 0.f;
    const float textW = label.empty() ? 0.f : text_->measure(label, s);
    const float w = iconW + textW + costW + 40 * scale, h = text_->lineBox(s) + 26 * scale;
    if (glow) {
        const float pulse = 0.5f - 0.5f * std::cos(time_ / 1.6f * 2 * kPi);
        hudRect(c, {w + 18, h + 18}, color, alpha * (0.08f + 0.08f * pulse), Shape::PillOutline, 8.f / (h + 18));
        hudRect(c, {w + 8, h + 8}, color, alpha * (0.22f + 0.14f * pulse), Shape::PillOutline, 4.f / (h + 8));
    }
    hudRect(c, {w, h}, kDeep, 0.88f * alpha, Shape::PillOutline, 1.f);
    hudRect(c, {w, h}, color, alpha, Shape::PillOutline, 2.f / h);
    float x = c.x - w / 2 + 20 * scale;
    if (icon) { hudSprite(*icon, {x + 13 * scale, c.y}, 34 * scale, 0, glm::vec3(1.f), alpha); x += iconW; }
    if (textW > 0) {
        TextStyle t = s;
        t.opacity = alpha;
        hudText(label, t, x, c.y - text_->lineBox(s) / 2, TextAlign::Left);
        x += textW + 10;
    }
    if (costW > 0) hudCoinAmount(coinCost, {x, c.y}, 20 * scale, alpha);
    if (fn && alpha > 0.3f) { if (nextHitLabel_.empty()) nextHitLabel_ = std::string(label); uiHit(c, {w + 10, std::max(h + 8, 46.f)}, std::move(fn)); }
    return {w, h};
}

void Game::hudTextButton(std::string_view label, glm::vec2 c, float alpha, std::function<void()> fn) {
    TextStyle s = style(FontId::BodyBold, 13, kMuted, 0.16f, true);
    s.opacity = alpha;
    const float w = text_->measure(label, s);
    hudText(label, s, c.x, c.y - text_->lineBox(s) / 2);
    hudRect({c.x, c.y + text_->lineBox(s) / 2 - 1}, {w, 1.2f}, kMuted, 0.5f * alpha);
    if (fn && alpha > 0.3f) { nextHitLabel_ = std::string(label); uiHit(c, {w + 24, 40}, std::move(fn)); }
}

// Modal panel: dimmed backdrop, rounded neon card, tag + title, optional cartoon close button. The card rises and
// fades in over 0.32 s with a slight overshoot; returns where its content starts.
Game::Panel Game::hudPanel(std::string_view tagText, std::string_view title, float width, float contentH, glm::vec3 accent, bool closable) {
    const float W = viewW_, H = viewH_;
    const float k = easeOut(screenT_ / 0.32f), pop = easeOutBack(screenT_ / 0.4f, 1.4f);
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.74f * k);
    TextStyle tag = style(FontId::BodyBold, 13, accent, 0.26f, true);
    tag.shadows = {blur(10, accent, 0.7f)};
    tag.opacity = k;
    TextStyle head = style(FontId::Display, 28, kText, 0.f, false, 1.05f);
    head.shadows = {hard(-2, kHot), hard(2, kNeon)};
    head.opacity = k;
    const auto lines = text_->wrap(title, head, width - 2 * kPad - 24, true);
    const float headerH = (tagText.empty() ? 0.f : text_->lineBox(tag) + 8) + text_->lineBox(head) * lines.size() + 14;
    const float ph = std::min(kPad + headerH + contentH + kPad, H - safeTop_ - safeBottom_ - 16);
    float cy = H / 2 + 30.f * (1.f - pop);
    cy = std::clamp(cy, safeTop_ + 8 + ph / 2, H - safeBottom_ - 8 - ph / 2);
    const glm::vec2 c{W / 2, cy};
    hudRect(c, {width + 16, ph + 16}, accent, 0.12f * k, Shape::PillOutline, 8.f / (ph + 16));
    hudRect(c, {width, ph}, kPanel, 0.97f * k, Shape::PillOutline, 1.f); // outline thicker than the shape = rounded fill
    hudRect(c, {width - 6, ph - 6}, kText, 0.025f * k, Shape::Scanlines);
    hudRect(c, {width, ph}, accent, 0.9f * k, Shape::PillOutline, 1.5f / ph);
    float y = c.y - ph / 2 + kPad;
    if (!tagText.empty()) { hudText(tagText, tag, c.x, y); y += text_->lineBox(tag) + 8; }
    for (const auto& l : lines) { hudText(l, head, c.x, y); y += text_->lineBox(head); }
    y += 14;
    if (closable) {
        constexpr float CLOSE = 62.f;
        const glm::vec2 xc{c.x + width / 2 - 14.f, c.y - ph / 2 + 14.f};
        const float p = easeOutBack(seg(screenT_, 0.12f, 0.45f), 2.4f);
        hudSprite(Icon::Close, xc, CLOSE * p * (1.f + 0.03f * std::sin(screenT_ * 3.1f)), 0.08f * std::sin(screenT_ * 2.3f), glm::vec3(1.f), k);
        nextHitLabel_ = "Close";
        uiHit(xc, {50, 50}, [this] { closeScreen(); });
    }
    return {c, {width, ph}, k, y};
}

// ================================================================ top bar, coins

void Game::hudCoinPill(bool interactive) {
    const float W = viewW_, y = safeTop_ + kTopBarY;
    const int amount = std::max(0, int(std::lround(coinShown_)));
    TextStyle n = style(FontId::BodyBold, 22, css("#ffe08a"), 0.02f);
    n.shadows = {blur(10, kGold, 0.45f)};
    TextStyle plus = style(FontId::BodyBold, 30, kNeon);
    plus.shadows = {blur(10, kNeon, 0.8f)};
    const float S = 28.f;
    const float nw = text_->measure(grouped(amount), n), pw = 12 + S + 10 + nw + 14 + 18 + 14;
    const float right = W - 16 - 44 - 10;
    const glm::vec2 c{right - pw / 2, y};
    const float bump = 1.f + 0.1f * coinPulse_;
    const glm::vec2 size = glm::vec2(pw, 44) * bump;
    if (coinPulse_ > 0.01f) hudRect(c, size * 1.4f, kGold, 0.35f * coinPulse_, Shape::RadialGlow);
    hudNineSlice(Icon::FramePill, c, size, size.y / 2, glm::vec3(0.85f, 0.92f, 1.f), 1.f, 0.5f);
    float x = c.x - size.x / 2 + 12;
    hudCoin({x + S / 2, y}, S * bump, 1.f);
    coinCounterPos_ = {x + S / 2, y};
    x += S + 10;
    TextStyle nb = n;
    nb.size *= bump;
    hudText(grouped(amount), nb, x, y - text_->lineBox(nb) / 2, TextAlign::Left);
    hudText("+", plus, c.x + size.x / 2 - 22, y - text_->lineBox(plus) / 2 - 1);
    if (interactive) { nextHitLabel_ = "Coins"; uiHit(c, {pw + 8, 50}, [this] { openScreen(Screen::Shop); }); }
}

void Game::hudTopBar() {
    const float y = safeTop_ + kTopBarY;
    nextHitLabel_ = "Settings";
    hudIconButton(Icon::Settings, {38, y}, 26, 1.f, [this] { openScreen(Screen::Settings); });
    // level: star + number, a thin XP bar under it
    {
        const glm::vec2 c{94, y - 3};
        hudSprite(Icon::Star, c, 50, 0, glm::vec3(1.f), 1.f);
        TextStyle n = style(FontId::Display, prog_.level() >= 10 ? 13 : 15, css("#3b1d00"), 0.f, false, 1.f);
        hudText(std::to_string(prog_.level()), n, c.x, c.y + 2 - text_->lineBox(n) / 2);
        const float bw = 34, f = prog_.fraction();
        hudRect({c.x, y + 19}, {bw + 4, 6}, kDeep, 0.85f, Shape::PillOutline, 1.f);
        if (f > 0.01f) hudRect({c.x - bw / 2 + bw * f / 2, y + 19}, {bw * f, 3}, kVolt, 1.f);
    }
    // daily streak: flickering flame + count (13+: also shown, without pressure, to children)
    const int st = dayStreak_.count();
    if (st > 0) {
        const glm::vec2 c{134, y};
        const int frame = int(time_ * 9.f) % kFlameFrames;
        hudSprite(Icon::Flame, c, 40, 0, glm::vec3(1.f), dayStreak_.playedToday() ? 1.f : 0.55f, frame);
        TextStyle n = style(FontId::Display, 16, kText, 0.f, false, 1.f);
        n.shadows = {hard(-1, kHot), blur(10, css("#ff6a2b"), 0.6f)};
        hudText(std::to_string(st), n, c.x + 15, c.y - text_->lineBox(n) / 2, TextAlign::Left);
    }
    hudCoinPill(true);
}

// ================================================================ title + game-over menus

void Game::hudTitleNav(float rowY) {
    const float W = viewW_, t = titleT_;
    int done = 0;
    for (const Mission& m : missions_.list()) done += m.done;
    struct Item { Icon icon; Screen screen; int badge; bool pulse; };
    std::vector<Item> items = {
        {Icon::Shop, Screen::Shop, adPolicy_.canOffer(Placement::FreeCoins) && !profile_.child() ? -1 : 0, false},
        {Icon::Locker, Screen::Locker, 0, false},
        {Icon::Missions, Screen::Missions, done < 3 ? 3 - done : 0, missions_.allDone() && !missions_.bonusClaimed()},
        {Icon::Gift, Screen::DailyDrop, 0, drop_.claimable()},
    };
    if (!profile_.child() && svc_.leaderboards->available()) items.push_back({Icon::Trophy, Screen::None, 0, false}); // the platform's board
    const int n = int(items.size());
    const float gap = std::min(84.f, (W - 24) / float(n));
    for (int i = 0; i < n; ++i) {
        const Up a = up(t, 0.3f + i * 0.05f);
        const glm::vec2 c{W / 2 + (i - (n - 1) * 0.5f) * gap, rowY + a.dy};
        const Screen sc = items[size_t(i)].screen;
        const bool selected = sc != Screen::None && screen_ == sc;
        hudIconButton(items[size_t(i)].icon, c, n > 4 ? 25 : 28, a.alpha, [this, sc] {
            if (sc == Screen::None) { track("leaderboard_open", {}); svc_.leaderboards->show(); }
            else if (screen_ != Screen::None) switchScreen(sc);
            else openScreen(sc);
        }, items[size_t(i)].badge, items[size_t(i)].pulse || selected);
        TextStyle l = style(FontId::BodyBold, n > 4 ? 9.5f : 10.5f, iconTint(items[size_t(i)].icon), 0.12f, true);
        l.opacity = 0.95f * a.alpha;
        hudText(iconName(items[size_t(i)].icon), l, c.x, c.y + 30);
    }
}

void Game::hudTitleMenu(float& bottom) {
    const float W = viewW_, t = titleT_;
    // menu row: shop, locker, missions, daily drop (+ leaderboards)
    titleNavY_ = bottom - 40 - 8;
    hudTitleNav(titleNavY_);
    bottom -= 84 + 35; // 35 pt clear between "Tap to jack in" and the menu row
    hudPill("Tap to jack in", bottom, 1.f);
    bottom -= text_->lineBox(style(FontId::BodyBold, 15, kText)) + 28 + 14;
    // pre-run boosts: free with an ad (a few a day) or for coins
    {
        const float cw = std::min(168.f, (W - 44) / 2), ch = 50;
        const float y = bottom - ch / 2;
        struct B { Boost b; Icon icon; const char* name; };
        const B boosts[] = {{Boost::Surge, Icon::Rocket, "Surge start"}, {Boost::Overclock, Icon::EggBolt, "Overclock"}};
        const bool adOk = rewardReady(Placement::Boost);
        for (int i = 0; i < 2; ++i) {
            const Up a = up(t, 0.2f + i * 0.06f);
            const glm::vec2 c{W / 2 + (i ? 1.f : -1.f) * (cw / 2 + 6), y + a.dy};
            const bool armed = boost_ == boosts[i].b;
            const bool other = boost_ != Boost::None && !armed;
            const float al = a.alpha * (other ? 0.4f : 1.f);
            const Boost which = boosts[i].b;
            nextHitLabel_ = boosts[i].name;
            hudTile(c, {cw, ch}, armed ? kGreen : kViolet, al, armed, other || armed ? std::function<void()>{} : std::function<void()>([this, which, adOk] {
                auto arm = [this, which] {
                    boost_ = which;
                    sfx(Sfx::Perfect, 4); buzz(12);
                    hudBurst({viewW_ / 2, viewH_ * 0.6f}, 16, {kGreen, kNeon, kVolt}, 260.f);
                    track("boost_armed", {{"boost", which == Boost::Surge ? "surge" : "overclock"}});
                };
                if (adOk) requestReward(Placement::Boost, arm);
                else if (wallet_.spend(tune_.boostCost, which == Boost::Surge ? "boost_surge" : "boost_overclock")) arm();
                else { showMessage("Not enough coins"); openScreen(Screen::Shop); }
            }));
            hudSprite(boosts[i].icon, {c.x - cw / 2 + 24, c.y}, 40 * (armed ? 1.f + 0.06f * std::sin(time_ * 6) : 1.f), 0, glm::vec3(1.f), al);
            TextStyle n = style(FontId::BodyBold, 12, kText, 0.12f, true);
            n.opacity = al;
            hudText(boosts[i].name, n, c.x - cw / 2 + 48, c.y - 15, TextAlign::Left);
            if (armed) {
                TextStyle on = style(FontId::BodyBold, 11, kGreen, 0.14f, true);
                on.opacity = al;
                hudText("Ready", on, c.x - cw / 2 + 48, c.y + 1, TextAlign::Left);
                hudSprite(Icon::Check, {c.x + cw / 2 - 18, c.y}, 30, 0, glm::vec3(1.f), al);
            } else if (adOk) {
                hudSprite(Icon::AdBadge, {c.x - cw / 2 + 60, c.y + 9}, 26, 0, glm::vec3(1.f), al);
                TextStyle f = style(FontId::BodyBold, 11, kVolt, 0.14f, true);
                f.opacity = al;
                hudText("Free", f, c.x - cw / 2 + 76, c.y + 1, TextAlign::Left);
            } else {
                hudCoinAmount(tune_.boostCost, {c.x - cw / 2 + 48, c.y + 9}, 16, al);
            }
        }
        bottom -= ch + 16;
    }
}

void Game::hudOverMenu(float bottom) {
    const float W = viewW_;
    const float a = std::min(1.f, overT_ / 0.5f);
    const float rowY = bottom - 48;
    // (Share Replay is the big button above the results: hudShareReplayButton)
    const Icon icons[] = {Icon::Shop, Icon::Locker, Icon::Missions};
    const Screen screens[] = {Screen::Shop, Screen::Locker, Screen::Missions};
    int done = 0;
    for (const Mission& m : missions_.list()) done += m.done;
    for (int i = 0; i < 3; ++i) {
        const glm::vec2 c{W / 2 + (i - 1.f) * 96.f, rowY};
        const Screen sc = screens[i];
        const int badge = i == 2 && done < 3 ? 3 - done : 0;
        const bool selected = screen_ == sc;
        hudIconButton(icons[i], c, 28, a, [this, sc] { if (screen_ != Screen::None) switchScreen(sc); else openScreen(sc); }, badge,
                      selected || (i == 2 && missions_.allDone() && !missions_.bonusClaimed()));
        TextStyle l = style(FontId::BodyBold, 10.5f, iconTint(icons[i]), 0.14f, true);
        l.opacity = 0.95f * a;
        hudText(iconName(icons[i]), l, c.x, c.y + 30);
    }
}

// Share Replay (13+): the main viral call to action, a big neon button centred above the results. The replay when
// there is one, otherwise the result + link. Pops in after the results, breathes, and a light sweep crosses it.
void Game::hudShareReplayButton(glm::vec2 c) {
    if (profile_.child()) return;
    const ReplayState rs = svc_.replay->state();
    const bool saving = rs == ReplayState::Exporting;
    const float W = viewW_;
    const float pop = easeOutBack(seg(overT_, 0.35f, 0.75f), 1.6f);
    if (pop <= 0.01f) return;
    const float k = std::min(1.f, pop) * (saving ? 0.6f : 1.f);
    const float breathe = 1.f + 0.025f * std::sin(time_ * 3.2f);
    const glm::vec2 size = glm::vec2(std::min(W - 64.f, 300.f), 64.f) * pop * breathe;
    const glm::vec3 pink = css("#ff2bd6"), violet = css("#9a5bff");
    // glow halo, pulsing
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 3.2f);
    hudRect(c, size * glm::vec2(1.35f, 2.2f), pink, (0.18f + 0.1f * pulse) * k, Shape::RadialGlow);
    hudRect(c, size + glm::vec2(14.f), pink, (0.18f + 0.12f * pulse) * k, Shape::PillOutline, 7.f / (size.y + 14));
    // body: deep fill, a violet-to-pink wash, bright neon rim
    hudRect(c, size, css("#1a0730"), 0.96f * k, Shape::PillOutline, 1.f);
    hudRect({c.x - size.x * 0.22f, c.y}, {size.x * 0.9f, size.y * 1.6f}, violet, 0.45f * k, Shape::RadialGlow);
    hudRect({c.x + size.x * 0.25f, c.y}, {size.x * 0.9f, size.y * 1.6f}, pink, 0.4f * k, Shape::RadialGlow);
    hudRect(c, size, pink, k, Shape::PillOutline, 2.6f / size.y);
    hudRect({c.x, c.y - size.y * 0.28f}, {size.x * 0.8f, 2.f}, glm::vec3(1.f), 0.18f * k); // top gloss line
    // light sweep every ~2.5 s
    const float g = std::fmod(time_ * 0.4f, 1.f) * 1.6f - 0.3f;
    if (g > -0.2f && g < 1.2f)
        hudRect({c.x - size.x / 2 + size.x * clampf(g, 0.f, 1.f), c.y}, {size.y * 0.7f, size.y * 0.92f}, glm::vec3(1.f),
                0.16f * k * std::sin(clampf(g, 0.f, 1.f) * kPi), Shape::Streak, 0, 0.35f);
    // icon + label, centred together
    TextStyle t = style(FontId::Display, 26.f * pop, kText, 0.02f, false, 1.f);
    t.shadows = {hard(-1.5f, kNeon, 0.9f), hard(1.5f, kHot, 0.9f), blur(14, pink, 0.6f)};
    t.opacity = k;
    const std::string label = saving ? "Saving\u2026" : "Share Replay";
    const float iconS = size.y * 0.9f, gap = 10.f;
    const float tw = text_->measure(label, t), total = iconS * 0.75f + gap + tw;
    const float x0 = c.x - total / 2;
    hudSprite(Icon::Replay, {x0 + iconS * 0.375f, c.y}, iconS * (1.f + 0.06f * pulse), 0, glm::vec3(1.f), k);
    hudText(label, t, x0 + iconS * 0.75f + gap, c.y - text_->lineBox(t) / 2, TextAlign::Left);
    if (!saving && overT_ > 0.6f) {
        nextHitLabel_ = "Share replay";
        uiHit(c, size + glm::vec2(10.f), [this] { shareReplay(); });
    }
}

// Results: the run's coins (with the x2 rewarded offer) and the level bar filling up.
void Game::hudOverRewards(float& bottom) {
    const float W = viewW_, t = overT_;
    // level bar
    {
        const Up a = up(t, 0.22f);
        const float bw = std::min(220.f, W - 140), y = bottom - 10 + a.dy;
        const float fill = clampf(seg(t, 0.4f, 1.3f), 0.f, 1.f);
        const float f = levelFrom_ != prog_.level() ? (fill < 0.5f ? glm::mix(xpFrom_, 1.f, fill * 2) : glm::mix(0.f, prog_.fraction(), fill * 2 - 1))
                                                    : glm::mix(xpFrom_, prog_.fraction(), fill);
        const int shownLevel = levelFrom_ != prog_.level() && fill < 0.5f ? levelFrom_ : prog_.level();
        TextStyle lv = style(FontId::BodyBold, 12, kVolt, 0.16f, true);
        lv.opacity = a.alpha;
        hudText("Lv " + std::to_string(shownLevel), lv, W / 2 - bw / 2 - 10, y - text_->lineBox(lv) / 2, TextAlign::Right);
        hudRect({W / 2, y}, {bw + 6, 12}, kDeep, 0.9f * a.alpha, Shape::PillOutline, 1.f);
        hudRect({W / 2, y}, {bw + 6, 12}, kVolt, 0.4f * a.alpha, Shape::PillOutline, 1.f / 12);
        if (f > 0.005f) {
            hudRect({W / 2 - bw / 2 + bw * f / 2, y}, {bw * f, 6}, kVolt, a.alpha);
            hudRect({W / 2 - bw / 2 + bw * f, y}, {16, 16}, kVolt, 0.5f * a.alpha * (fill < 1.f ? 1.f : 0.3f), Shape::RadialGlow);
        }
        TextStyle xp = style(FontId::BodyBold, 12, kText, 0.12f, true);
        xp.opacity = 0.8f * a.alpha;
        hudText("+" + std::to_string(runXp_) + " xp", xp, W / 2 + bw / 2 + 10, y - text_->lineBox(xp) / 2, TextAlign::Left);
        bottom -= 26;
    }
    // coins
    {
        const Up a = up(t, 0.12f);
        const float y = bottom - 22 + a.dy;
        overCoinsY_ = y;
        const int shown = int(std::round(runCoins_ * (coinsDoubled_ ? 2.f : 1.f) * easeOut(t / 0.6f)));
        const bool offer = !coinsDoubled_ && runCoins_ >= tune_.doubleCoinsMin && adPolicy_.canOffer(Placement::DoubleCoins);
        const float amountW = coinAmountWidth(shown, 32, true);
        const float total = amountW + (offer ? 16 + 116 : 0);
        hudCoinAmount(shown, {W / 2 - total / 2, y}, 32, a.alpha, TextAlign::Left, true);
        if (offer) {
            const Icon ad = Icon::AdBadge;
            nextHitLabel_ = "x2";
            hudActionButton("x2", &ad, 0, {W / 2 - total / 2 + amountW + 16 + 58, y}, kVolt, a.alpha, true, [this] {
                requestReward(Placement::DoubleCoins, [this] {
                    wallet_.earn(runCoins_, "double_coins");
                    coinsDoubled_ = true;
                    flyCoins({viewW_ / 2, overCoinsY_}, runCoins_, std::clamp(runCoins_ / 2, 4, 14));
                    hudBurst({viewW_ / 2, overCoinsY_}, 24, {kGold, kVolt, css("#ffffff")}, 300.f, true);
                    sfx(Sfx::Perfect, 8);
                });
            }, 0.9f);
        }
        bottom -= 50;
    }
}

// ================================================================ screens

void Game::hudScreen() {
    primary_ = nullptr;
    switch (screen_) {
    case Screen::AgeGate: hudAgeGate(); break;
    case Screen::Continue: hudContinue(); break;
    case Screen::Shop: hudShop(); break;
    case Screen::Locker: hudLocker(); break;
    case Screen::Missions: hudMissions(); break;
    case Screen::DailyDrop: hudDailyDrop(); break;
    case Screen::Settings: hudSettings(); break;
    case Screen::Dev: hudDev(); break;
    case Screen::LevelUp: hudLevelUp(); break;
    case Screen::StreakSave: hudStreakSave(); break;
    case Screen::ParentalGate: hudParentalGate(); break;
    case Screen::NotifPrimer: case Screen::ReplayPrimer: hudPrimer(); break;
    case Screen::Starter: hudStarter(); break;
    case Screen::None: break;
    }
    // the counter stays visible above panels that give or take coins, so the flying coins have somewhere to land
    if (screen_ == Screen::Shop || screen_ == Screen::DailyDrop || screen_ == Screen::LevelUp || screen_ == Screen::Missions ||
        screen_ == Screen::Starter || screen_ == Screen::StreakSave || screen_ == Screen::Continue || screen_ == Screen::Locker)
        hudCoinPill(false);
    if (rewardMsgT_ > 0) { // short notice ("No video available right now")
        const float a = std::min(1.f, rewardMsgT_ / 0.3f) * std::min(1.f, (2.2f - rewardMsgT_) / 0.2f + 0.01f);
        TextStyle m = style(FontId::BodyBold, 13, kText, 0.08f);
        m.opacity = a;
        const float w = text_->measure(rewardMsg_, m) + 32;
        const glm::vec2 c{viewW_ / 2, viewH_ - safeBottom_ - 40};
        hudRect(c, {w, 38}, kDeep, 0.92f * a, Shape::PillOutline, 1.f);
        hudRect(c, {w, 38}, kHot, 0.8f * a, Shape::PillOutline, 1.5f / 38);
        hudText(rewardMsg_, m, c.x, c.y - text_->lineBox(m) / 2);
    }
}

// ---------------------------------------------------------------- age gate (neutral: no year is pre-selected)
void Game::hudAgeGate() {
    const float W = viewW_;
    const int nowYear = std::atoi(svc_.clock->today().substr(0, 4).c_str());
    const float pw = std::min(W - 32, 360.f);
    TextStyle body = style(FontId::Body, 15, kText, 0.f, false, 1.4f);
    const auto lines = text_->wrap("We ask once so the game is set up right for you. Only your age group is kept on this device.", body, pw - 2 * kPad);
    const float contentH = text_->lineBox(body) * lines.size() + 18 + 70 + 22 + 56;
    const Panel p = hudPanel("Welcome", "What year were you born?", pw, contentH, kNeon, false);
    float y = p.top;
    body.opacity = 0.85f * p.k;
    for (const auto& l : lines) { hudText(l, body, p.c.x, y); y += text_->lineBox(body); }
    y += 18;
    // year picker: -10 -1 [YEAR] +1 +10
    {
        const float cy = y + 35;
        TextStyle yr = style(FontId::Display, 40, ageYear_ ? kText : kMuted, 0.04f, false, 1.f);
        if (ageYear_) yr.shadows = {hard(-2, kHot), hard(2, kNeon)};
        yr.opacity = p.k;
        hudText(ageYear_ ? std::to_string(ageYear_) : "- - - -", yr, p.c.x, cy - text_->lineBox(yr) / 2);
        const int steps[4] = {-10, -1, 1, 10};
        const float xs[4] = {-pw / 2 + 34, -pw / 2 + 80, pw / 2 - 80, pw / 2 - 34};
        for (int i = 0; i < 4; ++i) {
            const glm::vec2 c{p.c.x + xs[i], cy};
            hudRect(c, {40, 40}, kDeep, 0.9f * p.k, Shape::PillOutline, 1.f);
            hudRect(c, {40, 40}, kNeon, 0.6f * p.k, Shape::PillOutline, 1.5f / 40);
            TextStyle b = style(FontId::BodyBold, 14, kNeon);
            b.opacity = p.k;
            const std::string label = (steps[i] > 0 ? "+" : "") + std::to_string(steps[i]);
            hudText(label, b, c.x, c.y - text_->lineBox(b) / 2);
            const int step = steps[i];
            nextHitLabel_ = "year" + label;
            uiHit(c, {44, 48}, [this, step, nowYear] {
                const int base = ageYear_ ? ageYear_ : nowYear;
                ageYear_ = std::clamp(base + step, nowYear - 100, nowYear);
                sfx(Sfx::Lay, 2); buzz(5);
            });
        }
        y += 70 + 22;
    }
    const float a = ageYear_ ? p.k : 0.35f * p.k;
    auto confirm = [this, nowYear] {
        if (!ageYear_) return;
        const bool child = nowYear - ageYear_ <= 13; // conservative: a 13th birthday may not have happened yet this year
        profile_.setAudience(child ? Audience::Child : Audience::Teen);
        svc_.ads->setAudience(child);
        svc_.analytics->userProperty("audience", child ? "child" : "teen_adult");
        track("age_gate", {{"audience", child ? "child" : "teen_adult"}});
        closeScreen();
        queueSessionScreens();
    };
    hudButton("Continue", {p.c.x, y + 24}, kHot, a, ageYear_ != 0, ageYear_ ? std::function<void()>(confirm) : std::function<void()>{});
    if (ageYear_) primary_ = confirm;
}

// ---------------------------------------------------------------- continue (after a crash)
void Game::hudContinue() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    const bool first = run_.continues == 0;
    const int cost = first ? tune_.continueCost : tune_.continueCost2;
    const bool adOk = first && rewardReady(Placement::Continue);
    const bool coinsOk = wallet_.canAfford(cost);
    const float contentH = 120 + 14 + (adOk ? 62 : 0) + (coinsOk ? 62 : 0) + 40;
    const Panel p = hudPanel("Signal lost", "Continue?", pw, contentH, kHot, false);
    float y = p.top;
    // countdown ring
    {
        const glm::vec2 c{p.c.x, y + 58};
        const float left = clampf(1.f - continueT_ / tune_.continueSeconds, 0.f, 1.f);
        constexpr int SEG = 48;
        const float r = 50.f;
        hudRect(c, glm::vec2(r * 3.2f), kHot, 0.25f * p.k, Shape::RadialGlow);
        for (int i = 0; i < SEG; ++i) {
            const float u = float(i) / SEG, ang = -kPi / 2 + u * 2 * kPi;
            const bool lit = u < left;
            const glm::vec3 col = glm::mix(kRed, kGreen, left);
            hudRect(c + glm::vec2(std::cos(ang), std::sin(ang)) * r, {5.5f, 12}, lit ? col : kDeep, (lit ? 1.f : 0.8f) * p.k, Shape::Solid, 0, ang + kPi / 2);
        }
        const float secs = std::max(0.f, tune_.continueSeconds - continueT_);
        const int n = int(std::ceil(secs));
        const float tick = 1.f - (secs - std::floor(secs)); // 0 -> 1 through each second
        TextStyle num = style(FontId::Display, 46 * (1.f + 0.25f * std::max(0.f, 1.f - tick * 4.f)), kText, 0.f, false, 1.f);
        num.shadows = {hard(-2, kHot), hard(2, kNeon), blur(20, kHot, 0.6f)};
        num.opacity = p.k;
        hudText(std::to_string(std::max(n, 0)), num, c.x, c.y - text_->lineBox(num) / 2);
        y += 120 + 14;
    }
    std::function<void()> viaAd = [this] {
        requestReward(Placement::Continue, [this] { track("continue_used", {{"method", "ad"}}); revive(); });
    };
    std::function<void()> viaCoins = [this, cost] {
        if (wallet_.spend(cost, "continue")) { track("continue_used", {{"method", "coins"}}); revive(); }
    };
    if (adOk) {
        const Icon ad = Icon::AdBadge;
        nextHitLabel_ = "Watch ad";
        hudActionButton("Watch ad", &ad, 0, {p.c.x, y + 24}, kHot, p.k, true, viaAd);
        y += 62;
    }
    if (coinsOk) {
        nextHitLabel_ = "Pay";
        hudActionButton("Revive", nullptr, cost, {p.c.x, y + 24}, kGold, p.k, !adOk, viaCoins);
        y += 62;
    }
    primary_ = adOk ? viaAd : viaCoins;
    hudTextButton("No thanks", {p.c.x, y + 18}, p.k, [this] { closeScreen(); finishRun(); });
}

// ---------------------------------------------------------------- shop
// Glass panel, tech-framed tiles (free = green, 300 = gold, best value = pink, the rest glass), the starter pack and
// Remove Ads rows, and the menu row kept live underneath with Shop lit (so Locker / Missions are one tap away).
void Game::hudShop() {
    const float W = viewW_, H = viewH_;
    const float k = easeOut(screenT_ / 0.3f), pop = easeOutBack(screenT_ / 0.4f, 1.3f);
    const bool starter = !wallet_.owns("hen_glitch");
    const bool noads = !adPolicy_.removeAds();
    // lay out between the top bar and the menu row
    const float rowTop = (state_ == State::Dead ? overButtonRowBottom_ - 48 - 30 : titleNavY_ - 30) - 6;
    const float top = safeTop_ + kTopBarY + 30;
    const float pw = std::min(W - 32, 400.f), pad = 16, gap = 12;
    const float tileW = (pw - 2 * pad - gap) / 2;
    const float headH = 56, starterH = starter ? 76 : 0, noadsH = noads ? 60 : 0, restoreH = 36;
    const float fixed = headH + (starter ? starterH + gap : 0) + (noads ? noadsH + gap : 0) + restoreH + pad;
    const float tileH = clampf((rowTop - top - fixed - 3 * gap) / 3, 92, 132);
    const float ph = std::min(fixed + 3 * tileH + 3 * gap, rowTop - top);
    const glm::vec2 c{W / 2, top + ph / 2 + 26.f * (1.f - pop)};
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.42f * k);
    // frosted glass: the renderer blurs the city behind the panel (composite pass), then a light tint, the glass
    // frame's bright edge and a diagonal sheen sit on top. The frost rect matches FrameGlass's inner rounded rect.
    hudFrost(c, {pw - 11, ph - 11}, 18, k);
    hudNineSlice(Icon::FrameGlass, c, {pw + 6, ph + 6}, 34, glm::vec3(0.62f, 0.66f, 0.95f), 0.72f * k);
    hudRect({c.x, c.y - ph * 0.3f}, {pw * 1.1f, ph * 0.6f}, css("#8a7cff"), 0.10f * k, Shape::RadialGlow);
    {   // a slow sheen sliding across the glass
        const float s = std::fmod(time_ * 0.12f, 1.4f);
        if (s < 1.f) hudRect({c.x - pw / 2 + pw * s, c.y}, {pw * 0.35f, ph * 0.98f}, glm::vec3(1.f), 0.035f * k * std::sin(s * kPi), Shape::Streak, 0, 0.35f);
    }
    TextStyle title = style(FontId::Display, 30, kText, 0.f, false, 1.f);
    title.shadows = {hard(-2, kHot), hard(2, kNeon), blur(20, kHot, 0.4f)};
    title.opacity = k;
    float y = c.y - ph / 2 + 16;
    hudText("Coin Shop", title, c.x, y);
    y += headH - 8;
    {   // close button on the corner
        const glm::vec2 xc{c.x + pw / 2 - 6, c.y - ph / 2 + 8};
        const float p = easeOutBack(seg(screenT_, 0.12f, 0.45f), 2.4f);
        hudSprite(Icon::Close, xc, 60 * p, 0.06f * std::sin(screenT_ * 2.3f), glm::vec3(1.f), k);
        nextHitLabel_ = "Close";
        uiHit(xc, {52, 52}, [this] { closeScreen(); });
    }
    auto priceOf = [this](const ProductDef& d) {
        for (const Product& pr : products_) if (pr.id == d.id && !pr.price.empty()) return pr.price;
        return std::string(d.fallbackPrice);
    };
    // a framed price / action button; returns its hit size
    auto button = [&](glm::vec2 bc, glm::vec2 bs, Icon frame, std::string_view text, const Icon* icon, float al) {
        hudNineSlice(frame, bc, bs, bs.y / 2, glm::vec3(1.f), al, 0.5f);
        TextStyle t = style(FontId::BodyBold, std::min(18.f, bs.y * 0.46f), kText, 0.03f);
        t.shadows = {hard(1.5f, css("#05030f"), 0.7f)};
        t.opacity = al;
        const float tw = text_->measure(text, t) + (icon ? 30 : 0);
        float x = bc.x - tw / 2;
        if (icon) { hudSprite(*icon, {x + 12, bc.y}, 34, 0, glm::vec3(1.f), al); x += 30; }
        hudText(text, t, x, bc.y - text_->lineBox(t) / 2, TextAlign::Left);
    };
    auto amount = [&](glm::vec2 at, int coins, glm::vec3 color, float al) {
        TextStyle t = style(FontId::BodyBold, 21, color, 0.02f);
        t.shadows = {blur(8, color, 0.35f)};
        t.opacity = al;
        const std::string txt = grouped(coins);
        const float w = 18 + 6 + text_->measure(txt, t);
        hudCoin({at.x - w / 2 + 9, at.y}, 18, al, 0.f);
        hudText(txt, t, at.x - w / 2 + 24, at.y - text_->lineBox(t) / 2, TextAlign::Left);
    };
    // ---- the 2 x 3 grid
    const Icon piles[5] = {Icon::Pile300, Icon::Pile1000, Icon::Pile1800, Icon::Pile4000, Icon::Pile9000};
    for (int i = 0; i < 6; ++i) {
        const int col = i % 2, row = i / 2;
        const Up a = up(screenT_, 0.05f + i * 0.04f);
        const glm::vec2 tc{c.x + (col ? 1.f : -1.f) * (tileW / 2 + gap / 2), y + row * (tileH + gap) + tileH / 2 + a.dy};
        const float al = k * a.alpha;
        // bottom up: price button, coin amount, then the art fills what's left
        const float bh = std::min(38.f, tileH * 0.28f);
        const glm::vec2 btn{tc.x, tc.y + tileH * 0.5f - bh / 2 - 7};
        const glm::vec2 bsz{std::min(tileW * 0.62f, 118.f), bh};
        const float amountY = btn.y - bh / 2 - 13;
        const float artTop = tc.y - tileH / 2 + 8, artBottom = amountY - 11;
        const glm::vec2 artC{tc.x, (artTop + artBottom) / 2};
        const float artH = artBottom - artTop;
        if (i == 0) { // free coins (rewarded ad)
            const bool ok = adPolicy_.canOffer(Placement::FreeCoins) && !profile_.child();
            const int64_t cd = adPolicy_.cooldownLeft(Placement::FreeCoins);
            hudRect(tc, {tileW * 1.15f, tileH * 1.2f}, kGreen, (0.10f + 0.04f * std::sin(time_ * 2.2f)) * al, Shape::RadialGlow);
            hudNineSlice(Icon::FrameFree, tc, {tileW, tileH}, tileH / 2, glm::vec3(1.f), al, 0.5f); // FREE tab baked in
            const float bob = std::sin(time_ * 2.2f) * 3.f;
            const glm::vec2 coinC{tc.x, (artTop + btn.y - bh / 2) / 2 + 2};
            const float coinS = std::min(btn.y - bh / 2 - artTop, tileW * 0.5f) * 1.02f; // art fills ~85% of its cell
            hudRect(coinC, glm::vec2(coinS * 1.3f), kGreen, 0.28f * al, Shape::RadialGlow);
            hudSprite(Icon::CoinGreen, coinC + glm::vec2(0, bob), coinS, 0, glm::vec3(1.f), al);
            if (ok) {
                const Icon ad = Icon::AdBadge;
                button(btn, bsz, Icon::BtnDark, "+" + std::to_string(tune_.freeCoins), &ad, al);
                nextHitLabel_ = "Free coins";
                uiHit(tc, {tileW, tileH}, [this, tc] {
                    requestReward(Placement::FreeCoins, [this, tc] { wallet_.earn(tune_.freeCoins, "free_coins_ad"); flyCoins(tc, tune_.freeCoins, 8); });
                });
            } else {
                TextStyle w = style(FontId::BodyBold, 13, kMuted, 0.1f, true);
                w.opacity = al;
                hudText(cd > 0 ? "Next in " + mmss(cd) : "Back tomorrow", w, btn.x, btn.y - text_->lineBox(w) / 2);
            }
            continue;
        }
        const ProductDef& d = kProducts[i - 1];
        const bool gold = i == 1, best = d.bestValue;
        const glm::vec3 accent = best ? kHot : gold ? kGold : glm::vec3(1.f);
        if (best || gold) {
            hudRect(tc, {tileW * 1.15f, tileH * 1.2f}, accent, 0.10f * al, Shape::RadialGlow);
            hudNineSlice(Icon::FrameNeon, tc, {tileW, tileH}, tileH / 2, accent, al, 0.5f);
            if (best) { // the circuit grid behind the coins
                for (int g = 1; g < 6; ++g) {
                    hudRect({tc.x - tileW / 2 + tileW * g / 6.f, tc.y}, {1, tileH - 30}, kHot, 0.10f * al);
                    hudRect({tc.x, tc.y - tileH / 2 + tileH * g / 6.f}, {tileW - 30, 1}, kHot, 0.10f * al);
                }
            }
        } else {
            hudNineSlice(Icon::FrameGlass, tc, {tileW, tileH}, 24, glm::vec3(0.85f, 0.9f, 1.f), al);
        }
        // glint sweeping across the tile every few seconds
        {
            const float g = std::fmod(time_ * 0.35f + i * 0.17f, 1.6f);
            if (g < 1.f) hudRect({tc.x - tileW / 2 + tileW * g, tc.y}, {18, tileH * 0.9f}, glm::vec3(1.f), 0.06f * al * std::sin(g * kPi), Shape::Streak, 0, 0.35f);
        }
        // pile art (two cells wide), sized so the bigger hoards fill the tile like the mockup
        const float pileH = std::min(artH * 1.06f, (tileW - 12) * 0.5f); // cell height; the art fills ~94% of it
        if (piles[i - 1] == Icon::Pile9000) { // the hoard, big (as wide as the tile) so it reads as a lot; it overflows the
            // tile's top edge and the amount and price sit over its lower part (a soft dark glow keeps them readable)
            const float zs = (tileW - 4) / 3.f;                                  // 3 x 2 block cell: block = tile width
            const float heapH = zs * 2.f * 0.914f;                               // the art is ~91% of its block's height
            // raised so the peak pokes ~14% of its height out past the tile's top edge: too many coins to fit the box
            hudSprite(Icon::Pile9000, {tc.x, tc.y - tileH / 2 + 4 + heapH / 2 - heapH * 0.14f}, zs, 0, glm::vec3(1.f), al);
            hudRect({tc.x, (amountY + btn.y) / 2}, {tileW * 0.95f, bh * 3.2f}, css("#0a0818"), 0.5f * al, Shape::RadialGlow);
        }
        else
            hudSprite(piles[i - 1], artC, pileH, 0, glm::vec3(1.f), al);
        amount({tc.x, amountY}, d.coins, best ? css("#ff9cf0") : gold ? css("#ffe7a0") : kText, al);
        button(btn, bsz, best ? Icon::BtnDark : gold ? Icon::BtnGold : Icon::BtnBlue, priceOf(d), nullptr, al);
        if (best) { // BEST VALUE ribbon on the top edge (the art has the words)
            const float rs = tileW * 0.29f; // cell height; the ribbon spans ~2 cells wide, ~0.6 of the height
            hudSprite(Icon::RibbonBest, {tc.x + tileW / 2 - rs + 4, tc.y - tileH / 2 + rs * 0.2f}, rs, 0, glm::vec3(1.f), al);
        }
        nextHitLabel_ = d.id;
        uiHit(tc, {tileW, tileH}, [this, id = std::string(d.id)] { startPurchase(id); });
    }
    y += 3 * (tileH + gap);
    const float rowW = pw - 2 * pad;
    if (starter) {
        const ProductDef& d = *findProduct("starter_pack");
        const Up a = up(screenT_, 0.3f);
        const glm::vec2 rc{c.x, y + starterH / 2 + a.dy};
        const float al = k * a.alpha;
        hudRect(rc, {rowW * 1.05f, starterH * 1.6f}, kViolet, 0.22f * al, Shape::RadialGlow);
        hudRect(rc, {rowW - 6, starterH - 6}, css("#5b1d9e"), 0.85f * al, Shape::PillOutline, 1.f);           // purple body
        hudRect({rc.x - rowW * 0.2f, rc.y}, {rowW * 0.9f, starterH * 1.4f}, css("#c03cff"), 0.35f * al, Shape::RadialGlow);
        hudNineSlice(Icon::FrameGlass, rc, {rowW, starterH}, 24, css("#e2a8ff"), al);
        {   // the chest, glowing and bobbing
            const glm::vec2 cc{rc.x - rowW / 2 + 44, rc.y + 1 + std::sin(time_ * 2.f) * 2.f};
            hudRect(cc, glm::vec2(96), css("#ffd23a"), (0.22f + 0.1f * std::sin(time_ * 3.f)) * al, Shape::RadialGlow);
            hudSprite(Icon::Chest3D, cc, 84, 0, glm::vec3(1.f), al);
        }
        TextStyle h = style(FontId::BodyBold, 22, kText, 0.02f);
        h.opacity = al;
        hudText("Starter Pack", h, rc.x - rowW / 2 + 94, rc.y - 30, TextAlign::Left);
        TextStyle sb = style(FontId::Body, 15, css("#e6d4ff"), 0.02f);
        sb.opacity = al;
        hudText("Glitch Hen outfit", sb, rc.x - rowW / 2 + 94, rc.y - 7, TextAlign::Left);
        TextStyle am = style(FontId::BodyBold, 16, css("#ffe7a0"), 0.02f);
        am.opacity = al;
        hudCoin({rc.x - rowW / 2 + 102, rc.y + 21}, 16, al, 0.f);
        hudText("+" + grouped(tune_.starterCoins), am, rc.x - rowW / 2 + 114, rc.y + 21 - text_->lineBox(am) / 2, TextAlign::Left);
        button({rc.x + rowW / 2 - 64, rc.y}, {112, 44}, Icon::BtnDark, priceOf(d), nullptr, al);
        nextHitLabel_ = "starter_pack";
        uiHit(rc, {rowW, starterH}, [this] { startPurchase("starter_pack"); });
        y += starterH + gap;
    }
    if (noads) {
        const ProductDef& d = *findProduct("remove_ads");
        const Up a = up(screenT_, 0.36f);
        const glm::vec2 rc{c.x, y + noadsH / 2 + a.dy};
        const float al = k * a.alpha;
        hudRect(rc, {rowW - 6, noadsH - 6}, css("#151230"), 0.9f * al, Shape::PillOutline, 1.f);
        hudRect({rc.x + rowW * 0.3f, rc.y}, {rowW * 0.8f, noadsH * 1.6f}, kHot, 0.16f * al, Shape::RadialGlow);
        hudNineSlice(Icon::FrameGlass, rc, {rowW, noadsH}, 22, glm::vec3(0.7f, 0.8f, 1.f), al);
        hudSprite(Icon::NoAds, {rc.x - rowW / 2 + 38, rc.y}, 58, 0, glm::vec3(1.f), al);
        TextStyle h = style(FontId::BodyBold, 18, kText, 0.08f, true);
        h.opacity = al;
        hudText("Remove ads", h, rc.x - rowW / 2 + 74, rc.y - 22, TextAlign::Left);
        TextStyle am = style(FontId::BodyBold, 16, css("#ffe7a0"), 0.02f);
        am.opacity = al;
        hudCoin({rc.x - rowW / 2 + 82, rc.y + 11}, 16, al, 0.f);
        hudText("+" + grouped(tune_.removeAdsBonus), am, rc.x - rowW / 2 + 94, rc.y + 11 - text_->lineBox(am) / 2, TextAlign::Left);
        button({rc.x + rowW / 2 - 64, rc.y}, {112, 42}, Icon::BtnBlue, priceOf(d), nullptr, al);
        nextHitLabel_ = "remove_ads";
        uiHit(rc, {rowW, noadsH}, [this] { startPurchase("remove_ads"); });
        y += noadsH + gap;
    }
    hudTextButton("Restore purchases", {c.x, y + restoreH / 2 - 4}, k, [this] { svc_.store->restore(); showMessage("Restoring purchases\u2026"); });
    // the menu row stays live under the panel, with Shop lit
    if (state_ == State::Dead) hudOverMenu(overButtonRowBottom_);
    else if (state_ == State::Title) hudTitleNav(titleNavY_);
}

// ---------------------------------------------------------------- locker (bottom sheet; the hen previews above it)
void Game::hudLocker() {
    const float W = viewW_, H = viewH_;
    const float k = easeOut(screenT_ / 0.35f);
    const float sheetH = std::min(H * 0.62f, 600.f);
    const float top = H - sheetH * k;
    hudRect({W / 2, H / 2}, {W, H}, kInk, 0.25f * k, Shape::BottomFade);
    hudRect({W / 2, top + sheetH / 2 + 30}, {W + 4, sheetH + 60}, kPanel, 0.98f, Shape::PillOutline, 1.f);
    hudRect({W / 2, top + sheetH / 2 + 30}, {W + 4, sheetH + 60}, kNeon, 0.8f, Shape::PillOutline, 1.5f / (sheetH + 60));
    hudRect({W / 2, top + 10}, {44, 5}, kMuted, 0.6f, Shape::PillOutline, 1.f);
    // header + close
    TextStyle head = style(FontId::Display, 24, kText, 0.f, false, 1.f);
    head.shadows = {hard(-2, kHot), hard(2, kNeon)};
    hudText("Locker", head, 22, top + 22, TextAlign::Left);
    {
        const glm::vec2 xc{W - 36, top + 34};
        hudSprite(Icon::Close, xc, 54, 0, glm::vec3(1.f), k);
        nextHitLabel_ = "Close";
        uiHit(xc, {50, 50}, [this] { closeScreen(); });
    }
    // tabs
    const char* tabs[3] = {"Outfits", "Trails", "Crash fx"};
    const float tabY = top + 78, tabW = (W - 44) / 3;
    for (int i = 0; i < 3; ++i) {
        const glm::vec2 c{22 + tabW * (i + 0.5f), tabY};
        const bool on = int(lockerTab_) == i;
        hudRect(c, {tabW - 8, 36}, on ? kHot : kDeep, on ? 0.9f : 0.7f, Shape::PillOutline, 1.f);
        if (!on) hudRect(c, {tabW - 8, 36}, kNeon, 0.4f, Shape::PillOutline, 1.2f / 36);
        TextStyle t = style(FontId::BodyBold, 13, on ? css("#ffffff") : kNeon, 0.14f, true);
        hudText(tabs[i], t, c.x, c.y - text_->lineBox(t) / 2);
        const Slot s = Slot(i);
        nextHitLabel_ = tabs[i];
        uiHit(c, {tabW - 4, 44}, [this, s] { lockerTab_ = s; lockerPreview_.clear(); applyCosmetics(); });
    }
    // grid
    std::vector<const Cosmetic*> items;
    for (const Cosmetic& c : catalog()) if (c.slot == lockerTab_) items.push_back(&c);
    const int cols = 3;
    const int rows = int((items.size() + cols - 1) / cols);
    const float gridTop = tabY + 30, actionH = 74;
    const float cellW = (W - 44 - (cols - 1) * 10) / cols;
    const float cellH = clampf((H - gridTop - actionH - safeBottom_ - 10 - (rows - 1) * 10) / float(rows), 58, 96);
    const Cosmetic* featured = featuredOutfit(civilDay(svc_.clock->today()));
    const std::string& sel = lockerPreview_.empty() ? wallet_.active(lockerTab_).id : lockerPreview_;
    for (size_t i = 0; i < items.size(); ++i) {
        const Cosmetic& it = *items[i];
        const int col = int(i) % cols, row = int(i) / cols;
        const Up a = up(screenT_, 0.05f + i * 0.025f);
        const glm::vec2 c{22 + cellW / 2 + col * (cellW + 10), gridTop + cellH / 2 + row * (cellH + 10) + a.dy};
        const bool owned = wallet_.owns(it.id);
        const bool equipped = wallet_.equipped(it.slot) == it.id;
        const bool selected = sel == it.id;
        const glm::vec3 accent = selected ? kHot : owned ? kNeon : kMuted;
        const Icon tile = it.slot == Slot::Outfit ? outfitTile(it.id) : Icon::Close;
        if (tile != Icon::Close) {
            // artist tile (403 x 270 art in a 2 x 2 cell block: 0.979 of the block wide, 0.656 tall)
            const float tw = std::min(cellW, cellH / 0.67f) * (selected ? 1.f + 0.025f * std::sin(time_ * 4.f) : 1.f), th = tw * 0.67f;
            if (selected) {
                hudRect(c, {tw * 1.25f, th * 1.5f}, kHot, 0.28f * a.alpha, Shape::RadialGlow);
                hudRect(c, {tw + 8, th + 8}, kHot, 0.95f * a.alpha, Shape::PillOutline, 2.4f / (th + 8));
            }
            hudSprite(tile, c, tw / (2 * 0.979f), 0, glm::vec3(1.f), a.alpha);
            const glm::vec2 corner{c.x + tw / 2 - tw * 0.11f, c.y - th / 2 + th * 0.17f}; // where the sheet's padlock sat
            if (equipped) hudSprite(Icon::Check, corner, tw * 0.22f, 0, glm::vec3(1.f), a.alpha);
            else if (!owned) hudSprite(Icon::Lock, corner, tw * 0.2f, 0, glm::vec3(1.f), a.alpha);
            if (featured && featured->id == it.id && !owned) {
                const float pulse = 0.5f + 0.5f * std::sin(time_ * 4);
                const glm::vec2 wc{c.x - tw / 2 + 21, c.y - th / 2 + 10};
                hudRect(wc, {40, 18}, kVolt, a.alpha, Shape::PillOutline, 1.f);
                TextStyle f = style(FontId::BodyBold, 10, css("#1a0420"), 0.1f, true);
                f.opacity = a.alpha;
                hudText("Week", f, wc.x, wc.y - text_->lineBox(f) / 2);
                hudRect(c, {tw + 6, th + 6}, kVolt, 0.3f * pulse * a.alpha, Shape::PillOutline, 3.f / th);
            }
            nextHitLabel_ = it.id;
            uiHit(c, {cellW, cellH}, [this, id = std::string(it.id)] {
                lockerPreview_ = id;
                applyCosmetics();
                sfx(Sfx::Lay, 3); buzz(6);
            });
            continue;
        }
        nextHitLabel_ = it.id;
        hudTile(c, {cellW, cellH}, accent, a.alpha, selected, [this, id = std::string(it.id)] {
            lockerPreview_ = id;
            applyCosmetics();
            sfx(Sfx::Lay, 3); buzz(6);
        });
        // swatch: outfits = a little hen-colour badge (body, accent comb, visor); trails / crash = two-colour orb
        const glm::vec2 sc{c.x, c.y - cellH * 0.1f};
        const float R = std::min(cellW, cellH) * 0.5f;
        if (it.slot == Slot::Outfit) {
            const glm::vec3 body = glm::clamp(glm::pow(it.skin.body, glm::vec3(1 / 2.2f)), 0.f, 1.f);
            const glm::vec3 acc = it.skin.rainbow ? hueColor(time_ * 0.6f) : glm::clamp(glm::pow(it.skin.accent, glm::vec3(1 / 2.2f)), 0.f, 1.f);
            const glm::vec3 vis = glm::clamp(glm::pow(it.skin.visor, glm::vec3(1 / 2.2f)), 0.f, 1.f);
            const glm::vec3 plate = glm::clamp(glm::pow(it.skin.plate, glm::vec3(1 / 2.2f)), 0.f, 1.f);
            hudRect(sc, glm::vec2(R * 1.9f), acc, 0.25f * a.alpha, Shape::RadialGlow);
            hudRect({sc.x, sc.y - R * 0.48f}, {R * 0.42f, R * 0.34f}, acc, a.alpha, Shape::SoftDisc);          // comb
            hudRect(sc, {R * 1.02f, R * 1.02f}, plate, a.alpha, Shape::SoftDisc);                               // outline
            hudRect(sc, {R * 0.92f, R * 0.92f}, body, a.alpha, Shape::SoftDisc);                                // head
            hudRect({sc.x + R * 0.1f, sc.y - R * 0.04f}, {R * 0.62f, R * 0.17f}, vis, a.alpha, Shape::PillOutline, 1.f); // visor
            hudRect({sc.x + R * 0.1f, sc.y - R * 0.04f}, {R * 0.9f, R * 0.4f}, vis, 0.35f * a.alpha, Shape::RadialGlow);
            hudRect({sc.x - R * 0.18f, sc.y - R * 0.22f}, {R * 0.22f, R * 0.12f}, glm::vec3(1.f), 0.5f * a.alpha, Shape::SoftDisc); // gloss
        } else {
            hudRect(sc, glm::vec2(R * 1.7f), it.swatch[0], 0.35f * a.alpha, Shape::RadialGlow);
            hudRect(sc, {R * 0.84f, R * 0.84f}, it.swatch[1], a.alpha, Shape::SoftDisc);
            hudRect({sc.x - R * 0.12f, sc.y - R * 0.12f}, {R * 0.5f, R * 0.5f}, it.swatch[0], a.alpha, Shape::SoftDisc);
            hudRect({sc.x - R * 0.2f, sc.y - R * 0.22f}, {R * 0.18f, R * 0.1f}, glm::vec3(1.f), 0.6f * a.alpha, Shape::SoftDisc);
        }
        TextStyle nm = style(FontId::BodyBold, 10.5f, kText, 0.06f, true);
        nm.opacity = a.alpha * (owned ? 1.f : 0.75f);
        hudText(it.name, nm, c.x, c.y + cellH / 2 - 8 - text_->lineBox(nm));
        if (equipped) hudSprite(Icon::Check, {c.x + cellW / 2 - 13, c.y - cellH / 2 + 13}, 26, 0, glm::vec3(1.f), a.alpha);
        else if (!owned) hudSprite(Icon::Lock, {c.x + cellW / 2 - 13, c.y - cellH / 2 + 13}, 24, 0, glm::vec3(1.f), a.alpha);
        if (featured && featured->id == it.id && !owned) {
            const float pulse = 0.5f + 0.5f * std::sin(time_ * 4);
            hudRect({c.x - cellW / 2 + 22, c.y - cellH / 2 + 11}, {38, 16}, kVolt, 0.9f * a.alpha, Shape::PillOutline, 1.f);
            TextStyle f = style(FontId::BodyBold, 9, css("#1a0420"), 0.12f, true);
            f.opacity = a.alpha;
            hudText("Week", f, c.x - cellW / 2 + 22, c.y - cellH / 2 + 11 - text_->lineBox(f) / 2);
            hudRect(c, {cellW + 6, cellH + 6}, kVolt, 0.25f * pulse * a.alpha, Shape::PillOutline, 3.f / cellH);
        }
    }
    // action for the selected item
    const Cosmetic* cur = findCosmetic(sel);
    if (!cur) return;
    const glm::vec2 ac{W / 2, H - safeBottom_ - actionH / 2 - 4};
    const bool owned = wallet_.owns(cur->id);
    if (owned) {
        const bool equipped = wallet_.equipped(cur->slot) == cur->id;
        hudButton(equipped ? "Equipped" : "Equip", ac, equipped ? kGreen : kHot, equipped ? 0.6f : 1.f, !equipped,
                  equipped ? std::function<void()>{} : std::function<void()>([this, id = std::string(cur->id)] {
                      wallet_.equip(id); lockerPreview_.clear(); applyCosmetics();
                      sfx(Sfx::Perfect, 6); buzz(15);
                      hudBurst({viewW_ / 2, viewH_ * 0.3f}, 30, {kHot, kNeon, kVolt}, 380.f);
                      track("cosmetic_equip", {{"id", id}});
                  }));
        return;
    }
    const std::string id = cur->id;
    switch (cur->unlock) {
    case Unlock::Coins: case Unlock::Featured: {
        const bool isFeatured = cur->unlock == Unlock::Featured && featured && featured->id == cur->id;
        const bool second = !profile_.child() && (isFeatured || (cur->slot == Slot::Outfit && wallet_.tryOnId() != id));
        const float x = second ? W * 0.27f : ac.x, x2 = W * 0.73f; // two buttons: one per half
        hudActionButton("Buy", nullptr, cur->price, {x, ac.y}, kGold, 1.f, wallet_.canAfford(cur->price), [this, id] {
            const Cosmetic* c = findCosmetic(id);
            if (!c) return;
            if (!wallet_.canAfford(c->price)) { showMessage("Not enough coins"); openScreen(Screen::Shop); return; }
            if (wallet_.buy(*c)) {
                wallet_.equip(id); lockerPreview_.clear(); applyCosmetics();
                sfx(Sfx::SurgeStart); buzz(25);
                hudBurst({viewW_ / 2, viewH_ * 0.3f}, 46, {kGold, kHot, kNeon, kVolt}, 460.f, true);
                flashScreen(srgbColor("#ffd23a"), 0.2f);
                track("cosmetic_buy", {{"id", id}, {"price", std::to_string(c->price)}});
            }
        });
        if (isFeatured && !profile_.child()) {
            const int have = wallet_.featuredProgress(id);
            const Icon ad = Icon::AdBadge;
            char lbl[32];
            std::snprintf(lbl, sizeof lbl, "%d/%d", have, tune_.featuredAdsNeeded);
            nextHitLabel_ = "Featured ad";
            hudActionButton(lbl, &ad, 0, {x2, ac.y}, kVolt, rewardReady(Placement::Featured) ? 1.f : 0.45f, true, [this, id] {
                requestReward(Placement::Featured, [this, id] {
                    if (wallet_.addFeaturedProgress(id) >= tune_.featuredAdsNeeded) {
                        wallet_.grant(id); wallet_.equip(id); lockerPreview_.clear(); applyCosmetics();
                        hudBurst({viewW_ / 2, viewH_ * 0.3f}, 46, {kVolt, kHot, kNeon}, 460.f);
                        toast("Unlocked!", Icon::Star);
                    }
                });
            });
        } else if (cur->slot == Slot::Outfit && !profile_.child() && wallet_.tryOnId() != id) {
            // not featured: a rewarded ad lets you wear it for a few runs
            const Icon ad = Icon::AdBadge;
            nextHitLabel_ = "Try";
            hudActionButton("Try", &ad, 0, {x2, ac.y}, kNeon, rewardReady(Placement::TryOn) ? 1.f : 0.45f, false, [this, id] {
                requestReward(Placement::TryOn, [this, id] {
                    wallet_.tryOn(id, tune_.tryOnRuns); lockerPreview_.clear(); applyCosmetics();
                    toast("Yours for " + std::to_string(tune_.tryOnRuns) + " runs", Icon::Locker);
                });
            });
        }
        break;
    }
    case Unlock::Level: {
        TextStyle t = style(FontId::BodyBold, 14, kVolt, 0.14f, true);
        hudSprite(Icon::Star, {ac.x - 80, ac.y}, 36);
        hudText("Reach level " + std::to_string(cur->level), t, ac.x - 58, ac.y - text_->lineBox(t) / 2, TextAlign::Left);
        break;
    }
    case Unlock::Starter:
        hudButton("Get starter pack", ac, kViolet, 1.f, true, [this] { openScreen(Screen::Shop); });
        break;
    case Unlock::Free: break;
    }
}

// ---------------------------------------------------------------- daily missions
void Game::hudMissions() {
    const float W = viewW_;
    const float pw = std::min(W - 24, 380.f);
    const float rowH = 74;
    const float contentH = 3 * (rowH + 10) + 96 + 30;
    const Panel p = hudPanel("Daily missions", "Today’s jobs", pw, contentH, kHot, true);
    float y = p.top;
    for (int i = 0; i < 3; ++i) {
        const Mission& m = missions_.list()[size_t(i)];
        const Up a = up(screenT_, 0.08f + i * 0.07f);
        const glm::vec2 c{p.c.x, y + rowH / 2 + a.dy};
        const float al = p.k * a.alpha;
        hudTile(c, {pw - 32, rowH}, m.done ? kGreen : kNeon, al, m.done, {});
        hudSprite(m.done ? Icon::Check : Icon::Missions, {c.x - (pw - 32) / 2 + 32, c.y}, 46, 0, glm::vec3(1.f), al);
        TextStyle d = style(FontId::BodyBold, 13.5f, kText, 0.04f);
        d.opacity = al;
        hudText(Missions::describe(m), d, c.x - (pw - 32) / 2 + 62, c.y - 24, TextAlign::Left);
        // progress bar
        const float bw = pw - 32 - 62 - 90, bx = c.x - (pw - 32) / 2 + 62;
        const float f = float(m.progress) / float(std::max(1, m.target));
        hudRect({bx + bw / 2, c.y + 10}, {bw, 10}, kDeep, al, Shape::PillOutline, 1.f);
        if (f > 0) hudRect({bx + bw * f / 2, c.y + 10}, {bw * f, 6}, m.done ? kGreen : kHot, al, Shape::PillOutline, 1.f);
        TextStyle pr = style(FontId::BodyBold, 11, kMuted, 0.08f);
        pr.opacity = al;
        hudText(std::to_string(m.progress) + " / " + std::to_string(m.target), pr, bx, c.y + 18, TextAlign::Left);
        // reward, or reroll for unfinished ones
        const float rx = c.x + (pw - 32) / 2 - 46;
        if (m.done) hudCoinAmount(tune_.missionReward, {rx, c.y - 2}, 18, al, TextAlign::Center);
        else {
            const bool adOk = rewardReady(Placement::MissionReroll) && !profile_.child();
            hudCoinAmount(tune_.missionReward, {rx, c.y - 14}, 16, al * 0.8f, TextAlign::Center);
            const Icon ad = Icon::AdBadge;
            nextHitLabel_ = "Swap" + std::to_string(i);
            hudActionButton("Swap", adOk ? &ad : nullptr, adOk ? 0 : tune_.rerollCost, {rx, c.y + 18}, kViolet, al, false, [this, i, adOk] {
                auto swap = [this, i] { missions_.reroll(i); sfx(Sfx::Lay, 5); track("mission_reroll", {}); };
                if (adOk) requestReward(Placement::MissionReroll, swap);
                else if (wallet_.spend(tune_.rerollCost, "mission_reroll")) swap();
                else showMessage("Not enough coins");
            }, 0.62f);
        }
        y += rowH + 10;
    }
    // all-three bonus
    {
        const glm::vec2 c{p.c.x, y + 44};
        const bool ready = missions_.allDone() && !missions_.bonusClaimed();
        hudTile(c, {pw - 32, 88}, ready ? kVolt : kViolet, p.k, ready, {});
        hudSprite(missions_.bonusClaimed() ? Icon::ChestOpen : Icon::ChestClosed, {c.x - (pw - 32) / 2 + 44, c.y}, 72 * (ready ? 1.f + 0.05f * std::sin(time_ * 6) : 1.f), 0,
                  glm::vec3(1.f), p.k);
        TextStyle h = style(FontId::BodyBold, 13, kText, 0.12f, true);
        h.opacity = p.k;
        hudText(missions_.bonusClaimed() ? "Bonus collected" : "Finish all three", h, c.x - (pw - 32) / 2 + 84, c.y - 26, TextAlign::Left);
        hudCoinAmount(tune_.missionAllBonus, {c.x - (pw - 32) / 2 + 84, c.y + 2}, 20, p.k, TextAlign::Left, true);
        if (ready) {
            auto claim = [this, c](bool twice) {
                missions_.claimBonus();
                const int coins = tune_.missionAllBonus * (twice ? 2 : 1);
                wallet_.earn(coins, twice ? "mission_bonus_x2" : "mission_bonus");
                flyCoins(c, coins, 14);
                hudBurst(c, 36, {kVolt, kGold, kHot}, 380.f, true);
                sfx(Sfx::SurgeStart); buzz(25);
            };
            const bool adOk = rewardReady(Placement::MissionBonus) && !profile_.child();
            hudButton("Claim", {c.x + (pw - 32) / 2 - (adOk ? 112 : 56), c.y + 22}, kVolt, p.k, true, [claim] { claim(false); }, 0.75f);
            if (adOk) {
                const Icon ad = Icon::AdBadge;
                nextHitLabel_ = "x2 bonus";
                hudActionButton("x2", &ad, 0, {c.x + (pw - 32) / 2 - 40, c.y + 22}, kHot, p.k, false, [this, claim] {
                    requestReward(Placement::MissionBonus, [claim] { claim(true); });
                }, 0.7f);
            }
            primary_ = [claim] { claim(false); };
        }
        y += 96;
    }
    // time until the next set
    {
        const int64_t now = svc_.clock->now();
        const int64_t h = svc_.clock->localHour(now);
        TextStyle t = style(FontId::BodyBold, 11, kMuted, 0.14f, true);
        t.opacity = p.k;
        hudText("New missions in about " + std::to_string(std::max<int64_t>(1, 24 - h)) + "h", t, p.c.x, y + 6);
    }
}

// ---------------------------------------------------------------- daily drop (7-day ladder)
void Game::hudDailyDrop() {
    const float W = viewW_;
    const float pw = std::min(W - 24, 380.f);
    const float gap = 8, cell = (pw - 32 - 3 * gap) / 4;
    const float contentH = 2 * (cell * 1.1f) + gap + 24 + 60;
    const Panel p = hudPanel("Daily drop", dropClaimed_ >= 0 ? "Collected!" : "Your reward", pw, contentH, kGreen, true);
    const bool claimable = drop_.claimable();
    const int next = drop_.dayIndex();             // the step claimable now (or tomorrow's, once claimed)
    const int today = dropClaimed_ >= 0 ? dropClaimed_ : next;
    const int week = drop_.claims() / 7;            // which lap of the ladder (for the "claimed" ticks)
    (void)week;
    float y = p.top;
    for (int i = 0; i < 7; ++i) {
        const int row = i < 4 ? 0 : 1, col = i < 4 ? i : i - 4;
        const float rowW = row == 0 ? 4 * cell + 3 * gap : 3 * cell + 2 * gap;
        const float w = (i == 6) ? cell * 1.0f : cell;
        const glm::vec2 c{p.c.x - rowW / 2 + cell / 2 + col * (cell + gap) + (i == 6 ? 0 : 0), y + row * (cell * 1.1f + gap) + cell * 0.55f};
        const bool past = dropClaimed_ >= 0 ? i <= dropClaimed_ : i < next;
        const bool isToday = i == today;
        const Up a = up(screenT_, 0.05f + i * 0.04f);
        const float al = p.k * a.alpha;
        const float pop = isToday && dropClaimed_ >= 0 ? 1.f + 0.18f * std::max(0.f, 1.f - (screenT_ - dropT_) * 3.f) : 1.f;
        hudTile({c.x, c.y + a.dy}, glm::vec2(w, cell * 1.1f) * pop, isToday ? kGreen : past ? kMuted : kViolet, al, isToday, {});
        TextStyle d = style(FontId::BodyBold, 10, isToday ? kGreen : kMuted, 0.14f, true);
        d.opacity = al;
        hudText("Day " + std::to_string(i + 1), d, c.x, c.y + a.dy - cell * 0.5f);
        if (i == 6) hudSprite(past ? Icon::ChestOpen : Icon::ChestClosed, {c.x, c.y + a.dy}, cell * 0.72f, 0, glm::vec3(1.f), al);
        else hudCoin({c.x, c.y + a.dy}, cell * 0.44f, al * (past && !isToday ? 0.5f : 1.f), isToday ? -1.f : 0.f);
        TextStyle amt = style(FontId::Display, 12, past && !isToday ? kMuted : kGold);
        amt.opacity = al;
        hudText(grouped(tune_.dailyDrop[i]), amt, c.x, c.y + a.dy + cell * 0.26f);
        if (past && !isToday) hudSprite(Icon::Check, {c.x + w / 2 - 10, c.y + a.dy - cell * 0.45f}, 22, 0, glm::vec3(1.f), al);
    }
    y += 2 * (cell * 1.1f) + gap + 24;
    if (claimable) {
        auto claim = [this, p](bool twice) {
            const int step = drop_.claim();
            if (step < 0) return;
            dropClaimed_ = step;
            dropT_ = screenT_;
            dropCoins_ = tune_.dailyDrop[step] * (twice ? 2 : 1);
            wallet_.earn(dropCoins_, twice ? "daily_drop_x2" : "daily_drop");
            flyCoins(p.c, dropCoins_, 14);
            hudBurst(p.c, 40, {kGreen, kGold, kVolt}, 420.f, true);
            sfx(Sfx::SurgeStart); buzz(25);
            track("daily_drop", {{"step", std::to_string(step + 1)}, {"doubled", twice ? "1" : "0"}});
        };
        const bool adOk = rewardReady(Placement::DropDouble) && !profile_.child();
        if (adOk) {
            const Icon ad = Icon::AdBadge;
            nextHitLabel_ = "x2 drop";
            hudActionButton("Claim x2", &ad, 0, {p.c.x + 70, y + 26}, kHot, p.k, true, [this, claim] { requestReward(Placement::DropDouble, [claim] { claim(true); }); }, 0.9f);
            hudButton("Claim", {p.c.x - 90, y + 26}, kGreen, p.k, false, [claim] { claim(false); }, 0.9f);
        } else {
            hudButton("Claim", {p.c.x, y + 26}, kGreen, p.k, true, [claim] { claim(false); });
        }
        primary_ = [claim] { claim(false); };
    } else {
        hudButton("Done", {p.c.x, y + 26}, kNeon, p.k, false, [this] { dropClaimed_ = -1; closeScreen(); });
        primary_ = [this] { dropClaimed_ = -1; closeScreen(); };
    }
}

// ---------------------------------------------------------------- settings
void Game::hudSettings() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    std::vector<std::pair<std::string, std::function<void()>>> rows;
    rows.push_back({muted_ ? "Music: off" : "Music: on", [this] { toggleMute(); }});
    if (privacyButton_) rows.push_back({"Privacy settings", [this] { svc_.ads->showPrivacyOptions(); }});
    if (!profile_.child()) {
        const bool on = profile_.flag("notif") && svc_.notifications->permission() != NotifPermission::Denied;
        rows.push_back({on ? "Reminders: on" : "Reminders: off", [this, on] {
            if (on) { profile_.setFlag("notif", false); rescheduleReminders(); track("notif_toggle", {{"on", "0"}}); }
            else { onNotificationsAccepted(); track("notif_toggle", {{"on", "1"}}); }
        }});
    }
    if (!adPolicy_.removeAds()) rows.push_back({"Remove ads", [this] { startPurchase("remove_ads"); }});
    rows.push_back({"Restore purchases", [this] { svc_.store->restore(); showMessage("Restoring purchases…"); }});
#if !defined(CS_ENV_PROD)
    rows.push_back({"Developer", [this] { switchScreen(Screen::Dev); }});
#endif
    const float contentH = rows.size() * 58.f + 30;
    const Panel p = hudPanel("", "Settings", pw, contentH, kNeon, true);
    float y = p.top;
    for (auto& r : rows) {
        hudButton(r.first, {p.c.x, y + 24}, kNeon, p.k, false, r.second, 0.9f);
        y += 58;
    }
    TextStyle v = style(FontId::BodyBold, 11, kMuted, 0.14f, true);
    v.opacity = p.k;
#if defined(CS_ENV_PROD)
    hudText("Cuckoo Stack", v, p.c.x, y + 6);
#else
    hudText("Cuckoo Stack · staging", v, p.c.x, y + 6);
#endif
}

// ---------------------------------------------------------------- developer menu (staging builds only)
// Everything a tester needs to reach a state quickly: coins, owning / equipping any cosmetic, level, streak, daily
// drop, missions, ads, audience, and any modal. Prod builds compile it out of Settings (the screen stays unreachable).
void Game::hudDev() {
    const float W = viewW_;
    const float pw = std::min(W - 24, 400.f);
    const float inner = pw - 2 * 18;
    // ---- what this tab shows: a list of (label, state, action) buttons, two per row
    enum class St { Plain, On, Owned, Locked };
    struct B { std::string label; St st; std::function<void()> fn; };
    std::vector<B> bs;
    std::string note;
    auto cosmetics = [&](Slot slot) {
        for (const Cosmetic& c : catalog()) {
            if (c.slot != slot) continue;
            const bool eq = wallet_.equipped(slot) == c.id, own = wallet_.owns(c.id);
            bs.push_back({c.name, eq ? St::On : own ? St::Owned : St::Locked, [this, id = std::string(c.id)] {
                wallet_.grant(id); wallet_.equip(id); lockerPreview_.clear(); applyCosmetics();
                sfx(Sfx::Lay, 3); buzz(6);
            }});
        }
        bs.push_back({"Unlock all", St::Plain, [this, slot] {
            for (const Cosmetic& c : catalog()) if (c.slot == slot) wallet_.grant(c.id);
            showMessage("Unlocked");
        }});
        bs.push_back({"Lock all (every tab)", St::Plain, [this] { wallet_.debugLockAll(); applyCosmetics(); showMessage("Locked"); }});
        note = "Tap to own + equip. Green = equipped, cyan = owned.";
    };
    switch (devTab_) {
    case 0: {
        for (int n : {100, 1000, 10000, 100000})
            bs.push_back({"+" + grouped(n), St::Plain, [this, n] { wallet_.earn(n, "debug"); flyCoins({viewW_ / 2, viewH_ / 2}, n, 10); }});
        bs.push_back({"Set to 0", St::Plain, [this] { wallet_.debugSetCoins(0); coinShown_ = 0; }});
        bs.push_back({"Set to 50", St::Plain, [this] { wallet_.debugSetCoins(50); coinShown_ = 50; }});
        note = "Balance: " + grouped(wallet_.coins());
        break;
    }
    case 1: cosmetics(Slot::Outfit); break;
    case 2: cosmetics(Slot::Trail); break;
    case 3: cosmetics(Slot::Crash); break;
    default: {
        bs.push_back({"Level +1 (chest)", St::Plain, [this] { levelGained(prog_.add(prog_.xpToNext() - prog_.xp())); }});
        bs.push_back({"Level 1", St::Plain, [this] { prog_.debugSetLevel(1); }});
        bs.push_back({"Level 15", St::Plain, [this] { prog_.debugSetLevel(14); levelGained(prog_.add(prog_.xpToNext())); }});
        bs.push_back({"Streak 7 days", St::Plain, [this] { dayStreak_.debugSet(7, 0); }});
        bs.push_back({"Streak missed 1 day", St::Plain, [this] { dayStreak_.debugSet(5, 2); }});
        bs.push_back({"Streak reset", St::Plain, [this] { dayStreak_.debugSet(0, 0); }});
        bs.push_back({"Daily drop again", St::Plain, [this] { drop_.debugSet(drop_.claims(), true); }});
        bs.push_back({"Daily drop day 7", St::Plain, [this] { drop_.debugSet(6, true); }});
        bs.push_back({"Complete missions", St::Plain, [this] {
            for (const Mission& m : missions_.list())
                missionProgress(m.kind == MissionKind::Distance ? missions_.recordDistance(m.target) : missions_.record(m.kind, m.target));
        }});
        bs.push_back({"New missions", St::Plain, [this] { for (int i = 0; i < 3; ++i) missions_.reroll(i); }});
        bs.push_back({boost_ == Boost::Surge ? "Surge start: armed" : "Free Surge Start", boost_ == Boost::Surge ? St::On : St::Plain,
                      [this] { boost_ = Boost::Surge; }});
        bs.push_back({adPolicy_.removeAds() ? "Remove ads: on" : "Remove ads: off", adPolicy_.removeAds() ? St::On : St::Plain,
                      [this] { adPolicy_.setRemoveAds(!adPolicy_.removeAds()); }});
        bs.push_back({"Reset ad caps", St::Plain, [this] { adPolicy_.debugReset(false); showMessage("Ad caps cleared"); }});
        bs.push_back({"Ad on next game over", St::Plain, [this] { adPolicy_.debugReset(true); showMessage("Next game over shows an ad"); }});
        bs.push_back({profile_.child() ? "Audience: under 13" : "Audience: 13+", profile_.child() ? St::On : St::Plain, [this] {
            const bool child = !profile_.child();
            profile_.setAudience(child ? Audience::Child : Audience::Teen);
            svc_.ads->setAudience(child);
        }});
        const std::pair<const char*, Screen> screens[] = {{"Show level-up", Screen::LevelUp}, {"Show starter offer", Screen::Starter},
                                                          {"Show streak save", Screen::StreakSave}, {"Show daily drop", Screen::DailyDrop},
                                                          {"Show parental gate", Screen::ParentalGate}, {"Show age gate", Screen::AgeGate}};
        for (const auto& [label, s] : screens) bs.push_back({label, St::Plain, [this, s = s] { switchScreen(s); }});
        note = "Level " + std::to_string(prog_.level()) + " · streak " + std::to_string(dayStreak_.count()) + " · drop day " +
               std::to_string(drop_.dayIndex() + 1) + " · runs " + std::to_string(adPolicy_.lifetimeRuns());
        break;
    }
    }
    // ---- layout
    constexpr float tabH = 34, rowH = 40, gap = 8;
    constexpr int kRows = 11; // the Game tab's; every tab gets the same height so the tabs never move under a finger
    const float contentH = tabH + 14 + 22 + kRows * (rowH + gap);
    const Panel p = hudPanel("Staging only", "Developer", pw, contentH, kVolt, true);
    float y = p.top;
    const char* tabs[] = {"Coins", "Outfits", "Trails", "Crash", "Game"};
    const float tw = inner / 5;
    for (int i = 0; i < 5; ++i) {
        const glm::vec2 c{p.c.x - inner / 2 + tw * (i + 0.5f), y + tabH / 2};
        const bool on = devTab_ == i;
        hudRect(c, {tw - 4, tabH}, on ? kVolt : kDeep, (on ? 0.95f : 0.8f) * p.k, Shape::PillOutline, 1.f);
        if (!on) hudRect(c, {tw - 4, tabH}, kVolt, 0.4f * p.k, Shape::PillOutline, 1.2f / tabH);
        TextStyle t = style(FontId::BodyBold, 11, on ? css("#1a0420") : kVolt, 0.08f, true);
        t.opacity = p.k;
        hudText(tabs[i], t, c.x, c.y - text_->lineBox(t) / 2);
        nextHitLabel_ = std::string("dev:") + tabs[i];
        uiHit(c, {tw, tabH + 6}, [this, i] { devTab_ = i; });
    }
    y += tabH + 14;
    TextStyle n = style(FontId::Body, 12, kMuted, 0.02f);
    n.opacity = p.k;
    hudText(note, n, p.c.x, y);
    y += 22;
    const float bw = (inner - gap) / 2;
    for (size_t i = 0; i < bs.size(); ++i) {
        const glm::vec2 c{p.c.x + (i % 2 ? 1.f : -1.f) * (bw / 2 + gap / 2), y + (i / 2) * (rowH + gap) + rowH / 2};
        const glm::vec3 col = bs[i].st == St::On ? kGreen : bs[i].st == St::Owned ? kNeon : bs[i].st == St::Locked ? kMuted : kVolt;
        hudRect(c, {bw, rowH}, kDeep, 0.9f * p.k, Shape::PillOutline, 1.f);
        hudRect(c, {bw, rowH}, col, (bs[i].st == St::On ? 1.f : 0.6f) * p.k, Shape::PillOutline, (bs[i].st == St::On ? 2.2f : 1.2f) / rowH);
        TextStyle t = style(FontId::BodyBold, 12, bs[i].st == St::Locked ? kMuted : kText, 0.04f);
        t.opacity = p.k;
        hudText(bs[i].label, t, c.x, c.y - text_->lineBox(t) / 2);
        nextHitLabel_ = "dev:" + bs[i].label;
        uiHit(c, {bw, rowH}, bs[i].fn);
    }
}

// ---------------------------------------------------------------- level up: a chest to crack open
void Game::hudLevelUp() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    const float contentH = 190 + 70;
    const Panel p = hudPanel("Level up!", "Level " + std::to_string(prog_.level()), pw, contentH, kVolt, chestOpened_);
    const float y = p.top;
    const glm::vec2 c{p.c.x, y + 90};
    // rays behind
    for (int i = 0; i < 10; ++i) {
        const float a = time_ * 0.4f + i * kPi / 5;
        hudRect(c + glm::vec2(std::cos(a), std::sin(a)) * 70.f, {120, 10}, kVolt, 0.12f * p.k, Shape::Streak, 0, a);
    }
    hudRect(c, glm::vec2(220), kVolt, 0.35f * p.k, Shape::RadialGlow);
    const float openT = chestOpened_ ? screenT_ - chestT_ : -1.f;
    if (!chestOpened_) {
        const float shake = std::sin(time_ * 30) * 2.5f * (0.5f + 0.5f * std::sin(time_ * 2.f));
        hudSprite(Icon::ChestClosed, c + glm::vec2(shake, 0), 150 * (1.f + 0.04f * std::sin(time_ * 5)), shake * 0.01f, glm::vec3(1.f), p.k);
        auto open = [this, c] {
            chestOpened_ = true;
            chestT_ = screenT_;
            wallet_.earn(chestCoins_, "level_chest");
            sfx(Sfx::SurgeStart); buzz(30);
            flashScreen(srgbColor("#f4ff5a"), 0.25f);
            hudBurst(c, 50, {kVolt, kGold, kHot, kNeon}, 520.f, true);
            flyCoins(c, chestCoins_, 14);
        };
        hudButton("Open", {p.c.x, y + 222}, kVolt, p.k, true, open);
        primary_ = open;
    } else {
        const bool crack = openT < 0.18f;
        hudSprite(crack ? Icon::ChestCrack : Icon::ChestOpen, c, 150 * (1.f + 0.2f * std::max(0.f, 1.f - openT * 3)), 0, glm::vec3(1.f), p.k);
        hudCoinAmount(chestCoins_ * (chestDoubled_ ? 2 : 1), {c.x, c.y + 84}, 26, p.k * seg(openT, 0.1f, 0.4f), TextAlign::Center, true);
        const bool adOk = !chestDoubled_ && rewardReady(Placement::ChestDouble) && !profile_.child();
        auto done = [this] { levelUps_ = 0; closeScreen(); };
        if (adOk) {
            const Icon ad = Icon::AdBadge;
            nextHitLabel_ = "x2 chest";
            hudActionButton("x2", &ad, 0, {p.c.x + 70, y + 222}, kHot, p.k, true, [this, c] {
                requestReward(Placement::ChestDouble, [this, c] {
                    chestDoubled_ = true;
                    wallet_.earn(chestCoins_, "level_chest_x2");
                    flyCoins(c, chestCoins_, 12);
                    hudBurst(c, 30, {kGold, kVolt}, 380.f, true);
                });
            });
            hudButton("Collect", {p.c.x - 70, y + 222}, kVolt, p.k, false, done, 0.9f);
        } else hudButton("Collect", {p.c.x, y + 222}, kVolt, p.k, true, done);
        primary_ = done;
    }
}

// ---------------------------------------------------------------- streak save (13+ only; offered once)
void Game::hudStreakSave() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    TextStyle body = style(FontId::Body, 15, kText, 0.f, false, 1.4f);
    const int n = dayStreak_.count();
    const auto lines = text_->wrap("You missed a day. Keep your " + std::to_string(n) + "-day streak going?", body, pw - 2 * kPad);
    const float contentH = 130 + text_->lineBox(body) * lines.size() + 14 + 62 + 62 + 40;
    const Panel p = hudPanel("Streak", std::to_string(n) + " days", pw, contentH, css("#ff6a2b"), false);
    float y = p.top;
    const glm::vec2 c{p.c.x, y + 62};
    hudRect(c, glm::vec2(180), css("#ff6a2b"), 0.35f * p.k, Shape::RadialGlow);
    hudSprite(Icon::Flame, c, 120 * (1.f + 0.04f * std::sin(time_ * 7)), 0, glm::vec3(1.f), p.k * (0.6f + 0.4f * std::abs(std::sin(time_ * 3))), int(time_ * 9) % kFlameFrames);
    y += 130;
    body.opacity = 0.85f * p.k;
    for (const auto& l : lines) { hudText(l, body, p.c.x, y); y += text_->lineBox(body); }
    y += 14;
    auto keep = [this] {
        dayStreak_.keep();
        toast("Streak saved!", Icon::Flame);
        hudBurst({viewW_ / 2, viewH_ * 0.4f}, 30, {css("#ff6a2b"), kVolt, kHot}, 380.f);
        closeScreen();
    };
    const Icon ad = Icon::AdBadge;
    nextHitLabel_ = "Keep";
    hudActionButton("Keep it", &ad, 0, {p.c.x, y + 24}, kHot, rewardReady(Placement::StreakSave) ? p.k : 0.45f * p.k, true,
                    [this, keep] { requestReward(Placement::StreakSave, keep); });
    y += 62;
    hudActionButton("Repair", nullptr, tune_.streakRepairCost, {p.c.x, y + 24}, kGold, p.k, false, [this, keep] {
        if (wallet_.spend(tune_.streakRepairCost, "streak_repair")) keep(); else showMessage("Not enough coins");
    });
    y += 62;
    hudTextButton("Start over", {p.c.x, y + 18}, p.k, [this] { closeScreen(); });
}

// ---------------------------------------------------------------- parental gate (purchases by under-13s)
void Game::hudParentalGate() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    TextStyle body = style(FontId::Body, 15, kText, 0.f, false, 1.4f);
    const auto lines = text_->wrap("Ask a grown-up to answer this before buying.", body, pw - 2 * kPad);
    const float contentH = text_->lineBox(body) * lines.size() + 16 + 60 + 2 * 62 + 30;
    const Panel p = hudPanel("For grown-ups", "Parents only", pw, contentH, kViolet, true);
    float y = p.top;
    body.opacity = 0.85f * p.k;
    for (const auto& l : lines) { hudText(l, body, p.c.x, y); y += text_->lineBox(body); }
    y += 16;
    TextStyle q = style(FontId::Display, 30, kText);
    q.opacity = p.k;
    hudText(std::to_string(gateA_) + " × " + std::to_string(gateB_) + " = ?", q, p.c.x, y);
    y += 60;
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 c{p.c.x + (i % 2 ? 1.f : -1.f) * 70, y + (i / 2) * 62 + 24};
        nextHitLabel_ = "ans" + std::to_string(i);
        hudButton(std::to_string(gateChoices_[size_t(i)]), c, kViolet, p.k, false, [this, i] {
            const bool ok = i == gateAnswer_;
            auto then = std::move(gateThen_);
            gateThen_ = nullptr;
            closeScreen();
            if (ok && then) then();
            else showMessage("That's not right");
        });
    }
}

// ---------------------------------------------------------------- permission primers (Phase 2/3 fill these in)
void Game::hudPrimer() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    const bool notif = screen_ == Screen::NotifPrimer;
    TextStyle body = style(FontId::Body, 15, kText, 0.f, false, 1.4f);
    const auto lines = text_->wrap(notif ? "Get a heads-up when a new course drops and when your streak needs you. One a day, never at night."
                                         : "Record your runs so you can share your best crashes with friends.",
                                   body, pw - 2 * kPad);
    const float contentH = 110 + text_->lineBox(body) * lines.size() + 14 + 62 + 36;
    const Panel p = hudPanel(notif ? "Stay in the loop" : "Replays", notif ? "Course alerts" : "Share your runs", pw, contentH, kNeon, false);
    float y = p.top;
    hudSprite(notif ? Icon::Bell : Icon::Replay, {p.c.x, y + 50}, 100 * (1.f + 0.05f * std::sin(time_ * 4)), 0.1f * std::sin(time_ * 3), glm::vec3(1.f), p.k);
    y += 110;
    body.opacity = 0.85f * p.k;
    for (const auto& l : lines) { hudText(l, body, p.c.x, y); y += text_->lineBox(body); }
    y += 14;
    auto yes = [this, notif] { primerAccepted(notif); closeScreen(); };
    auto no = [this, notif] { primerDeclined(notif); closeScreen(); };
    hudButton(notif ? "Turn on" : "Record my runs", {p.c.x, y + 24}, kHot, p.k, true, yes);
    primary_ = yes;
    hudTextButton("Not now", {p.c.x, y + 62 + 12}, p.k, no);
}

// ---------------------------------------------------------------- starter pack offer
void Game::hudStarter() {
    const float W = viewW_;
    const float pw = std::min(W - 32, 340.f);
    const float contentH = 150 + 30 + 40 + 62 + 40;
    const Panel p = hudPanel("One-time offer", "Starter pack", pw, contentH, kViolet, true);
    float y = p.top;
    const glm::vec2 c{p.c.x, y + 70};
    hudRect(c, glm::vec2(240), kViolet, 0.4f * p.k, Shape::RadialGlow);
    hudSprite(Icon::ChestOpen, c, 150 * (1.f + 0.03f * std::sin(time_ * 4)), 0, glm::vec3(1.f), p.k);
    y += 150;
    TextStyle b = style(FontId::BodyBold, 14, kText, 0.08f, true);
    b.opacity = p.k;
    hudText("Glitch Hen outfit", b, p.c.x, y);
    y += 30;
    hudCoinAmount(tune_.starterCoins, {p.c.x, y + 12}, 26, p.k, TextAlign::Center, true);
    y += 40;
    std::string price = findProduct("starter_pack")->fallbackPrice;
    for (const Product& pr : products_) if (pr.id == "starter_pack" && !pr.price.empty()) price = pr.price;
    hudButton(price, {p.c.x, y + 24}, kViolet, p.k, true, [this] { startPurchase("starter_pack"); });
    primary_ = [this] { startPurchase("starter_pack"); };
    hudTextButton("Maybe later", {p.c.x, y + 62 + 12}, p.k, [this] { closeScreen(); });
}

// ================================================================ feedback: flying coins, toasts, sparks

void Game::flyCoins(glm::vec2 from, int amount, int pieces) {
    if (amount <= 0) return;
    pieces = std::clamp(pieces, 1, int(flyCoins_.size()));
    const int each = amount / pieces;
    int extra = amount - each * pieces;
    int made = 0;
    for (FlyCoin& f : flyCoins_) {
        if (f.live) continue;
        if (made >= pieces) break;
        f.live = true;
        f.from = from + glm::vec2(rng_.range(-26.f, 26.f), rng_.range(-14.f, 14.f));
        f.ctrl = f.from + glm::vec2(rng_.range(-110.f, 110.f), rng_.range(-200.f, -60.f));
        f.t = 0;
        f.delay = made * 0.05f;
        f.value = each + (extra > 0 ? 1 : 0);
        if (extra > 0) --extra;
        ++made;
    }
    // any coins that didn't get a sprite (pool full) land on the counter straight away
    int flying = 0;
    for (const FlyCoin& f : flyCoins_) if (f.live) flying += f.value;
    (void)flying;
}

void Game::toast(std::string text, Icon icon, int coins) {
    if (toasts_.size() >= 4) toasts_.pop_front();
    toasts_.push_back({std::move(text), icon, coins, 0.f});
}

void Game::hudBurst(glm::vec2 at, int n, std::initializer_list<glm::vec3> cols, float speed, bool coins) {
    const glm::vec3* c = cols.begin();
    const int nc = int(cols.size());
    int made = 0;
    for (HudSpark& s : sparks_) {
        if (s.life > 0) continue;
        if (made >= n) break;
        const float a = rng_.range(0.f, 2 * kPi), sp = speed * rng_.range(0.35f, 1.f);
        s.p = at;
        s.v = {std::cos(a) * sp, std::sin(a) * sp - speed * 0.3f};
        s.rot = rng_.range(0.f, 6.28f); s.spin = rng_.range(-12.f, 12.f);
        s.maxLife = s.life = rng_.range(0.5f, 0.95f);
        s.coin = coins && made % 3 == 0;
        s.size = s.coin ? rng_.range(14.f, 22.f) : rng_.range(3.f, 7.f);
        s.color = c[made % nc];
        ++made;
    }
}

void Game::updateHudFx(float rdt) {
    for (HudSpark& s : sparks_) {
        if (s.life <= 0) continue;
        s.life -= rdt;
        s.v.y += 900.f * rdt;
        s.v *= 1.f - std::min(1.f, rdt * 1.4f);
        s.p += s.v * rdt;
        s.rot += s.spin * rdt;
    }
    int inFlight = 0;
    for (FlyCoin& f : flyCoins_) {
        if (!f.live) continue;
        f.t += rdt;
        if (f.t - f.delay >= 0.68f) {
            f.live = false;
            coinPulse_ = 1.f;
            if (rng_.next01() < 0.5f) sfx(Sfx::Corn);
            buzz(4);
            hudBurst(coinCounterPos_, 4, {kGold, css("#ffffff")}, 160.f);
        } else inFlight += f.value;
    }
    coinPulse_ = std::max(0.f, coinPulse_ - rdt * 4.f);
    const float target = float(wallet_.coins() - inFlight);
    if (coinShown_ < target) coinShown_ = std::min(target, coinShown_ + std::max(1.f, (target - coinShown_) * rdt * 9.f));
    else if (coinShown_ > target) coinShown_ = std::max(target, coinShown_ - std::max(1.f, (coinShown_ - target) * rdt * 12.f));
    if (!toasts_.empty()) {
        toasts_.front().t += rdt;
        if (toasts_.front().t > 2.6f) toasts_.pop_front();
    }
}

void Game::hudCoinFx() {
    for (const HudSpark& s : sparks_) {
        if (s.life <= 0) continue;
        const float k = s.life / s.maxLife;
        if (s.coin) hudCoin(s.p, s.size, k, std::fmod(s.rot * 0.2f, 1.f));
        else hudRect(s.p, glm::vec2(s.size, s.size * 0.45f), s.color, k, Shape::Solid, 0, s.rot);
    }
    for (const FlyCoin& f : flyCoins_) {
        if (!f.live || f.t < f.delay) continue;
        const float u = easeOut(clampf((f.t - f.delay) / 0.68f, 0.f, 1.f) * 0.9f + 0.1f * clampf((f.t - f.delay) / 0.68f, 0.f, 1.f));
        const float v = clampf((f.t - f.delay) / 0.68f, 0.f, 1.f);
        const float e = v * v * (3 - 2 * v);
        (void)u;
        const glm::vec2 a = glm::mix(f.from, f.ctrl, e), b = glm::mix(f.ctrl, coinCounterPos_, e);
        const glm::vec2 p = glm::mix(a, b, e);
        const float size = glm::mix(30.f, 22.f, e) * (1.f + 0.3f * std::sin(v * kPi));
        hudRect(p, glm::vec2(size * 1.8f), kGold, 0.25f * (1.f - v * 0.5f), Shape::RadialGlow);
        hudCoin(p, size, 1.f, std::fmod(f.t * 1.8f, 1.f));
    }
}

void Game::hudToasts() {
    if (toasts_.empty()) return;
    const Toast& t = toasts_.front();
    const float in = easeOutBack(t.t / 0.35f, 1.6f), out = 1.f - seg(t.t, 2.2f, 2.6f);
    const float a = std::min(1.f, t.t / 0.2f) * out;
    const float y0 = state_ == State::Title ? safeTop_ + 92 : safeTop_ + 150; // below the score + sector line in a run
    const float y = y0 - 30.f * (1.f - in);
    TextStyle s = style(FontId::BodyBold, 13.5f, kText, 0.04f);
    s.opacity = a;
    const float tw = text_->measure(t.text, s);
    const float cw = t.coins > 0 ? coinAmountWidth(t.coins, 18, true) + 12 : 0;
    const float w = 36 + 8 + tw + cw + 26, h = 46;
    const glm::vec2 c{viewW_ / 2, y};
    hudRect(c, {w + 12, h + 12}, kGreen, 0.12f * a, Shape::PillOutline, 6.f / (h + 12));
    hudRect(c, {w, h}, kDeep, 0.93f * a, Shape::PillOutline, 1.f);
    hudRect(c, {w, h}, kGreen, 0.85f * a, Shape::PillOutline, 1.6f / h);
    float x = c.x - w / 2 + 14;
    hudSprite(t.icon, {x + 16, c.y}, 40 * (1.f + 0.15f * std::max(0.f, 1.f - t.t * 3)), 0, glm::vec3(1.f), a);
    x += 36 + 8;
    hudText(t.text, s, x, c.y - text_->lineBox(s) / 2, TextAlign::Left);
    x += tw + 12;
    if (t.coins > 0) hudCoinAmount(t.coins, {x, c.y}, 18, a, TextAlign::Left, true);
}

// ================================================================ session screens + primers

void Game::debugOpenScreen(const std::string& name) {
    static const std::pair<const char*, Screen> kNames[] = {
        {"shop", Screen::Shop}, {"locker", Screen::Locker}, {"missions", Screen::Missions}, {"drop", Screen::DailyDrop},
        {"settings", Screen::Settings}, {"dev", Screen::Dev}, {"levelup", Screen::LevelUp}, {"streak", Screen::StreakSave}, {"gate", Screen::ParentalGate},
        {"notif", Screen::NotifPrimer}, {"replay", Screen::ReplayPrimer}, {"starter", Screen::Starter}, {"continue", Screen::Continue},
        {"age", Screen::AgeGate}};
    for (const auto& [n, sc] : kNames)
        if (name == n) {
            screenQueue_.clear();
            if (sc == Screen::DailyDrop) { profile_.setCounter("dbg", 1); }
            if (sc == Screen::LevelUp) { chestCoins_ = tune_.levelChestCoins; chestOpened_ = false; }
            if (sc == Screen::ParentalGate) { parentalGate([] {}); return; }
            if (sc == Screen::Continue) { continueT_ = 1.2f; }
            openScreen(sc);
            return;
        }
}


void Game::queueSessionScreens() {
    if (profile_.audience() == Audience::Unknown) return;
    if (dayStreak_.status() == Streak::Status::Saveable && !profile_.child() && profile_.counter("streak_prompt_day") != int(civilDay(svc_.clock->today())))
        queueScreen(Screen::StreakSave);
    if (drop_.claimable()) queueScreen(Screen::DailyDrop);
    if (profile_.sessions() >= 3 && !wallet_.owns("hen_glitch") && profile_.counter("starter_seen") < 2 && profile_.sessions() % 3 == 0)
        queueScreen(Screen::Starter);
}

void Game::primerAccepted(bool notif) {
    track(notif ? "notif_primer" : "replay_primer", {{"accepted", "1"}});
    profile_.setFlag(notif ? "notif_asked" : "replay_asked", true);
    if (notif) onNotificationsAccepted();
    else { profile_.setFlag("replays", true); svc_.replay->setEnabled(true); }
}

// The results' Share button: the replay when there is one, otherwise turn replays on (they record from the next run).
void Game::shareReplay() {
    if (profile_.child()) return;
    const ReplayState rs = svc_.replay->state();
    // the link is a challenge: friends who open it race this distance on the same course today
    const std::string caption = challenge_ && challenge_->beaten
        ? "Beat your " + std::to_string(challenge_->meters) + " m with " + std::to_string(finalDist_) + " m on Cuckoo Stack. Your move:"
        : "I hit " + std::to_string(finalDist_) + " m on today\u2019s Cuckoo Stack course. Can you beat it?";
    track("replay_share_tap", {{"distance", std::to_string(finalDist_)}, {"video", rs == ReplayState::Ready ? "1" : "0"}});
    if (svc_.backend->nudgesAvailable()) {
        svc_.backend->createChallenge(svc_.clock->today(), finalDist_);
        pendingShare_ = PendingShare{caption, finalDist_, 1.5f};
    } else {
        doShare(caption, finalDist_, "");
    }
    if (rs == ReplayState::Off) { // turn replays on so the next run comes with a video
        profile_.setFlag("replays", true);
        svc_.replay->setEnabled(true);
        toast("Replays on: your next run comes with video", Icon::Replay);
        track("replay_enabled", {{"from", "results"}});
    }
}

void Game::primerDeclined(bool notif) {
    track(notif ? "notif_primer" : "replay_primer", {{"accepted", "0"}});
    profile_.setFlag(notif ? "notif_asked" : "replay_asked", true);
}

} // namespace cs
