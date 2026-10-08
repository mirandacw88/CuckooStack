// Lives + rewarded-ad refill. Tuning lives here; platform SDK code implements IAds (Services.h).
#pragma once

namespace cs::lives {

constexpr int START = 3;                                  // new players
constexpr int REWARD = 3;                                 // lives granted by one rewarded ad
constexpr const char* KEY = "cluckstack-lives";           // persisted lives
constexpr const char* RUN_KEY = "cluckstack-run-active";  // set while a run is in progress (quitting mid-run costs the life)

// If no rewarded ad can be shown (offline, no fill, consent forbids ads), players would be locked out for good, and
// App Review rejects apps that stop working when an ad can't load. After AD_WAIT_SECONDS the offer turns into
// "Play anyway" and grants the lives. Set false to make the ad strictly the only way.
constexpr bool GRANT_WHEN_AD_UNAVAILABLE = true;
constexpr float AD_WAIT_SECONDS = 8.f;

} // namespace cs::lives
