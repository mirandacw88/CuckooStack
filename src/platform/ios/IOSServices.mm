#include "IOSServices.h"
#include "../../core/Economy.h"
#include "../../core/Log.h"

#import "CuckooStack-Swift.h"
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>

namespace cs {

namespace {
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

// transaction ids already credited (kept across launches): a crash between crediting and finishing must not pay twice
NSString* const kCreditedKey = @"cs.credited_transactions";
bool alreadyCredited(NSString* tx) { return [[NSUserDefaults.standardUserDefaults arrayForKey:kCreditedKey] containsObject:tx]; }
void markCredited(NSString* tx) {
    NSMutableArray* a = [[NSUserDefaults.standardUserDefaults arrayForKey:kCreditedKey] mutableCopy] ?: [NSMutableArray new];
    [a addObject:tx];
    while (a.count > 100) [a removeObjectAtIndex:0];
    [NSUserDefaults.standardUserDefaults setObject:a forKey:kCreditedKey];
}
} // namespace

// ---------------------------------------------------------------- analytics + remote config
void IOSAnalytics::event(const std::string& name, const AnalyticsParams& params) {
    NSMutableArray<NSString*>* kv = [NSMutableArray arrayWithCapacity:params.size() * 2];
    for (const auto& [k, v] : params) { [kv addObject:ns(k)]; [kv addObject:ns(v)]; }
    [CSFirebase.shared logEvent:ns(name) kv:kv];
}

void IOSAnalytics::userProperty(const std::string& name, const std::string& value) {
    [CSFirebase.shared setUserProperty:ns(name) value:ns(value)];
}

std::optional<double> IOSAnalytics::number(const std::string& key) {
    const double v = [CSFirebase.shared remoteNumber:ns(key)];
    return std::isnan(v) ? std::nullopt : std::optional<double>(v);
}

// ---------------------------------------------------------------- reminders
NotifPermission IOSNotifications::permission() {
    const NSInteger s = [CSNotifications.shared permission];
    return s == 1 ? NotifPermission::Granted : s == 2 ? NotifPermission::Denied : NotifPermission::Unknown;
}
void IOSNotifications::requestPermission() { [CSNotifications.shared requestPermission]; }
void IOSNotifications::replaceAll(const std::vector<Reminder>& reminders) {
    NSMutableArray<NSNumber*>* ids = [NSMutableArray new];
    NSMutableArray<NSNumber*>* at = [NSMutableArray new];
    NSMutableArray<NSString*>* titles = [NSMutableArray new];
    NSMutableArray<NSString*>* bodies = [NSMutableArray new];
    for (const Reminder& r : reminders) {
        [ids addObject:@(r.id)];
        [at addObject:@(double(r.at))];
        [titles addObject:ns(r.title)];
        [bodies addObject:ns(r.body)];
    }
    [CSNotifications.shared replaceAllWithIds:ids at:at titles:titles bodies:bodies];
}
bool IOSNotifications::consumeOpened(int& id) {
    const NSInteger o = [CSNotifications.shared consumeOpened];
    if (o < 0) return false;
    id = int(o);
    return true;
}

// ---------------------------------------------------------------- share replay
ReplayState IOSReplay::state() { return ReplayState(std::clamp(int([CSReplay.shared state]), 0, 5)); }
void IOSReplay::setEnabled(bool on) { [CSReplay.shared setEnabled:on]; }
void IOSReplay::runStarted() { [CSReplay.shared runStarted]; }
void IOSReplay::saveClip(const ReplayMeta& m) { [CSReplay.shared saveClipWithDistance:m.distance score:m.score newBest:m.newBest day:ns(m.day)]; }
void IOSReplay::share(const std::string& caption, const std::string& url) { [CSReplay.shared shareWithCaption:ns(caption) url:ns(url)]; }
bool IOSReplay::consumeShared(std::string& target) {
    NSString* t = [CSReplay.shared consumeShared];
    if (!t) return false;
    target = t.UTF8String;
    return true;
}

// ---------------------------------------------------------------- leaderboards (ids: config/store/leaderboards.env via CMake)
IOSLeaderboards::IOSLeaderboards() { [CSLeaderboards.shared configureWithDaily:@CS_GC_DAILY allTime:@CS_GC_ALLTIME]; }
void IOSLeaderboards::submit(int meters) { [CSLeaderboards.shared submit:meters]; }
void IOSLeaderboards::show() { [CSLeaderboards.shared show]; }

// ---------------------------------------------------------------- friend nudges
bool IOSBackend::nudgesAvailable() { return [CSFirebase.shared nudgesAvailable]; }
void IOSBackend::createChallenge(const std::string& day, int meters) { [CSFirebase.shared createChallengeWithDay:ns(day) meters:meters]; }
bool IOSBackend::pollChallengeId(std::string& id) {
    NSString* i = [CSFirebase.shared pollChallengeId];
    if (!i) return false;
    id = i.UTF8String;
    return true;
}
void IOSBackend::challengeBeaten(const std::string& id, int meters) { [CSFirebase.shared challengeBeatenWithId:ns(id) meters:meters]; }

// ---------------------------------------------------------------- store
IOSStore::IOSStore() { [CSStore.shared start]; }

std::vector<Product> IOSStore::products() {
    std::vector<Product> out;
    for (NSString* item in [[CSStore.shared productsString] componentsSeparatedByString:@";"]) {
        const NSRange bar = [item rangeOfString:@"|"];
        if (bar.location == NSNotFound) continue;
        out.push_back({[item substringToIndex:bar.location].UTF8String, [item substringFromIndex:bar.location + 1].UTF8String});
    }
    return out;
}

bool IOSStore::purchase(const std::string& productId) { return [CSStore.shared purchase:ns(productId)]; }
void IOSStore::restore() { [CSStore.shared restore]; }

bool IOSStore::pollEvent(PurchaseEvent& out) {
    // raw StoreKit events -> verified game events
    while (NSString* e = [CSStore.shared pollEvent]) {
        NSArray<NSString*>* f = [e componentsSeparatedByString:@"|"];
        if (f.count == 2) { // "productId|cancelled|pending|failed"
            PurchaseEvent pe;
            pe.productId = f[0].UTF8String;
            pe.result = [f[1] isEqualToString:@"cancelled"] ? PurchaseResult::Cancelled : [f[1] isEqualToString:@"pending"] ? PurchaseResult::Pending : PurchaseResult::Failed;
            ready_.push_back(pe);
            continue;
        }
        if (f.count < 5 || ![f[0] isEqualToString:@"tx"]) continue;
        NSString* product = f[1];
        NSString* tx = f[2];
        const bool restored = [f[3] isEqualToString:@"1"];
        NSString* jws = [[f subarrayWithRange:NSMakeRange(4, f.count - 4)] componentsJoinedByString:@"|"];
        const ProductDef* def = findProduct(product.UTF8String);
        if (!def) { [CSStore.shared finish:tx]; continue; }
        if (def->kind != ProductKind::Coins && restored) { // owned non-consumable (launch / restore): no server round trip
            ready_.push_back({product.UTF8String, PurchaseResult::Success, true});
            [CSStore.shared finish:tx];
            continue;
        }
        if (alreadyCredited(tx)) { [CSStore.shared finish:tx]; continue; }
        if (!inFlight_.insert(tx.UTF8String).second) continue;
        const std::string txid = tx.UTF8String;
        [CSFirebase.shared verifyPurchaseWithProductId:product token:jws transactionId:tx done:^(BOOL valid) {
            dispatch_async(dispatch_get_main_queue(), ^{
                this->inFlight_.erase(txid);
                if (valid) markCredited(tx);
                this->ready_.push_back({product.UTF8String, valid ? PurchaseResult::Success : PurchaseResult::Failed, false});
                [CSStore.shared finish:tx];
            });
        }];
    }
    if (ready_.empty()) return false;
    out = ready_.front();
    ready_.pop_front();
    return true;
}

} // namespace cs
