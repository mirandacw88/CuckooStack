#include "Difficulty.h"
#include "Math.h"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace cs {

Curve curve(double d) {
    constexpr double kPlateau = 350.0;
    const double t = std::min(1.0, std::max(0.0, d) / kPlateau);
    const double D = t * t * (3 - 2 * t);
    auto mix = [D](double a, double b) { return a + (b - a) * D; };
    return {
        D,
        mix(8.0, 12.4),             // speed, m/s
        mix(0.5, 0.4),              // seconds per egg: faster at full speed, so eggs per metre stay near level 1's (was 0.92: one egg every 11 m, levels 7+ unbeatable)
        jsRound(mix(1.4, 4.4)),     // tallest single step up, in eggs
        jsRound(mix(4, 8)),         // tallest wall
        jsRound(mix(7, 3)), jsRound(mix(10, 5)),
        mix(0.5, 0.26),             // chance the next segment steps down
        mix(0.55, 0.7),             // chance a rising wall has an overhead barrier
        jsRound(mix(4.4, 3)),       // clearance between wall top and barrier, in eggs
        mix(0.55, 0.32),
    };
}

// Persisted as "skill assist runs day zoneC zoneS d0,d1,..." under cluckstack-dda.
Dda::Dda(IStorage* storage) : storage_(storage) {
    if (!storage_) return;
    const auto raw = storage_->get("cluckstack-dda");
    if (!raw) return;
    std::istringstream in(*raw);
    std::string deaths;
    float skill = 0.f, assist = 0.f, zoneC = 0.f, zoneS = 0.f;
    int runs = 0;
    std::string day;
    // a damaged save (hand-edited, cut short) starts over instead of crashing the game at every launch
    if (!(in >> skill >> assist >> runs >> day >> zoneC >> zoneS)) return;
    in >> deaths;
    if (!std::isfinite(skill) || !std::isfinite(assist) || !std::isfinite(zoneC) || !std::isfinite(zoneS)) return;
    skill_ = std::max(0.f, skill); assist_ = clampf(assist, -0.35f, 0.55f); runs_ = std::max(0, runs);
    day_ = day == "-" ? std::string() : day;
    zoneC_ = zoneC; zoneS_ = clampf(zoneS, 0.f, 1.f);
    std::istringstream d(deaths);
    for (std::string tok; std::getline(d, tok, ',');) {
        char* end = nullptr;
        const long v = std::strtol(tok.c_str(), &end, 10);
        if (!tok.empty() && end && *end == '\0' && v >= 0 && v < 1000000) deaths_.push_back(int(v));
    }
    if (deaths_.size() > 8) deaths_.erase(deaths_.begin(), deaths_.end() - 8);
}

void Dda::save() const {
    if (!storage_) return;
    std::ostringstream out;
    out << skill_ << ' ' << assist_ << ' ' << runs_ << ' ' << (day_.empty() ? "-" : day_) << ' ' << zoneC_ << ' ' << zoneS_ << ' ';
    for (size_t i = 0; i < deaths_.size(); ++i) out << (i ? "," : "") << deaths_[i];
    if (deaths_.empty()) out << ',';
    storage_->set("cluckstack-dda", out.str());
}

float Dda::level(float x) const {
    float a = assist_ + boost_;
    if (zoneS_ > 0.f) {
        const float d = x - zoneC_, sigma = d < 0 ? 50.f : 30.f;
        a += zoneS_ * 0.4f * std::exp(-(d * d) / (2.f * sigma * sigma));
    }
    return clampf(a, -0.35f, 0.8f);
}

void Dda::newDay(const std::string& day) {
    if (day_ == day) return;
    day_ = day; deaths_.clear(); zoneC_ = 0.f; zoneS_ = 0.f;
    save();
}

// assist > 0 slows the run and speeds refill; assist < 0 does the opposite (capped gently)
float Dda::speedMul(float x) const { const float a = level(x); return a >= 0 ? 1.f - 0.2f * a : 1.f - 0.24f * a; }
float Dda::regenMul(float x) const { const float a = level(x); return a >= 0 ? 1.f - 0.35f * a : 1.f - 0.3f * a; }

void Dda::record(float d, float pbBefore) {
    runs_++;
    const float ref = skill_ > 0 ? skill_ : d, ratio = d / std::max(25.f, ref);
    if (pbBefore > 0 && d > pbBefore + 4) assist_ -= 0.05f;   // breakthrough: tighten a little
    else if (ratio < 0.6f) assist_ += 0.08f;                  // much worse than usual
    else if (ratio < 0.9f) assist_ += 0.04f;
    else if (ratio > 1.2f) assist_ -= 0.04f;                  // much better than usual
    else assist_ += 0.025f;                                   // stuck near usual: ease toward the next breakthrough
    assist_ = clampf(assist_, -0.35f, 0.55f);
    skill_ = skill_ > 0 ? skill_ + (d - skill_) * 0.35f : d;
    deaths_.push_back(jsRound(d));
    if (deaths_.size() > 8) deaths_.erase(deaths_.begin());
    int near = 0;
    for (size_t i = deaths_.size() > 5 ? deaths_.size() - 5 : 0; i < deaths_.size(); ++i)
        if (std::abs(float(deaths_[i]) - d) < 20.f) near++;
    if (near >= 3) {
        const float keep = std::abs(zoneC_ - d) < 30.f ? zoneS_ : 0.f;
        zoneC_ = float(jsRound(d)); zoneS_ = clampf(keep + 0.34f, 0.f, 1.f);
    } else if (zoneS_ > 0.f && d > zoneC_ + 30.f) {
        zoneS_ = zoneS_ * 0.4f < 0.05f ? 0.f : zoneS_ * 0.4f;
    }
    save();
}

} // namespace cs
