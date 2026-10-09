# Live ops: setup and running the game

Everything that has to exist outside the code before launch, and how to run the game once it's live.
The order below is the order to do it in.

## 1. Firebase projects (both environments)

1. Create two projects in the Firebase console, **`cuckoostack-staging`** and **`cuckoostack-prod`**, and put both on
   the **Blaze** plan (Cloud Functions need it; the free allowances still apply).
2. In each project, add:
   - an **Android app** with that environment's package (`…aerospheregames.staging` / `…aerospheregames`), plus the
     SHA-256 of its signing key;
   - an **iOS app** with that environment's bundle ID.
3. Download the config files (they are git-ignored):
   - `platforms/android/app/src/staging/google-services.json` and `.../src/prod/google-services.json`
   - `config/firebase/staging/GoogleService-Info.plist` and `config/firebase/prod/GoogleService-Info.plist`

   Then re-run `platforms/ios/generate_xcode.sh` (add `--env prod` for prod).
4. Turn on, in each project:
   - Analytics; link it to AdMob (AdMob → Settings → Linked services) so ad revenue shows per player.
   - Crashlytics.
   - Remote Config.
   - Authentication → Sign-in method → **Anonymous**.
   - Firestore (production mode).
   - Cloud Messaging: upload the APNs key for iOS.
5. Create `config/firebase/<env>/links.env` (git-ignored):
   ```
   APPLE_TEAM_ID=ABCDE12345
   ANDROID_SHA256=AA:BB:...   # that environment's app signing certificate (Play Console > App integrity)
   ```
6. Deploy (needs `npm i -g firebase-tools` and `firebase login`):
   ```bash
   scripts/firebase_deploy.sh staging
   ```
   This deploys the functions (`verifyPurchase`, `createChallenge`, `challengeBeaten`), the Firestore rules, hosting
   (the challenge page plus the app-link files) and the Remote Config template, which is generated from `Tuning.h`.
   For prod, run `scripts/firebase_deploy.sh prod` from a clean `main`, then promote Remote Config with
   `scripts/promote_remote_config.sh` after reviewing the diff.
7. Local testing without touching either project:
   ```bash
   cd firebase && firebase emulators:start
   ```

### Purchase verification credentials

- **Google Play:** Play Console → Users and permissions → invite the Cloud Functions service account
  (`<project>@appspot.gserviceaccount.com`) with "View financial data".
- **Apple:** set the numeric App Store app ID for prod (`firebase functions:config` is not used; it's a param):
  `firebase deploy --only functions` prompts for `APPLE_APP_ID` the first time. Staging accepts sandbox and Xcode
  transactions.

## 2. AdMob

1. Create the apps and the **rewarded** and **interstitial** ad units (one set per platform).
2. Paste the IDs into [`config/ads.env`](../config/ads.env) as `*_PROD`. A prod Release build fails while any are
   empty. Staging always uses Google's test IDs.
3. In **Privacy & messaging**:
   - create the GDPR and US-states consent messages;
   - create the **IDFA explainer** (iOS), which is shown before the tracking prompt.
4. Under **Blocking controls** and the app's settings, keep the maximum ad content rating at **T**. The game also
   sets it in code (G for children).

## 3. Store products

All IDs and amounts come from [`config/store/products.json`](../config/store/products.json); `economy_test` checks it
against the game. Create each product in **both** apps (staging and prod):

| ID | App Store type | Play type | Price tier |
|---|---|---|---|
| `coins_s` / `coins_m` / `coins_l` / `coins_xl` / `coins_xxl` | Consumable | One-time product | $0.99 / $2.99 / $4.99 / $9.99 / $19.99 |
| `starter_pack` | Non-consumable | One-time product | $2.99 |
| `remove_ads` | Non-consumable | One-time product | $3.99 |

In-store display names can say "Coin Pack"; the game itself shows only the coin icon and the amount.
Not automated yet: a script that creates these from `products.json` through the App Store Connect and Play
Developer APIs.

## 4. Leaderboards

[`config/store/leaderboards.env`](../config/store/leaderboards.env):
- **Game Center:** in both iOS apps, a *recurring* daily leaderboard (`cuckoostack.daily_distance`) and a classic
  one (`cuckoostack.best_distance`). Turn on the Game Center capability for both bundle IDs.
- **Play Games Services:** create the project per Android app, add a leaderboard ("Distance", higher is better), and
  fill `PLAY_GAMES_APP_ID_*` and `PLAY_LEADERBOARD_*`. The Leaders button stays hidden on Android until they're set.

## 5. Links (challenges)

- Links look like `https://cuckoostack-<env>.web.app/c?d=YYYY-MM-DD&m=412[&n=Name][&c=<challenge id>]`, set in
  [`src/core/Links.h`](../src/core/Links.h).
- **iOS:** the Associated Domains capability is in `CuckooStack.entitlements.in`. Enable it for the bundle IDs.
- **Android:** App Links are verified through `assetlinks.json`, which `firebase_deploy.sh` writes from `links.env`.
- **Test locally:**
  ```bash
  xcrun simctl openurl booted "cuckoostack-staging://c?d=$(date +%F)&m=50&n=Sam"
  ```
  ```bash
  adb shell am start -a android.intent.action.VIEW -d "cuckoostack-staging://c?d=$(date +%F)\&m=50\&n=Sam"
  ```
- With a custom domain later: change `Links.h`, the entitlements, `linkHost` in `app/build.gradle.kts`, and point
  the domain at Firebase Hosting.

## 6. Store forms

- **Apple App Privacy:**
  - Identifiers: device ID (ads, analytics).
  - Usage data: product interaction, advertising data.
  - Diagnostics: crash data (Crashlytics).
  - Purchases.

  Age rating 13+. In-app purchases listed. Push capability.
- **Google Play Data safety:** the same categories. **Target audience:** 13+, and declare that the app shows a
  neutral age screen and treats younger users with child-directed ads. **Ads declaration:** yes.
- Have the age handling (`Profile` audience, `IAds::setAudience`) reviewed by a lawyer before launch.

## 7. Running the game

- **Dashboards (Firebase Analytics):**
  - Day-1/7/30 retention.
  - `ad_impression` revenue per active user.
  - `run_end` per session.
  - The funnel: `ad_offer_shown` → `ad_offer_accepted` → `ad_rewarded_complete` per placement.
  - `iap_view` → `iap_purchase`.
  - `replay_share_tap` / `challenge_open` for virality.
- **A/B tests:** Firebase A/B Testing → Remote Config experiment on any `Tuning.h` key (for example
  `ad_every_runs` 3 vs 4, `continue_secs`, `dda_newbie`, prices). The game reads new values at the next launch, so a
  session never changes rules halfway through. Judge on day-7 retention and on ad revenue per daily user together.
- **Live events:** set `event_coin_mult` (for example 2) in Remote Config, with a condition (date range, country,
  user property). The title screen shows a "2× coins event" banner, and run and mission coins are multiplied.
- **Campaign pushes:** Cloud Messaging → new campaign → topic `all` (or `ios` / `android`). Only 13+ players who
  turned reminders on are subscribed. Keep it to occasional events, because reminders already send at most one a day.
- **Known gaps:**
  - Android Share Replay video: see [ANDROID_REPLAY.md](ANDROID_REPLAY.md). Sharing falls back to text and a link
    until it's fixed.
  - Linking the anonymous Firebase account to Game Center / Play Games (to keep the cloud wallet across devices).
  - The store-product sync script.
