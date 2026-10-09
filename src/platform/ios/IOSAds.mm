#include "IOSAds.h"
#include "../../core/Log.h"

#import "CuckooStack-Swift.h" // CSFirebase (ad revenue -> Analytics)
#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <GoogleMobileAds/GoogleMobileAds.h>
#import <UIKit/UIKit.h>
#import <UserMessagingPlatform/UserMessagingPlatform.h>

#include <algorithm>

// From config/ads.env via CMake: *_TEST for staging and Debug, *_PROD for prod Release.
#if !defined(CS_ADMOB_IOS_REWARDED) || !defined(CS_ADMOB_IOS_INTERSTITIAL)
#error "CS_ADMOB_IOS_REWARDED / CS_ADMOB_IOS_INTERSTITIAL must come from config/ads.env (see CMakeLists.txt)"
#endif

namespace {
NSString* rewardedUnitId() { return @CS_ADMOB_IOS_REWARDED; }
NSString* interstitialUnitId() { return @CS_ADMOB_IOS_INTERSTITIAL; }
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

GADPaidEventHandler revenueLogger(NSString* format, NSString* unit) {
    return ^(GADAdValue* v) {
        [CSFirebase.shared logAdRevenueWithFormat:format unit:unit value:v.value.doubleValue currency:v.currencyCode];
    };
}
} // namespace

@interface CSAdsImpl : NSObject <GADFullScreenContentDelegate>
@property(nonatomic, strong) GADRewardedAd* rewarded;
@property(nonatomic, strong) GADInterstitialAd* interstitial;
@property(nonatomic) BOOL loadingRewarded, loadingInterstitial, sdkStarted, rewardEarned, showing, splashDone, begun;
@property(nonatomic) int rewardedFailures, interstitialFailures;
@property(nonatomic) int audience; // -1 unknown, 0 teen/adult, 1 child
@property(nonatomic) cs::RewardedState state;
@end

@implementation CSAdsImpl

- (instancetype)init {
    if ((self = [super init])) { _audience = -1; _state = cs::RewardedState::Loading; }
    return self;
}

- (void)splashGone { self.splashDone = YES; [self maybeBegin]; }

- (void)setAudienceChild:(BOOL)child {
    if (self.audience >= 0) return; // fixed for this process: the SDK reads it once at start
    self.audience = child ? 1 : 0;
    [self maybeBegin];
}

- (void)maybeBegin {
    if (!self.splashDone || self.audience < 0 || self.begun) return;
    self.begun = YES;
    GADRequestConfiguration* rc = GADMobileAds.sharedInstance.requestConfiguration;
    if (self.audience == 1) {
        rc.ageRestrictedTreatment = GADAgeRestrictedTreatmentChild; // COPPA child-directed + under the age of consent
        rc.maxAdContentRating = GADMaxAdContentRatingGeneral;
    } else {
        rc.maxAdContentRating = GADMaxAdContentRatingTeen; // the game is rated 13+
    }
    UMPRequestParameters* params = [UMPRequestParameters new];
    params.tagForUnderAgeOfConsent = self.audience == 1;
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

// App Tracking Transparency after UMP (Google's recommended order). Never asked of children.
- (void)requestTrackingThenStart {
    if (@available(iOS 14.5, *)) {
        if (self.audience == 0 && ATTrackingManager.trackingAuthorizationStatus == ATTrackingManagerAuthorizationStatusNotDetermined) {
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
        CS_LOGI("Ads: SDK ready (consent %ld, audience %s)", (long)UMPConsentInformation.sharedInstance.consentStatus, self.audience == 1 ? "child" : "13+");
        dispatch_async(dispatch_get_main_queue(), ^{ [self loadRewarded]; [self loadInterstitial]; });
    }];
}

// ---------------------------------------------------------------- rewarded
- (void)loadRewarded {
    if (self.rewarded || self.loadingRewarded) return;
    if (!UMPConsentInformation.sharedInstance.canRequestAds) { self.state = cs::RewardedState::Unavailable; return; }
    self.loadingRewarded = YES;
    self.state = cs::RewardedState::Loading;
    [GADRewardedAd loadWithAdUnitID:rewardedUnitId() request:[GADRequest request] completionHandler:^(GADRewardedAd* ad, NSError* error) {
        self.loadingRewarded = NO;
        if (error) {
            self.state = cs::RewardedState::Unavailable;
            const double delay = kRetrySeconds[std::min(self.rewardedFailures++, 3)];
            CS_LOGW("Ads: rewarded failed to load (%s), retry in %.0f s", error.localizedDescription.UTF8String, delay);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self loadRewarded]; });
            return;
        }
        self.rewardedFailures = 0;
        ad.fullScreenContentDelegate = self;
        ad.paidEventHandler = revenueLogger(@"rewarded", rewardedUnitId());
        self.rewarded = ad;
        self.state = cs::RewardedState::Ready;
        CS_LOGI("Ads: rewarded loaded");
    }];
}

- (BOOL)showRewarded:(NSString*)placement {
    GADRewardedAd* ad = self.rewarded;
    if (!ad) { [self loadRewarded]; return NO; }
    self.rewarded = nil;
    self.state = cs::RewardedState::Loading;
    self.showing = YES;
    GADServerSideVerificationOptions* ssv = [GADServerSideVerificationOptions new];
    ssv.customRewardString = placement; // shows up in AdMob reporting / server-side verification
    ad.serverSideVerificationOptions = ssv;
    CS_LOGI("Ads: showing rewarded (%s)", placement.UTF8String);
    [ad presentFromRootViewController:topViewController() userDidEarnRewardHandler:^{
        CS_LOGI("Ads: reward earned (%s)", placement.UTF8String);
        self.rewardEarned = YES; // granted by the game on its next frame
    }];
    return YES;
}

- (BOOL)consumeReward {
    if (!self.rewardEarned) return NO;
    self.rewardEarned = NO;
    return YES;
}

// ---------------------------------------------------------------- interstitial
- (void)loadInterstitial {
    if (self.interstitial || self.loadingInterstitial || !UMPConsentInformation.sharedInstance.canRequestAds) return;
    self.loadingInterstitial = YES;
    [GADInterstitialAd loadWithAdUnitID:interstitialUnitId() request:[GADRequest request] completionHandler:^(GADInterstitialAd* ad, NSError* error) {
        self.loadingInterstitial = NO;
        if (error) {
            const double delay = kRetrySeconds[std::min(self.interstitialFailures++, 3)];
            CS_LOGW("Ads: interstitial failed to load (%s), retry in %.0f s", error.localizedDescription.UTF8String, delay);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [self loadInterstitial]; });
            return;
        }
        self.interstitialFailures = 0;
        ad.fullScreenContentDelegate = self;
        ad.paidEventHandler = revenueLogger(@"interstitial", interstitialUnitId());
        self.interstitial = ad;
        CS_LOGI("Ads: interstitial loaded");
    }];
}

- (BOOL)showInterstitial {
    GADInterstitialAd* ad = self.interstitial;
    if (!ad) { [self loadInterstitial]; return NO; }
    self.interstitial = nil;
    self.showing = YES;
    CS_LOGI("Ads: showing interstitial");
    [ad presentFromRootViewController:topViewController()];
    return YES;
}

// ---------------------------------------------------------------- shared
- (void)adDidDismissFullScreenContent:(id<GADFullScreenPresentingAd>)ad {
    self.showing = NO;
    [self loadRewarded];
    [self loadInterstitial];
}

- (void)ad:(id<GADFullScreenPresentingAd>)ad didFailToPresentFullScreenContentWithError:(NSError*)error {
    CS_LOGW("Ads: failed to show: %s", error.localizedDescription.UTF8String);
    self.showing = NO;
    [self loadRewarded];
    [self loadInterstitial];
}

@end

namespace cs {

IOSAds::IOSAds() : impl_((__bridge_retained void*)[CSAdsImpl new]) {}
IOSAds::~IOSAds() { CFBridgingRelease(impl_); }
void IOSAds::start() { [(__bridge CSAdsImpl*)impl_ splashGone]; }
void IOSAds::setAudience(bool child) {
    [CSFirebase.shared setChild:child]; // Analytics consent follows the same audience
    [(__bridge CSAdsImpl*)impl_ setAudienceChild:child];
}
RewardedState IOSAds::rewardedState() { return ((__bridge CSAdsImpl*)impl_).state; }
bool IOSAds::showRewarded(const char* placement) { return [(__bridge CSAdsImpl*)impl_ showRewarded:@(placement)]; }
bool IOSAds::consumeReward() { return [(__bridge CSAdsImpl*)impl_ consumeReward]; }
bool IOSAds::interstitialReady() { return ((__bridge CSAdsImpl*)impl_).interstitial != nil; }
bool IOSAds::showInterstitial() { return [(__bridge CSAdsImpl*)impl_ showInterstitial]; }
bool IOSAds::adShowing() { return ((__bridge CSAdsImpl*)impl_).showing; }
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
