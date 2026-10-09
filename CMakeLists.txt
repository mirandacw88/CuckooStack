# Cuckoo Stack: one build for Android (Gradle externalNativeBuild), iOS (Xcode generator) and desktop.
#
#   Desktop : cmake -S . -B build -G Ninja && cmake --build build && ./build/cuckoo_stack
#   iOS     : platforms/ios/generate_xcode.sh   (CMake -G Xcode, MoltenVK.xcframework from the Vulkan SDK)
#   Android : cd platforms/android && ./gradlew assembleDebug   (Gradle drives this file through the NDK)
cmake_minimum_required(VERSION 3.22)
project(CuckooStack VERSION 1.0.0 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(CS_PLATFORM ios)
  cmake_minimum_required(VERSION 3.28) # xcframework linking/embedding
  enable_language(OBJCXX)
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
foreach(pair "display:CuckooDisplay-Black" "body:ChakraPetch-Medium" "body_bold:ChakraPetch-Bold" "japanese:NotoSansJP-Bold-signs")
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
# the hen model (scripts/models/build_hen_mesh.py), embedded the same way
set(_hen_mesh ${CMAKE_CURRENT_SOURCE_DIR}/assets/models/hen.mesh)
set(_hen_cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts/hen_mesh.cpp)
add_custom_command(OUTPUT ${_hen_cpp}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_hen_mesh} -DOUTPUT=${_hen_cpp} -DSYMBOL=hen_mesh -DNAMESPACE=cs::assets -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${_hen_mesh} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake
  COMMENT "Embedding the hen model"
  VERBATIM)
list(APPEND CS_FONT_SOURCES ${_hen_cpp})
add_library(cs_fonts STATIC ${CS_FONT_SOURCES})

# ---------------------------------------------------------------- core: game logic, OS-free
add_library(cs_core STATIC
  src/core/Camera.cpp
  src/core/Daily.cpp
  src/core/Difficulty.cpp
  src/core/Economy.cpp
  src/core/HenModel.cpp
  src/core/EndCard.cpp
  src/core/Effects.cpp
  src/core/FileStorage.cpp
  src/core/Font.cpp
  src/core/Game.cpp
  src/core/GameHud.cpp
  src/core/GameSurge.cpp
  src/core/GameUi.cpp
  src/core/HudImage.cpp
  src/core/Geometry.cpp
  src/core/Hen.cpp
  src/core/Level.cpp
  src/core/Meta.cpp
  src/core/Monetization.cpp
  src/core/Reminders.cpp
  src/core/Tuning.cpp
  src/core/World.cpp
  src/core/audio/Synth.cpp)
target_include_directories(cs_core PUBLIC src)
target_include_directories(cs_core SYSTEM PRIVATE ${TP}/stb)
target_link_libraries(cs_core PUBLIC glm PRIVATE cs_fonts)
# the SDF atlas bake (stb_truetype) runs at startup: keep it optimised even in Debug builds
set_source_files_properties(src/core/Font.cpp PROPERTIES COMPILE_OPTIONS "-O2")
target_compile_options(cs_core PRIVATE ${CS_WARNINGS} ${CS_OPT})

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
    set(CS_APP_NAME "Cuckoo Stack")
    set(CS_APPICON "AppIcon")
    set(CS_LINK_DOMAIN "cuckoostack-prod.web.app")
    set(CS_URL_SCHEME "cuckoostack")
  else()
    set(CS_BUNDLE_ID "com.cuckoostack.aerospheregames.staging")
    set(CS_APP_NAME "Cuckoo Stack β")
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
    "-framework AVFoundation"
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
  add_executable(level_parity tests/level_parity.cpp)
  target_link_libraries(level_parity PRIVATE cs_core)
  foreach(t monetization economy daily)
    add_executable(${t}_test tests/${t}_test.cpp)
    target_link_libraries(${t}_test PRIVATE cs_core)
    target_compile_definitions(${t}_test PRIVATE CS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${t} COMMAND ${t}_test)
  endforeach()
  add_executable(surge_test tests/surge_test.cpp)
  target_link_libraries(surge_test PRIVATE cs_core)
  add_test(NAME surge COMMAND surge_test)
  add_executable(render_audio tests/render_audio.cpp)
  target_link_libraries(render_audio PRIVATE cs_core)
  find_program(NODE node)
  if(NODE)
    add_test(NAME level_parity COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_level_parity.sh)
  endif()
endif()
