plugins {
    id("com.android.application")
}

// AdMob IDs come from config/ads.env (shared with iOS): *_TEST for debug builds, *_PROD for release builds.
val adsEnvFile = rootProject.file("../../config/ads.env")
val adsEnv: Map<String, String> = adsEnvFile.takeIf { it.exists() }?.readLines().orEmpty()
    .map { it.trim() }
    .filter { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
    .associate { it.substringBefore("=").trim() to it.substringAfter("=").trim().removeSurrounding("\"") }
fun adsId(key: String): String = adsEnv["${key}_TEST"]?.takeIf { it.isNotBlank() }
    ?: throw GradleException("config/ads.env: ${key}_TEST is missing")
fun adsProdId(key: String): String = adsEnv["${key}_PROD"]?.takeIf { it.isNotBlank() } ?: run {
    logger.warn("config/ads.env: ${key}_PROD is empty, so release builds will show TEST ads")
    adsId(key)
}

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

    buildTypes {
        debug {
            manifestPlaceholders["admobAppId"] = adsId("ADMOB_ANDROID_APP_ID")
            buildConfigField("String", "ADMOB_REWARDED_ID", "\"${adsId("ADMOB_ANDROID_REWARDED")}\"")
        }
        release {
            manifestPlaceholders["admobAppId"] = adsProdId("ADMOB_ANDROID_APP_ID")
            buildConfigField("String", "ADMOB_REWARDED_ID", "\"${adsProdId("ADMOB_ANDROID_REWARDED")}\"")
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
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

dependencies {
    implementation("androidx.games:games-activity:4.4.2")
    implementation("com.google.oboe:oboe:1.11.0")
    implementation("com.google.android.gms:play-services-ads:25.5.0")
    implementation("com.google.android.ump:user-messaging-platform:4.0.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("androidx.core:core:1.17.0")
}
