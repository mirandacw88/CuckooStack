// iOS AdMob behind Google UMP consent and App Tracking Transparency: rewarded video (the opt-in rewards in
// src/core/Monetization.h) and the interstitial between runs. The game decides when; this only loads and shows.
#pragma once

#include "../../core/Services.h"

namespace cs {

class IOSAds final : public IAds {
public:
    IOSAds();
    ~IOSAds() override;
    // The splash is gone. Ads begin once the player's age group is known too (setAudience, from the game):
    // consent (UMP form where required) -> ATT prompt (13+ only) -> SDK start -> preload.
    void start();

    RewardedState rewardedState() override;
    bool showRewarded(const char* placement) override;
    bool consumeReward() override;
    bool privacyOptionsRequired() override;
    void showPrivacyOptions() override;
    bool interstitialReady() override;
    bool showInterstitial() override;
    bool adShowing() override;
    void setAudience(bool child) override;

private:
    void* impl_; // CSAdsImpl*
};

} // namespace cs
