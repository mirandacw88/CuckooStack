#include "Difficulty.h"
#include "Math.h"

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
        mix(0.5, 0.92),             // seconds per egg
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
    in >> skill_ >> assist_ >> runs_ >> day_ >> zoneC_ >> zoneS_ >> deaths;
    if (day_ == "-") day_.clear();
    std::istringstream d(deaths);
    for (std::string tok; std::getline(d, tok, ',');) if (!tok.empty()) deaths_.push_back(std::stoi(tok));
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
    float a = assist_;
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
