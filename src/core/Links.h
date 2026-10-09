// Web links the game shares (Share Replay captions, challenge links). Firebase Hosting serves each environment's
// project domain for free, including the app-link files (firebase/hosting); swap in a custom domain here once
// you have one (and add it to Associated Domains / the Android intent filter).
#pragma once

namespace cs::links {

#if defined(CS_ENV_PROD)
constexpr const char* kSite = "https://cuckoostack-prod.web.app";
#else
constexpr const char* kSite = "https://cuckoostack-staging.web.app";
#endif

} // namespace cs::links
