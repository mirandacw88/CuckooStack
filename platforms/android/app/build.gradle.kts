plugins {
    id("com.android.application")
    id("com.google.gms.google-services")
    id("com.google.firebase.crashlytics")
}

// Firebase config per environment: app/src/staging/google-services.json and app/src/prod/google-services.json
// (git-ignored; download them from the two Firebase projects). Without one, that build runs with Firebase off.
googleServices {
    missingGoogleServicesStrategy = com.google.gms.googleservices.GoogleServicesPlugin.MissingGoogleServicesStrategy.WARN
}

// AdMob IDs come from config/ads.env (shared with iOS): *_TEST for staging and debug builds, *_PROD for prod
// release builds (which fail if a PROD ID is missing).
val adsEnvFile = rootProject.file("../../config/ads.env")
val adsEnv: Map<String, String> = adsEnvFile.takeIf { it.exists() }?.readLines().orEmpty()
    .map { it.trim() }
    .filter { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
    .associate { it.substringBefore("=").trim() to it.substringAfter("=").trim().removeSurrounding("\"") }
// Play Games leaderboard (config/store/leaderboards.env); empty until the Play Games project exists
val boardsEnv: Map<String, String> = rootProject.file("../../config/store/leaderboards.env").takeIf { it.exists() }?.readLines().orEmpty()
    .map { it.trim() }
    .filter { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
    .associate { it.substringBefore("=").trim() to it.substringAfter("=").trim() }

fun adsId(key: String): String = adsEnv["${key}_TEST"]?.takeIf { it.isNotBlank() }
    ?: throw GradleException("config/ads.env: ${key}_TEST is missing")
fun adsProdId(key: String): String = adsEnv["${key}_PROD"]?.takeIf { it.isNotBlank() }
    ?: "MISSING_${key}_PROD" // checked by the prodRelease guard below, so test IDs can never ship

android {
    namespace = "com.cuckoostack.aerospheregames"
    // Google Play: new apps and updates must target API 36 since 31 Aug 2026 (API 35 for existing listings).
    compileSdk = 36
    // NDK r28+: 16 KB page-aligned ELF by default (Android 15+ devices); CMakeLists.txt also forces it.
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.cuckoostack.aerospheregames"
        minSdk = 24          // Android 7.0: first release with Vulkan in the platform
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"
        ndk {
            // arm64 covers almost every Vulkan-capable phone; armv7 for older 32-bit devices; x86_64 for emulators/ChromeOS
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared") // required by the GameActivity prefab
            }
        }
    }

    buildFeatures {
        prefab = true // GameActivity native library and headers
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt") // the same root build as iOS and desktop
            version = "3.22.1"
        }
    }

    // Environments (see README): two Firebase projects, two app IDs so both builds install side by side.
    flavorDimensions += "env"
    productFlavors {
        create("staging") {
            dimension = "env"
            applicationIdSuffix = ".staging"
            versionNameSuffix = "-staging"
            resValue("string", "app_name", "Cuckoo Stack \u03b2")
            externalNativeBuild { cmake { arguments += "-DCS_ENV=staging" } }
            manifestPlaceholders["linkHost"] = "cuckoostack-staging.web.app" // challenge links (src/core/Links.h)
            manifestPlaceholders["linkScheme"] = "cuckoostack-staging"
            resValue("string", "game_services_project_id", boardsEnv["PLAY_GAMES_APP_ID_STAGING"] ?: "")
            resValue("string", "play_leaderboard_id", boardsEnv["PLAY_LEADERBOARD_STAGING"] ?: "")
        }
        create("prod") {
            dimension = "env"
            resValue("string", "app_name", "Cuckoo Stack")
            externalNativeBuild { cmake { arguments += "-DCS_ENV=prod" } }
            manifestPlaceholders["linkHost"] = "cuckoostack-prod.web.app"
            manifestPlaceholders["linkScheme"] = "cuckoostack"
            resValue("string", "game_services_project_id", boardsEnv["PLAY_GAMES_APP_ID_PROD"] ?: "")
            resValue("string", "play_leaderboard_id", boardsEnv["PLAY_LEADERBOARD_PROD"] ?: "")
        }
    }

    buildTypes {
        debug {
            manifestPlaceholders["admobAppId"] = adsId("ADMOB_ANDROID_APP_ID")
            buildConfigField("String", "ADMOB_REWARDED_ID", "\"${adsId("ADMOB_ANDROID_REWARDED")}\"")
            buildConfigField("String", "ADMOB_INTERSTITIAL_ID", "\"${adsId("ADMOB_ANDROID_INTERSTITIAL")}\"")
        }
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    // test ads for staging (any build type); real ads only for prodRelease
    androidComponents {
        onVariants { variant ->
            val prodRelease = variant.flavorName == "prod" && variant.buildType == "release"
            if (variant.buildType == "release") {
                fun id(key: String) = if (prodRelease) adsProdId(key) else adsId(key)
                variant.manifestPlaceholders.put("admobAppId", id("ADMOB_ANDROID_APP_ID"))
                variant.buildConfigFields?.put("ADMOB_REWARDED_ID", com.android.build.api.variant.BuildConfigField("String", "\"${id("ADMOB_ANDROID_REWARDED")}\"", null))
                variant.buildConfigFields?.put("ADMOB_INTERSTITIAL_ID", com.android.build.api.variant.BuildConfigField("String", "\"${id("ADMOB_ANDROID_INTERSTITIAL")}\"", null))
            }
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false // keep .so files uncompressed and 16 KB aligned in the APK
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// a prod release must never carry test ad IDs: fail the build if config/ads.env has an empty *_PROD value
tasks.matching { it.name == "preProdReleaseBuild" }.configureEach {
    doFirst {
        val missing = listOf("ADMOB_ANDROID_APP_ID", "ADMOB_ANDROID_REWARDED", "ADMOB_ANDROID_INTERSTITIAL").filter { adsEnv["${it}_PROD"].isNullOrBlank() }
        if (missing.isNotEmpty()) throw GradleException("config/ads.env: set ${missing.joinToString { "${it}_PROD" }} before building prodRelease")
    }
}

dependencies {
    implementation("androidx.games:games-activity:4.4.2")
    implementation("com.google.oboe:oboe:1.11.0")
    implementation("com.google.android.gms:play-services-ads:25.5.0")
    implementation("com.google.android.ump:user-messaging-platform:4.0.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.core:core:1.17.0")
    implementation("com.android.billingclient:billing:8.0.0")
    implementation("androidx.work:work-runtime:2.10.0")
    implementation("com.google.android.gms:play-services-games-v2:20.1.2")
    implementation(platform("com.google.firebase:firebase-bom:34.0.0"))
    implementation("com.google.firebase:firebase-analytics")
    implementation("com.google.firebase:firebase-crashlytics")
    implementation("com.google.firebase:firebase-config")
    implementation("com.google.firebase:firebase-auth")
    implementation("com.google.firebase:firebase-functions")
    implementation("com.google.firebase:firebase-messaging")
}
