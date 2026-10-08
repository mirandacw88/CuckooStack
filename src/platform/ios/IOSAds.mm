#include "IOSAds.h"
#include "../../core/Log.h"

#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <GoogleMobileAds/GoogleMobileAds.h>
#import <UIKit/UIKit.h>
#import <UserMessagingPlatform/UserMessagingPlatform.h>

#include <algorithm>

// From config/ads.env via CMake: ADMOB_IOS_REWARDED_TEST in Debug, ADMOB_IOS_REWARDED_PROD in Release.
#ifndef CS_ADMOB_IOS_REWARDED
#error "CS_ADMOB_IOS_REWARDED must come from config/ads.env (see CMakeLists.txt)"
#endif

namespace {
NSString* rewardedUnitId() { return @CS_ADMOB_IOS_REWARDED; }
const double kRetrySeconds[] = {5, 15, 30, 60};

UIViewController* topViewController() {
    UIViewController* vc = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:UIWindowScene.class])
            for (UIWindow* w in ((UIWindowScene*)scene).windows)
                if (w.isKeyWindow) vc = w.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}
} // namespace

@interface CSAdsImpl : NSObject <GADFullScreenContentDelegate>
@property(nonatomic, strong) GADRewardedAd* rewarded;
@property(nonatomic) BOOL loading, sdkStarted, rewardEarned;
@property(nonatomic) int failures;
@property(nonatomic) cs::RewardedState state;
@end

@implementation CSAdsImpl

- (void)start {
    UMPRequestParameters* params = [UMPRequestParameters new];
#ifndef NDEBUG
    // debug: `--ump-eea` makes the simulator behave like a device in the EEA (consent form + privacy settings)
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--ump-eea"]) {
        UMPDebugSettings* debug = [UMPDebugSettings new];
        debug.geography = UMPDebugGeographyEEA;
        params.debugSettings = debug;
    }
#endif
    [UMPConsentInformation.sharedInstance requestConsentInfoUpdateWithParameters:params completionHandler:^(NSError* error) {
        if (error) CS_LOGW("Ads: consent update failed: %s", error.localizedDescription.UTF8String);
        [UMPConsentForm loadAndPresentIfRequiredFromViewController:topViewController() completionHandler:^(NSError* formError) {
            if (formError) CS_LOGW("Ads: consent form: %s", formError.localizedDescription.UTF8String);
            [self requestTrackingThenStart];
        }];
    }];
    // returning users: consent from the last session applies immediately
    if (UMPConsentInformation.sharedInstance.canRequestAds) [self startSdk];
}

// App Tracking Transparency after UMP (Google's recommended order); the SDK reads the result itself
- (void)requestTrackingThenStart {
    if (@available(iOS 14.5, *)) {
        if (ATTrackingManager.trackingAuthorizationStatus == ATTrackingManagerAuthorizationStatusNotDetermined) {
            [ATTrackingManager requestTrackingAuthorizationWithCompletionHandler:^(ATTrackingManagerAuthorizationStatus) {
                dispatch_async(dispatch_get_main_queue(), ^{ [self startSdk]; });
            }];
            return;
        }
    }
    [self startSdk];
}

- (void)startSdk {
    if (!UMPConsentInformation.sharedInstance.canRequestAds) { self.state = cs::RewardedState::Unavailable; return; }
    if (self.sdkStarted) return;
    self.sdkStarted = YES;
    [GADMobileAds.sharedInstance startWithCompletionHandler:^(GADInitializationStatus*) {
        CS_LOGI("Ads: SDK ready (consent %ld)", (long)UMPConsentInformation.sharedInstance.consentStatus);
        dispatch_async(dispatch_get_main_queue(), ^{ [self load]; });
    }];
}

- (void)load {
    if (self.rewarded || self.loading) return;
    if (!UMPConsentInformation.sharedInstance.canRequestAds) { self.state = cs::RewardedState::Unavailable; return; }
    self.loading = YES;
    self.state = cs::RewardedState::Loading;
    [GADRewardedAd loadWithAdUnitID:rewardedUnitId() request:[GADRequest request] completionHandler:^(GADRewardedAd* ad, NSError* error) {
        self.loading = NO;
        if (error) {
            self.state = cs::RewardedState::Unavailable;
            const double delay = kRetrySeconds[std::min(self.failures++, 3)];
            CS_LOGW("Ads: rewarded failed to load (%s), retry in %.0f s", error.localizedDescription.UTF8String, delay);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self load]; });
            return;
        }
        self.failures = 0;
        ad.fullScreenContentDelegate = self;
        self.rewarded = ad;
        self.state = cs::RewardedState::Ready;
        CS_LOGI("Ads: rewarded loaded");
    }];
}

- (BOOL)show {
    GADRewardedAd* ad = self.rewarded;
    if (!ad) { [self load]; return NO; }
    self.rewarded = nil;
    self.state = cs::RewardedState::Loading;
    CS_LOGI("Ads: showing rewarded");
    [ad presentFromRootViewController:topViewController() userDidEarnRewardHandler:^{
        CS_LOGI("Ads: reward earned");
        self.rewardEarned = YES; // granted by the game on its next frame
    }];
    return YES;
}

- (BOOL)consumeReward {
    if (!self.rewardEarned) return NO;
    self.rewardEarned = NO;
    return YES;
}

- (void)adDidDismissFullScreenContent:(id<GADFullScreenPresentingAd>)ad { [self load]; }

- (void)ad:(id<GADFullScreenPresentingAd>)ad didFailToPresentFullScreenContentWithError:(NSError*)error {
    CS_LOGW("Ads: rewarded failed to show: %s", error.localizedDescription.UTF8String);
    [self load];
}

@end

namespace cs {

IOSAds::IOSAds() : impl_((__bridge_retained void*)[CSAdsImpl new]) {}
IOSAds::~IOSAds() { CFBridgingRelease(impl_); }
void IOSAds::start() { [(__bridge CSAdsImpl*)impl_ start]; }
RewardedState IOSAds::rewardedState() { return ((__bridge CSAdsImpl*)impl_).state; }
bool IOSAds::showRewarded() { return [(__bridge CSAdsImpl*)impl_ show]; }
bool IOSAds::consumeReward() { return [(__bridge CSAdsImpl*)impl_ consumeReward]; }
bool IOSAds::privacyOptionsRequired() {
    return UMPConsentInformation.sharedInstance.privacyOptionsRequirementStatus == UMPPrivacyOptionsRequirementStatusRequired;
}
void IOSAds::showPrivacyOptions() {
    CSAdsImpl* impl = (__bridge CSAdsImpl*)impl_;
    [UMPConsentForm presentPrivacyOptionsFormFromViewController:topViewController() completionHandler:^(NSError* error) {
        if (error) CS_LOGW("Ads: privacy options: %s", error.localizedDescription.UTF8String);
        [impl startSdk];
    }];
}

} // namespace cs
