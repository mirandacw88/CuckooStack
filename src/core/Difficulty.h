// Difficulty curve and the invisible adaptive difficulty (DDA) (first ported from the web build; no longer kept in step).
// The daily course layout never changes; DDA only scales run speed and egg refill.
#pragma once

#include "Services.h"

#include <string>
#include <vector>

namespace cs {

// (Doubles: from the web port; the native game no longer needs to match it.)
struct Curve {
    double D, speed, regen;
    int maxUp, cap, minLen, maxLen;
    double drop, ceil;
    int gap;
    double corn;
};
// S-curve over the first 350 m, then a flat plateau at full difficulty.
Curve curve(double distance);

class Dda {
public:
    explicit Dda(IStorage* storage);
    float level(float x) const;
    void newDay(const std::string& day);
    float speedMul(float x) const;
    float regenMul(float x) const;
    void record(float distance, float pbBefore);

    float assist() const { return assist_; }
    // extra assist on top of the adaptive one (new players' first runs); not saved
    void setBoost(float b) { boost_ = b; }
    float zoneCenter() const { return zoneC_; }
    float zoneStrength() const { return zoneS_; }

private:
    void save() const;
    IStorage* storage_;
    float skill_ = 0.f, assist_ = 0.12f, zoneC_ = 0.f, zoneS_ = 0.f, boost_ = 0.f;
    int runs_ = 0;
    std::string day_;
    std::vector<int> deaths_;
};

} // namespace cs
