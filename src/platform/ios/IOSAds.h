// iOS AdMob rewarded video behind Google UMP consent and App Tracking Transparency (lives refill, src/core/Lives.h).
#pragma once

#include "../../core/Services.h"

namespace cs {

class IOSAds final : public IAds {
public:
    IOSAds();
    ~IOSAds() override;
    // Consent (UMP form where required) -> ATT prompt -> SDK start -> preload. Call once the splash is gone.
    void start();

    RewardedState rewardedState() override;
    bool showRewarded() override;
    bool consumeReward() override;
    bool privacyOptionsRequired() override;
    void showPrivacyOptions() override;

private:
    void* impl_; // CSAdsImpl*
};

} // namespace cs
