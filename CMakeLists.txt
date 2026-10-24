# Cuckoo Stack: one build for Android (Gradle externalNativeBuild), iOS (Xcode generator) and desktop.
#
#   Desktop : cmake -S . -B build -G Ninja && cmake --build build && ./build/cuckoo_stack
#   iOS     : platforms/ios/generate_xcode.sh   (CMake -G Xcode, MoltenVK.xcframework from the Vulkan SDK)
#   Android : cd platforms/android && ./gradlew assembleDebug   (Gradle drives this file through the NDK)
cmake_minimum_required(VERSION 3.22)
project(CuckooStack VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
  # Objective-C (GLFW's Cocoa .m files) and Objective-C++ (the iOS sources; on macOS the iOS video recorder's test),
  # enabled up front so every target, GLFW included, gets their build rules
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(CS_PLATFORM ios)
  cmake_minimum_required(VERSION 3.28) # xcframework linking/embedding
  enable_language(Swift) # StoreKit 2 + Firebase glue (src/platform/ios/IOSServices.swift)
elseif(ANDROID)
  set(CS_PLATFORM android)
else()
  set(CS_PLATFORM desktop)
endif()

# Environment (README "Environments"): staging and prod use separate Firebase projects and app IDs.
# Android sets it per product flavour; iOS: generate_xcode.sh --env prod. Game rules never depend on it.
set(CS_ENV "staging" CACHE STRING "staging | prod")
set_property(CACHE CS_ENV PROPERTY STRINGS staging prod)
if(NOT CS_ENV MATCHES "^(staging|prod)$")
  message(FATAL_ERROR "CS_ENV must be staging or prod (got '${CS_ENV}')")
endif()
if(CS_ENV STREQUAL "prod")
  add_compile_definitions(CS_ENV_PROD)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
foreach(dep volk/volk.h Vulkan-Headers/include/vulkan/vulkan_core.h VMA/include/vk_mem_alloc.h glm/glm/glm.hpp stb/stb_truetype.h)
  if(NOT EXISTS ${TP}/${dep})
    message(FATAL_ERROR "Missing third_party/${dep}. Run scripts/bootstrap_deps.sh first.")
  endif()
endforeach()

# ---------------------------------------------------------------- third party
# volk: Vulkan entry points resolved at runtime (dlopen + vkGetInstanceProcAddr); libvulkan is never linked.
add_library(volk STATIC ${TP}/volk/volk.c)
target_include_directories(volk SYSTEM PUBLIC ${TP}/volk ${TP}/Vulkan-Headers/include)
target_link_libraries(volk PRIVATE ${CMAKE_DL_LIBS})

add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE ${TP}/glm)

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ${TP}/VMA/include)

set(CS_WARNINGS -Wall -Wextra -Wno-missing-field-initializers)
# Debug builds still optimise the game's own code: unoptimised builds spent ~20x more CPU per frame (particles,
# physics, render-list building) and made phones run hot when launched from Xcode / installDebug. Debug info is
# kept, so breakpoints and stack traces still work (some locals may show as optimised out).
set(CS_OPT $<$<CONFIG:Debug>:-O2>)

# ---------------------------------------------------------------- shaders
include(cmake/Shaders.cmake)

# ---------------------------------------------------------------- fonts: embedded TTFs (assets/fonts, SIL OFL 1.1)
set(CS_FONT_SOURCES "")
foreach(pair "display:CuckooDisplay-Black" "body:ChakraPetch-Medium" "body_bold:ChakraPetch-Bold" "japanese:NotoSansJP-Bold-signs"
             "hieroglyphs:NotoSansEgyptianHieroglyphs-tomb")
  string(REPLACE ":" ";" parts ${pair})
  list(GET parts 0 sym)
  list(GET parts 1 file)
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${file}.ttf)
  set(cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts/${sym}.cpp)
  add_custom_command(OUTPUT ${cpp}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${src} -DOUTPUT=${cpp} -DSYMBOL=${sym} -DNAMESPACE=cs::fontdata -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${src} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding font ${file}"
    VERBATIM)
  list(APPEND CS_FONT_SOURCES ${cpp})
endforeach()
# HUD icon atlas (scripts/hud/make_hud_icons.py), embedded like the fonts
set(_hud_png ${CMAKE_CURRENT_SOURCE_DIR}/assets/hud/hud_icons.png)
set(_hud_cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts/hud_icons.cpp)
add_custom_command(OUTPUT ${_hud_cpp}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_hud_png} -DOUTPUT=${_hud_cpp} -DSYMBOL=hud_icons -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${_hud_png} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  COMMENT "Embedding HUD sprites"
  VERBATIM)
list(APPEND CS_FONT_SOURCES ${_hud_cpp})
# Neon Egypt's backdrop atlas (scripts/backdrop/build_egypt_backdrop.py), embedded the same way
set(_egypt_png ${CMAKE_CURRENT_SOURCE_DIR}/assets/backdrop/egypt_backdrop.png)
set(_egypt_cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts/egypt_backdrop.cpp)
add_custom_command(OUTPUT ${_egypt_cpp}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_egypt_png} -DOUTPUT=${_egypt_cpp} -DSYMBOL=egypt_backdrop -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${_egypt_png} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  COMMENT "Embedding the Egypt backdrop"
  VERBATIM)
list(APPEND CS_FONT_SOURCES ${_egypt_cpp})
# the hen model (scripts/models/build_hen_mesh.py), embedded the same way
set(_hen_mesh ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/hen.mesh)
set(_hen_cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts/hen_mesh.cpp)
add_custom_command(OUTPUT ${_hen_cpp}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_hen_mesh} -DOUTPUT=${_hen_cpp} -DSYMBOL=hen_mesh -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${_hen_mesh} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  COMMENT "Embedding the hen model"
  VERBATIM)
list(APPEND CS_FONT_SOURCES ${_hen_cpp})
# the title screen's jet droid (scripts/models/export_droid.py, run in Blender): mesh + albedo + normal map
foreach(_m droid.mesh droid.jpg droid_n.jpg)
  string(REPLACE "." "_" _sym ${_m})
  set(_in ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/${_m})
  set(_out ${CMAKE_CURRENT_BINARY_DIR}/fonts/${_sym}.cpp)
  add_custom_command(OUTPUT ${_out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_in} -DOUTPUT=${_out} -DSYMBOL=${_sym} -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${_in} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding the droid ${_m}"
    VERBATIM)
  list(APPEND CS_FONT_SOURCES ${_out})
endforeach()
# the soundtrack (assets/music, beat grids in src/core/audio/MusicTracks.inc via scripts/audio/analyze_music.py)
foreach(_m midnight_city_sprint.mp3 giza_overdrive.mp3 katana_overdrive.mp3)
  string(REPLACE "." "_" _sym ${_m})
  set(_in ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/${_m})
  set(_out ${CMAKE_CURRENT_BINARY_DIR}/fonts/${_sym}.cpp)
  add_custom_command(OUTPUT ${_out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_in} -DOUTPUT=${_out} -DSYMBOL=${_sym} -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${_in} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding music ${_m}"
    VERBATIM)
  list(APPEND CS_FONT_SOURCES ${_out})
endforeach()
# recorded sound effects (assets/sfx; played by src/core/audio/Music.cpp)
foreach(_m applause_cheer.mp3)
  string(REPLACE "." "_" _sym ${_m})
  set(_in ${CMAKE_CURRENT_SOURCE_DIR}/assets/sfx/${_m})
  set(_out ${CMAKE_CURRENT_BINARY_DIR}/fonts/${_sym}.cpp)
  add_custom_command(OUTPUT ${_out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_in} -DOUTPUT=${_out} -DSYMBOL=${_sym} -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${_in} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding sound effect ${_m}"
    VERBATIM)
  list(APPEND CS_FONT_SOURCES ${_out})
endforeach()
# outfits with their own hen model (scripts/models/add_outfit_model.py): every assets/models/outfits/<id>.mesh + .jpg
file(GLOB _outfit_assets CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/outfits/*.mesh
     ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/outfits/*.jpg)
foreach(_in ${_outfit_assets})
  get_filename_component(_name ${_in} NAME)
  string(REPLACE "." "_" _sym "outfit_${_name}")
  set(_out ${CMAKE_CURRENT_BINARY_DIR}/fonts/${_sym}.cpp)
  add_custom_command(OUTPUT ${_out}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_in} -DOUTPUT=${_out} -DSYMBOL=${_sym} -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    DEPENDS ${_in} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
    COMMENT "Embedding outfit model ${_name}"
    VERBATIM)
  list(APPEND CS_FONT_SOURCES ${_out})
endforeach()
add_library(cs_fonts STATIC ${CS_FONT_SOURCES})

# ---------------------------------------------------------------- level: courses, gauntlets and their rules, OS-free
# (no game, no services): what the game, the checkers and the level editors share
add_library(cs_level STATIC
  src/core/Chunk.cpp
  src/core/CourseRules.cpp
  src/core/Difficulty.cpp
  src/core/Gauntlet.cpp
  src/core/GauntletDesigns.cpp
  src/core/Gauntlets.cpp
  src/core/Hazards.cpp
  src/core/hazards/Disco.cpp
  src/core/hazards/Laser.cpp
  src/core/hazards/Lava.cpp
  src/core/hazards/Spring.cpp
  src/core/hazards/Saw.cpp
  src/core/Json.cpp
  src/core/Level.cpp
  src/core/LevelCatalog.cpp
  src/core/LevelData.cpp
  src/core/LevelJudge.cpp)
target_include_directories(cs_level PUBLIC src)
target_link_libraries(cs_level PUBLIC glm)
target_compile_options(cs_level PRIVATE ${CS_WARNINGS} ${CS_OPT})

# ---------------------------------------------------------------- core: game logic, OS-free
add_library(cs_core STATIC
  src/core/Camera.cpp
  src/core/BotRun.cpp
  src/core/LevelBake.cpp
  src/core/LevelBeats.cpp
  src/core/Campaign.cpp
  src/core/Daily.cpp
  src/core/DroidModel.cpp
  src/core/Economy.cpp
  src/core/HenModel.cpp
  src/core/EndCard.cpp
  src/core/Effects.cpp
  src/core/FileStorage.cpp
  src/core/Font.cpp
  src/core/Game.cpp
  src/core/GameDroid.cpp src/core/GameBot.cpp src/core/GameSaw.cpp src/core/GameLaser.cpp src/core/GameLava.cpp src/core/GameSpring.cpp
  src/core/GameGauntlet.cpp
  src/core/GameHud.cpp
  src/core/GameSurge.cpp
  src/core/GameUi.cpp
  src/core/HudImage.cpp
  src/core/Geometry.cpp
  src/core/Hen.cpp
  src/core/Meta.cpp
  src/core/Monetization.cpp
  src/core/Reminders.cpp
  src/core/Tuning.cpp
  src/core/World.cpp
  src/core/WorldEgypt.cpp
  src/core/audio/Music.cpp
  src/core/audio/MiniaudioImpl.cpp
  src/core/audio/Synth.cpp)
target_include_directories(cs_core PUBLIC src)
target_include_directories(cs_core SYSTEM PRIVATE ${TP}/stb ${TP}/miniaudio)
# miniaudio: MP3 decoding everywhere; the playback device too on desktop (DesktopAudio.cpp)
if(CS_PLATFORM STREQUAL "desktop")
  set_source_files_properties(src/core/audio/MiniaudioImpl.cpp PROPERTIES COMPILE_DEFINITIONS CS_MA_DEVICE_IO)
endif()
set_source_files_properties(src/core/audio/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "-O2;-w") # decode fast even in Debug
find_package(Threads REQUIRED) # (Neon Egypt's backdrop art is decoded off the main thread)
target_link_libraries(cs_core PUBLIC cs_level glm Threads::Threads PRIVATE cs_fonts)
# the SDF atlas bake (stb_truetype) runs at startup: keep it optimised even in Debug builds
set_source_files_properties(src/core/Font.cpp PROPERTIES COMPILE_OPTIONS "-O2")
target_compile_options(cs_core PRIVATE ${CS_WARNINGS} ${CS_OPT})

# ---------------------------------------------------------------- validation: the checkers' reports and bot runs
# (level_check, gauntlet_check, the level editors): the game played headless, in-memory services
add_library(cs_validate STATIC
  src/core/GauntletReport.cpp
  src/core/ReportJson.cpp)
target_link_libraries(cs_validate PUBLIC cs_core)
target_compile_options(cs_validate PRIVATE ${CS_WARNINGS} ${CS_OPT})

# ---------------------------------------------------------------- the level editor's core (src/editor): a level open
# for editing, its tools and scene, the bot in the background, merge and refit; the iPhone and iPad editor's, behind a
# plain C API (cs_editor.h)
add_library(cs_editor STATIC
  src/editor/BotJob.cpp
  src/editor/CEditor.cpp
  src/editor/EditCourse.cpp
  src/editor/EditorDoc.cpp
  src/editor/EditorScene.cpp
  src/editor/EditorTools.cpp
  src/editor/GridDoc.cpp
  src/editor/GridJob.cpp
  src/editor/MazeGen.cpp
  src/editor/LevelMerge.cpp)
target_link_libraries(cs_editor PUBLIC cs_validate)
target_compile_options(cs_editor PRIVATE ${CS_WARNINGS} ${CS_OPT})

# ---------------------------------------------------------------- graphics: Vulkan 1.0 + volk + VMA, OS-free
add_library(cs_graphics STATIC
  src/graphics/BakedPatterns.cpp
  src/graphics/Renderer.cpp
  src/graphics/VmaImpl.cpp
  src/graphics/VulkanBuffer.cpp
  src/graphics/VulkanContext.cpp
  src/graphics/VulkanImage.cpp
  src/graphics/VulkanPipeline.cpp
  src/graphics/VulkanSwapchain.cpp)
target_link_libraries(cs_graphics PUBLIC cs_core volk vma PRIVATE cs_shaders)
target_include_directories(cs_graphics SYSTEM PRIVATE ${TP}/stb) # stb_image: the hen texture (implementation in HudImage.cpp)
target_compile_options(cs_graphics PRIVATE ${CS_WARNINGS} ${CS_OPT})
set_source_files_properties(src/graphics/VmaImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")

# ---------------------------------------------------------------- platform targets
if(CS_PLATFORM STREQUAL "android")
  # GameActivity prefab (androidx.games:games-activity), enabled by `buildFeatures { prefab = true }`
  find_package(game-activity REQUIRED CONFIG)
  find_package(oboe REQUIRED CONFIG) # com.google.oboe:oboe prefab: AAudio / OpenSL ES output
  add_library(cuckoo_stack SHARED
    src/platform/android/AndroidAudio.cpp
    src/platform/android/AndroidMain.cpp
    src/platform/android/AndroidReplay.cpp
    src/platform/android/AndroidWindow.cpp)
  target_link_libraries(cuckoo_stack PRIVATE cs_graphics game-activity::game-activity_static oboe::oboe android log mediandk)
  target_compile_options(cuckoo_stack PRIVATE ${CS_WARNINGS} ${CS_OPT})
  target_link_options(cuckoo_stack PRIVATE
    # keep GameActivity's JNI entry points when linking it statically
    "-u" "Java_com_google_androidgamesdk_GameActivity_initializeNativeCode"
    # 16 KB page alignment: required for apps targeting Android 15+ on Google Play
    "-Wl,-z,max-page-size=16384")

elseif(CS_PLATFORM STREQUAL "ios")
  set(CS_MOLTENVK_XCFRAMEWORK "$ENV{VULKAN_SDK}/../iOS/lib/MoltenVK.xcframework" CACHE PATH
      "Dynamic MoltenVK.xcframework (Vulkan SDK: <sdk>/iOS/lib/MoltenVK.xcframework)")
  if(NOT EXISTS ${CS_MOLTENVK_XCFRAMEWORK})
    message(FATAL_ERROR "MoltenVK.xcframework not found. Pass -DCS_MOLTENVK_XCFRAMEWORK=/path/to/MoltenVK.xcframework")
  endif()
  set(CS_LAUNCH ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/LaunchScreen.storyboard)
  set(CS_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/Assets.xcassets) # AppIcon (scripts/icon/make_icons.py)

  # AdMob IDs from config/ads.env (shared with Android): *_TEST for staging and for Debug, *_PROD for prod Release.
  # Editing the file re-runs CMake automatically on the next Xcode build.
  set(CS_ADS_ENV ${CMAKE_CURRENT_SOURCE_DIR}/config/ads.env)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CS_ADS_ENV})
  file(STRINGS ${CS_ADS_ENV} _ads_lines REGEX "^[A-Z0-9_]+=")
  foreach(line ${_ads_lines})
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _ ${line})
    string(STRIP "${CMAKE_MATCH_2}" _val)
    set(ADS_${CMAKE_MATCH_1} "${_val}")
  endforeach()
  set(CS_ADS_PROD_MISSING "")
  foreach(key ADMOB_IOS_APP_ID ADMOB_IOS_REWARDED ADMOB_IOS_INTERSTITIAL)
    if(NOT ADS_${key}_TEST)
      message(FATAL_ERROR "config/ads.env: ${key}_TEST is missing")
    endif()
    if(CS_ENV STREQUAL "prod" AND NOT ADS_${key}_PROD)
      list(APPEND CS_ADS_PROD_MISSING ${key}_PROD)
      set(ADS_${key}_PROD "${ADS_${key}_TEST}") # Debug still works; Release is stopped by the check below
    endif()
    if(CS_ENV STREQUAL "prod")
      set(CS_AD_${key} "$<IF:$<CONFIG:Debug>,${ADS_${key}_TEST},${ADS_${key}_PROD}>")
    else()
      set(CS_AD_${key} "${ADS_${key}_TEST}") # staging: test ads in every configuration
    endif()
  endforeach()

  # Game Center leaderboard ids (config/store/leaderboards.env)
  set(CS_BOARDS_ENV ${CMAKE_CURRENT_SOURCE_DIR}/config/store/leaderboards.env)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CS_BOARDS_ENV})
  file(STRINGS ${CS_BOARDS_ENV} _board_lines REGEX "^[A-Z0-9_]+=")
  foreach(line ${_board_lines})
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _ ${line})
    string(STRIP "${CMAKE_MATCH_2}" _val)
    set(BOARD_${CMAKE_MATCH_1} "${_val}")
  endforeach()

  # Environment: app ID, name, icon and Firebase project (config/firebase/<env>/GoogleService-Info.plist)
  if(CS_ENV STREQUAL "prod")
    set(CS_BUNDLE_ID "com.cuckoostack.aerospheregames")
    set(CS_APP_NAME "Cuckoo Stack OVERDRIVE")
    set(CS_APPICON "AppIcon")
    set(CS_LINK_DOMAIN "cuckoostack-prod.web.app")
    set(CS_URL_SCHEME "cuckoostack")
  else()
    set(CS_BUNDLE_ID "com.cuckoostack.aerospheregames.staging")
    set(CS_APP_NAME "Cuckoo Stack OVERDRIVE β")
    set(CS_APPICON "AppIconStaging")
    set(CS_LINK_DOMAIN "cuckoostack-staging.web.app")
    set(CS_URL_SCHEME "cuckoostack-staging")
  endif()
  # challenge-link domain (src/core/Links.h) + a URL scheme for local testing (xcrun simctl openurl)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/CuckooStack.entitlements.in ${CMAKE_CURRENT_BINARY_DIR}/CuckooStack.entitlements @ONLY)
  set(CS_FIREBASE_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/config/firebase/${CS_ENV}/GoogleService-Info.plist)
  if(EXISTS ${CS_FIREBASE_PLIST})
    set(CS_FIREBASE_RES ${CS_FIREBASE_PLIST})
  else()
    message(WARNING "No ${CS_FIREBASE_PLIST}: this ${CS_ENV} build runs with Firebase off (README: Environments)")
    set(CS_FIREBASE_RES "")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/firebase)

  # Firebase iOS SDK (scripts/bootstrap_deps.sh): static xcframeworks, linked like AdMob below
  set(CS_FB ${TP}/Firebase)
  if(NOT EXISTS ${CS_FB}/FirebaseCore.xcframework)
    message(FATAL_ERROR "Missing third_party/Firebase. Run scripts/bootstrap_deps.sh.")
  endif()
  file(GLOB _fb_xcfs LIST_DIRECTORIES true ${CS_FB}/*.xcframework)
  set(CS_FB_DEVICE "")
  set(CS_FB_SIM "")
  set(CS_FB_LINK "")
  foreach(x ${_fb_xcfs})
    get_filename_component(_n ${x} NAME_WE)
    string(APPEND CS_FB_DEVICE " ${x}/ios-arm64")
    string(APPEND CS_FB_SIM " ${x}/ios-arm64_x86_64-simulator")
    string(APPEND CS_FB_LINK " -framework ${_n}")
  endforeach()
  # their privacy manifests must ship in the app bundle when they're linked statically
  file(GLOB CS_FB_PRIVACY LIST_DIRECTORIES true ${CS_FB}/*.xcframework/ios-arm64/*.framework/*_Privacy.bundle)
  set(CS_GMA ${TP}/GoogleMobileAds)
  foreach(xcf GoogleMobileAds UserMessagingPlatform)
    if(NOT EXISTS ${CS_GMA}/${xcf}.xcframework)
      message(FATAL_ERROR "Missing third_party/GoogleMobileAds/${xcf}.xcframework. Run scripts/bootstrap_deps.sh.")
    endif()
  endforeach()
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/skadnetworks.txt _skan REGEX "skadnetwork$")
  set(CS_SKADNETWORK_ITEMS "")
  foreach(id ${_skan})
    string(APPEND CS_SKADNETWORK_ITEMS "        <dict><key>SKAdNetworkIdentifier</key><string>${id}</string></dict>\n")
  endforeach()
  # the SDKs' privacy manifests must ship in the app bundle when they're linked statically
  set(CS_GMA_PRIVACY ${CS_GMA}/GoogleMobileAds.xcframework/ios-arm64/GoogleMobileAds.framework/PrivacyInfo.xcprivacy)
  set(CS_UMP_PRIVACY ${CS_GMA}/UserMessagingPlatform.xcframework/ios-arm64/UserMessagingPlatform.framework/PrivacyInfo.xcprivacy)
  add_executable(CuckooStack MACOSX_BUNDLE
    src/platform/ios/IOSAds.mm
    src/platform/ios/IOSApp.mm
    src/platform/ios/IOSAudio.mm
    src/platform/ios/IOSServices.mm
    src/platform/ios/IOSServices.swift
    src/platform/ios/IOSVideoRecorder.mm
    src/platform/ios/IOSWindow.mm
    ${CS_LAUNCH}
    ${CS_ASSETS}
    ${CS_GMA_PRIVACY}
    ${CS_UMP_PRIVACY}
    ${CS_FB_PRIVACY}
    ${CS_FIREBASE_RES})
  set_source_files_properties(${CS_GMA_PRIVACY} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/GoogleMobileAdsPrivacy.bundle")
  set_source_files_properties(${CS_UMP_PRIVACY} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/UserMessagingPlatformPrivacy.bundle")
  set_source_files_properties(${CS_LAUNCH} ${CS_ASSETS} ${CS_FB_PRIVACY} ${CS_FIREBASE_RES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_link_libraries(CuckooStack PRIVATE cs_graphics ${CS_MOLTENVK_XCFRAMEWORK}
    "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework IOSurface" "-framework Foundation"
    "-framework AVFoundation" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
    "-framework StoreKit" "-framework AppTrackingTransparency" "-framework AdSupport" "-framework JavaScriptCore"
    "-framework WebKit" "-framework SystemConfiguration" "-framework CoreTelephony" "-framework MessageUI"
    "-framework SafariServices" "-ObjC")
  target_compile_definitions(CuckooStack PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:CS_ADMOB_IOS_REWARDED=\"${CS_AD_ADMOB_IOS_REWARDED}\">"
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:CS_ADMOB_IOS_INTERSTITIAL=\"${CS_AD_ADMOB_IOS_INTERSTITIAL}\">"
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:CS_GC_DAILY=\"${BOARD_GAMECENTER_DAILY}\">"
    "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:CS_GC_ALLTIME=\"${BOARD_GAMECENTER_ALLTIME}\">")
  target_compile_options(CuckooStack PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${CS_WARNINGS};${CS_OPT};-fobjc-arc>")
  if(CS_ADS_PROD_MISSING)
    # a prod Release must never ship test ads: fail the build (Debug keeps working with test IDs)
    string(REPLACE ";" ", " _missing "${CS_ADS_PROD_MISSING}")
    add_custom_command(TARGET CuckooStack PRE_BUILD
      COMMAND "$<$<CONFIG:Release>:/bin/sh;-c;echo 'error: config/ads.env: set ${_missing} before a prod Release build' && exit 1>"
      COMMAND_EXPAND_LISTS VERBATIM)
  endif()
  set_target_properties(CuckooStack PROPERTIES
    OUTPUT_NAME "CuckooStack"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "${CS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "${CS_APP_NAME}"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "CuckooStack-Swift.h"
    XCODE_ATTRIBUTE_PRODUCT_MODULE_NAME "CuckooStack"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
    # AdMob app ID per configuration, expanded into Info.plist (GADApplicationIdentifier) by Xcode
    "XCODE_ATTRIBUTE_CS_ADMOB_APP_ID" "$<IF:$<STREQUAL:${CS_ENV},prod>,${ADS_ADMOB_IOS_APP_ID_PROD},${ADS_ADMOB_IOS_APP_ID_TEST}>"
    "XCODE_ATTRIBUTE_CS_ADMOB_APP_ID[variant=Debug]" "${ADS_ADMOB_IOS_APP_ID_TEST}"
    # static AdMob/UMP/Firebase xcframeworks: Xcode picks the device or simulator slice per SDK
    "XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS[sdk=iphoneos*]" "${CS_GMA}/GoogleMobileAds.xcframework/ios-arm64 ${CS_GMA}/UserMessagingPlatform.xcframework/ios-arm64${CS_FB_DEVICE}"
    "XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS[sdk=iphonesimulator*]" "${CS_GMA}/GoogleMobileAds.xcframework/ios-arm64_x86_64-simulator ${CS_GMA}/UserMessagingPlatform.xcframework/ios-arm64_x86_64-simulator${CS_FB_SIM}"
    XCODE_ATTRIBUTE_OTHER_LDFLAGS "-framework GoogleMobileAds -framework UserMessagingPlatform${CS_FB_LINK}"
    XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/$(PLATFORM_NAME) $(SDKROOT)/usr/lib/swift"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "${CS_APPICON}"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_BINARY_DIR}/CuckooStack.entitlements"
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CS_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_EMBED_FRAMEWORKS ${CS_MOLTENVK_XCFRAMEWORK}
    XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
    XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON)

  # The level editor (CuckooEditor): a separate app for iPhone and iPad, SwiftUI over the editor's core (cs_editor.h)
  # and the game itself for play-testing (EngineHost.mm); no ads, no Firebase. It syncs with tools/level_editor's
  # server on the Mac (--lan).
  option(CS_BUILD_EDITOR "Build the level editor app (CuckooEditor) for iPhone and iPad" ON)
  if(CS_BUILD_EDITOR)
    file(GLOB CS_EDITOR_SWIFT ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios_editor/*.swift)
    set(CS_EDITOR_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios_editor/Assets.xcassets) # (scripts/icon/make_editor_icon.py)
    add_executable(CuckooEditor MACOSX_BUNDLE
      ${CS_EDITOR_SWIFT}
      src/platform/ios_editor/EngineHost.mm
      src/platform/ios/IOSAudio.mm
      src/platform/ios/IOSWindow.mm
      ${CS_EDITOR_ASSETS})
    set_source_files_properties(${CS_EDITOR_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_link_libraries(CuckooEditor PRIVATE cs_editor cs_graphics ${CS_MOLTENVK_XCFRAMEWORK}
      "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework IOSurface" "-framework Foundation"
      "-framework AVFoundation" "-framework Network" "-framework WebKit")
    target_compile_options(CuckooEditor PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${CS_WARNINGS};${CS_OPT};-fobjc-arc>")
    set_target_properties(CuckooEditor PROPERTIES
      OUTPUT_NAME "CuckooEditor"
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platforms/ios_editor/Info.plist.in
      MACOSX_BUNDLE_GUI_IDENTIFIER "${CS_BUNDLE_ID}.editor"
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CS_BUNDLE_ID}.editor"
      MACOSX_BUNDLE_BUNDLE_NAME "Cuckoo Editor"
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_BUNDLE_VERSION "1"
      XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
      XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios_editor/EditorBridging.h"
      XCODE_ATTRIBUTE_PRODUCT_MODULE_NAME "CuckooEditor"
      XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
      XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
      XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
      XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
      XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/$(PLATFORM_NAME) $(SDKROOT)/usr/lib/swift"
      XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
      XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CS_IOS_TEAM_ID}"
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
      XCODE_EMBED_FRAMEWORKS ${CS_MOLTENVK_XCFRAMEWORK}
      XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
      XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON)

    # The editor's UI tests (tests/ui/ios_editor, XCUITest): they drive CuckooEditor on a simulator with --ui-test-reset (its
    # own empty data folder). Run: xcodebuild test -project build-ios/CuckooStack.xcodeproj -scheme CuckooEditor
    # -destination 'id=<a simulator of its own>' (README: Testing)
    file(GLOB CS_EDITOR_UITEST_SWIFT ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/ios_editor/*.swift)
    set(CS_EDITOR_UITEST_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/ios_editor/Fixtures/hurdles.txt)
    if(CS_EDITOR_UITEST_SWIFT)
      add_executable(CuckooEditorUITests MACOSX_BUNDLE ${CS_EDITOR_UITEST_SWIFT} ${CS_EDITOR_UITEST_FIXTURES})
      set_source_files_properties(${CS_EDITOR_UITEST_FIXTURES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
      target_link_libraries(CuckooEditorUITests PRIVATE "-framework XCTest" "-framework Foundation" "-framework UIKit")
      add_dependencies(CuckooEditorUITests CuckooEditor)
      set_target_properties(CuckooEditorUITests PROPERTIES
        OUTPUT_NAME "CuckooEditorUITests"
        XCODE_PRODUCT_TYPE "com.apple.product-type.bundle.ui-testing"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${CS_BUNDLE_ID}.editor.uitests"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CS_BUNDLE_ID}.editor.uitests"
        XCODE_ATTRIBUTE_WRAPPER_EXTENSION "xctest"
        XCODE_ATTRIBUTE_TEST_TARGET_NAME "CuckooEditor"
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CS_IOS_TEAM_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "$(PLATFORM_DIR)/Developer/Library/Frameworks"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks @loader_path/Frameworks $(PLATFORM_DIR)/Developer/Library/Frameworks"
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS "$(DEVELOPER_DIR)/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/$(PLATFORM_NAME) $(SDKROOT)/usr/lib/swift"
        XCODE_ATTRIBUTE_ENABLE_TESTING_SEARCH_PATHS YES)
    endif()
  endif()

else()
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
  add_executable(cuckoo_stack src/main.cpp src/platform/desktop/DesktopAudio.cpp)
  target_include_directories(cuckoo_stack SYSTEM PRIVATE ${TP}/miniaudio)
  target_link_libraries(cuckoo_stack PRIVATE cs_graphics glfw ${CMAKE_DL_LIBS})
  if(APPLE)
    target_link_libraries(cuckoo_stack PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
  elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(cuckoo_stack PRIVATE Threads::Threads m)
  endif()
  target_compile_options(cuckoo_stack PRIVATE ${CS_WARNINGS} ${CS_OPT})
endif()

# ---------------------------------------------------------------- tests (desktop only)
if(CS_PLATFORM STREQUAL "desktop")
  enable_testing()
  # Each test sits next to the file it tests: cs_test(src/core/Camera) builds src/core/Camera_test.cpp into
  # Camera_test, registered with ctest as "Camera". Links cs_core unless LIBS says otherwise; EXTRA adds sources;
  # tests/ (TestUtil.h) and src/ are on the include path; CS_SOURCE_DIR points at the checkout (for tests that read
  # project files); it runs in the build directory.
  function(cs_test path)
    cmake_parse_arguments(T "" "EXT" "LIBS;EXTRA" ${ARGN})
    get_filename_component(name ${path} NAME)
    if(NOT T_EXT)
      set(T_EXT cpp)
    endif()
    if(NOT T_LIBS)
      set(T_LIBS cs_core)
    endif()
    add_executable(${name}_test ${path}_test.${T_EXT} ${T_EXTRA})
    target_include_directories(${name}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${name}_test PRIVATE ${T_LIBS})
    target_compile_definitions(${name}_test PRIVATE CS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name}_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endfunction()

  foreach(f Campaign Daily Economy EndCard FileStorage Game GameSurge HenIdle Math Meta Monetization Reminders RenderList ReplayClip
            Services Tuning Difficulty Camera Materials HudStyle BeatClock Geometry Effects World HenModel Hen Font HudImage GameUi GameHud DroidModel GameDroid GameDev HudImg GameSaw GameLava Gauntlet GameGauntlet LevelData GameBlock CourseRules Hazards Chunk)
    cs_test(src/core/${f})
  endforeach()
  cs_test(src/core/LevelCatalog LIBS cs_validate) # (two games on two threads: BotRun)
  cs_test(src/core/ReportJson LIBS cs_validate) # (a chunk judged; the bot plays it: BotRun)
  foreach(f Level GameLaser GameSpring GameBounce LevelBeats) # (the generator's own courses, LevelBake; a bot's run, BotRun)
    cs_test(src/core/${f} LIBS cs_validate)
  endforeach()
  foreach(f EditorDoc LevelMerge BotJob GridDoc MazeGen)
    cs_test(src/editor/${f} LIBS cs_editor)
  endforeach()
  # the bot plays every level through (all 30, their gauntlets too): 3 minutes or so on a laptop, past the 120 s the
  # pre-commit hook and CI give a test by default (`ctest --timeout 120`), so these have their own (MazeGen: hundreds
  # of mazes generated, judged and played in the game, at every density and floor count)
  set_tests_properties(GameDev GameDroid LevelBeats MazeGen PROPERTIES TIMEOUT 600)
  target_include_directories(HudImg_test SYSTEM PRIVATE ${TP}/stb) # (stb_image: the PNGs, to compare with the atlas)
  foreach(f AudioParam Mp3 Music Synth)
    cs_test(src/core/audio/${f})
  endforeach()
  cs_test(src/graphics/BakedPatterns LIBS cs_graphics)
  cs_test(src/graphics/Quality)
  cs_test(src/graphics/ShaderBlobs LIBS cs_core cs_shaders)
  cs_test(src/graphics/RendererLogic)
  cs_test(src/platform/android/AndroidManifest)
  cs_test(src/platform/android/AndroidLogic)
  cs_test(src/platform/ios/ReplayAudioTap)
  cs_test(tests/polish) # the polish round: spans many files, so it stays in tests/
  if(APPLE) # the iOS video recorder runs on macOS too (VideoToolbox)
    cs_test(src/platform/ios/IOSVideoRecorder EXT mm EXTRA src/platform/ios/IOSVideoRecorder.mm
            LIBS "-framework Foundation" "-framework AVFoundation" "-framework VideoToolbox" "-framework CoreMedia"
                 "-framework CoreVideo" "-framework QuartzCore")
    target_compile_options(IOSVideoRecorder_test PRIVATE -fobjc-arc)
    # it records in real time (frames pushed at 30 fps, its clips measured against the clock): it runs alone, so the
    # other tests of a `ctest -j` can't starve its encoder and fail its speed checks
    set_tests_properties(IOSVideoRecorder PROPERTIES RUN_SERIAL TRUE)
  endif()
  add_executable(render_audio tests/render_audio.cpp)
  target_link_libraries(render_audio PRIVATE cs_core)
  # the gauntlet designer's checker (scripts/gauntlets): a gauntlet's way through, near misses and egg meter
  add_executable(gauntlet_check tests/gauntlet_check.cpp)
  target_include_directories(gauntlet_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(gauntlet_check PRIVATE cs_validate)
  # ...and the level designer's (assets/levels): a level's course checked, played by the bot; or baked by the generator
  add_executable(level_range tests/level_range.cpp) # (the generators' expressive range: scripts/levels/expressive_range.py)
  target_include_directories(level_range PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(level_range PRIVATE cs_core)
  add_executable(level_check tests/level_check.cpp)
  target_include_directories(level_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(level_check PRIVATE cs_editor) # (--refit: LevelMerge)
  # the asset scripts (scripts/tests): the droid mesh format, and the Daily Drop sprites and HUD atlas reproducing the
  # committed files (those image tests skip themselves without Pillow and numpy)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME scripts COMMAND ${Python3_EXECUTABLE} -W error::ResourceWarning -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests)
    # the level editor (tools/level_editor): its server's API, checking and saving levels and grids with the checkers
    add_test(NAME level_editor COMMAND ${Python3_EXECUTABLE} -W error::ResourceWarning -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools/level_editor/tests)
    set_tests_properties(level_editor PROPERTIES ENVIRONMENT LEVEL_EDITOR_BUILD=${CMAKE_CURRENT_BINARY_DIR})
  endif()
  # ...and its models, in Node (when there's one): a gauntlet's grid, a level's course, the history and its words; and
  # its wiki, which must document every feature (a tooltip with no section there fails)
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    set(_editor_js ${CMAKE_CURRENT_SOURCE_DIR}/tools/level_editor/tests)
    add_test(NAME level_editor_js COMMAND ${NODE_EXECUTABLE} --test ${_editor_js}/grid.test.mjs ${_editor_js}/course.test.mjs
             ${_editor_js}/history.test.mjs ${_editor_js}/wiki.test.mjs)
  endif()
  # the iPhone and iPad editor's rules apart from its looks (Foundation only: the rails' groups, what a group's button
  # wears, the generators' switches, the maze's cells), built with swiftc on a Mac and checked
  find_program(SWIFTC_EXECUTABLE swiftc)
  if(APPLE AND SWIFTC_EXECUTABLE)
    set(_ios_logic ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios_editor)
    set(_ios_logic_bin ${CMAKE_CURRENT_BINARY_DIR}/ios_editor_logic_test)
    add_test(NAME ios_editor_logic COMMAND sh -c "\"${SWIFTC_EXECUTABLE}\" -o \"${_ios_logic_bin}\" \"${_ios_logic}/ToolGrouping.swift\" \"${_ios_logic}/MazeCells.swift\" \"${_ios_logic}/GridNames.swift\" \"${_ios_logic}/ChunkLogic.swift\" \"${_ios_logic}/LaunchRequest.swift\" \"${CMAKE_CURRENT_SOURCE_DIR}/tests/swift/ios_editor/main.swift\" && \"${_ios_logic_bin}\"")
    # ...and the chunk store and the core's chunk calls (EdDoc, the chunk files: Chunk.h through cs_editor.h), with the core linked in
    set(_ios_chunks_bin ${CMAKE_CURRENT_BINARY_DIR}/ios_editor_chunks_test)
    add_test(NAME ios_editor_chunks COMMAND sh -c "\"${SWIFTC_EXECUTABLE}\" -import-objc-header \"${CMAKE_CURRENT_SOURCE_DIR}/src/editor/cs_editor.h\" -o \"${_ios_chunks_bin}\" \"${_ios_logic}/CoreBridge.swift\" \"${_ios_logic}/ChunkCore.swift\" \"${_ios_logic}/StoreBasics.swift\" \"${_ios_logic}/ChunkLogic.swift\" \"${_ios_logic}/ChunkStore.swift\" \"${_ios_logic}/ChunkSyncLogic.swift\" \"${_ios_logic}/SyncConflict.swift\" \"${_ios_logic}/Core.swift\" \"${_ios_logic}/Models.swift\" \"${_ios_logic}/LevelStore.swift\" \"${_ios_logic}/ChunkStoreApp.swift\" \"${_ios_logic}/LaunchRequest.swift\" \"${_ios_logic}/BackupModel.swift\" \"${_ios_logic}/ChunkModel.swift\" \"${_ios_logic}/MazeStorage.swift\" \"${_ios_logic}/ToolGrouping.swift\" \"${_ios_logic}/GridNames.swift\" \"${CMAKE_CURRENT_SOURCE_DIR}/tests/swift/ios_editor_chunks/main.swift\" $<TARGET_FILE:cs_editor> $<TARGET_FILE:cs_validate> $<TARGET_FILE:cs_core> $<TARGET_FILE:cs_level> $<TARGET_FILE:cs_fonts> -Xlinker -lc++ && \"${_ios_chunks_bin}\"")
  endif()
endif()
