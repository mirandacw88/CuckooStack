// src/core service interfaces backed by the Swift services (IOSServices.swift): Firebase Analytics + Remote Config,
// and StoreKit 2 purchases (verified through Firebase before the game credits a consumable).
#pragma once

#include "../../core/Services.h"

#include <deque>
#include <set>
#include <string>

namespace cs {

class IOSAnalytics final : public IAnalytics, public IRemoteConfig {
public:
    void event(const std::string& name, const AnalyticsParams& params) override;
    void userProperty(const std::string& name, const std::string& value) override;
    std::optional<double> number(const std::string& key) override;
};

class IOSNotifications final : public INotifications {
public:
    NotifPermission permission() override;
    void requestPermission() override;
    void replaceAll(const std::vector<Reminder>& reminders) override;
    bool consumeOpened(int& reminderId) override;
};

class IOSReplay final : public IReplay {
public:
    ReplayState state() override;
    void setEnabled(bool on) override;
    void runStarted() override;
    void saveClip(const ReplayMeta& meta) override;
    void share(const std::string& caption, const std::string& url) override;
    bool consumeShared(std::string& target) override;
};

class IOSLeaderboards final : public ILeaderboards {
public:
    IOSLeaderboards();
    bool available() override { return true; }
    void submit(int meters) override;
    void show() override;
};

class IOSBackend final : public IBackend {
public:
    bool nudgesAvailable() override;
    void createChallenge(const std::string& day, int meters) override;
    bool pollChallengeId(std::string& id) override;
    void challengeBeaten(const std::string& id, int meters) override;
};

class IOSStore final : public IStore {
public:
    IOSStore();
    std::vector<Product> products() override;
    bool purchase(const std::string& productId) override;
    void restore() override;
    bool pollEvent(PurchaseEvent& out) override;

private:
    std::deque<PurchaseEvent> ready_;      // verified, waiting for the game
    std::set<std::string> inFlight_;       // transaction ids being verified
};

} // namespace cs
