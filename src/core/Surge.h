// Surge mode: collect SURGE_NEED disco balls in a row to smash through walls for a few seconds.
// Every tuning value for the mode lives here.
#pragma once

namespace cs::surge {

// ---- trigger
constexpr int NEED = 10;                // balls in a row (7-8 makes surges a regular reward)
constexpr double MISS_BEHIND = 0.9;     // a ball more than this far behind the hen counts as missed (m)

// ---- surge rules
constexpr float TIME = 5.f;             // surge duration (s)
constexpr float GRACE = 0.7f;           // smashing stays on this long after the surge ends (s)
constexpr float SPEED_MUL = 1.35f;      // on top of difficulty + adaptive speed
constexpr double SMASH_AHEAD = 0.22;    // column probed in front of the hen (m)
constexpr double LEVEL_EPS = 0.05;      // level = floor(baseY / block + LEVEL_EPS)
constexpr int POINTS_PER_BLOCK = 1;
constexpr float MUSIC_BPM_CAP = 150.f;

// ---- smash feedback
constexpr int DEBRIS_PER_SMASH = 18;    // pooled block meshes per wall hit (sampled evenly if more broke)
constexpr int DEBRIS_POOL = 72;         // total pooled blocks (four overlapping smashes)
constexpr float DEBRIS_FWD = 0.55f;     // forward velocity = speed * FWD + rand(FWD_MIN, FWD_MAX)
constexpr float DEBRIS_FWD_MIN = 1.f, DEBRIS_FWD_MAX = 5.f;
constexpr float DEBRIS_UP_MIN = 3.f, DEBRIS_UP_MAX = 9.f;
constexpr float DEBRIS_SIDE = 5.f;      // sideways speed, sent away from the camera so debris never covers the hen
constexpr float DEBRIS_TOWARD_CAMERA = 0.6f; // the most it may drift toward the camera
constexpr float DEBRIS_SPIN = 12.f;
constexpr float DEBRIS_GRAVITY = 22.f;
constexpr float DEBRIS_BOUNCE = 0.35f;
constexpr float DEBRIS_LIFE_MIN = 1.f, DEBRIS_LIFE_MAX = 1.6f;
constexpr float DEBRIS_SHRINK = 0.4f;   // shrink-out over the last part of life (s)
constexpr int SMASH_PARTICLES = 46;
constexpr float SMASH_SHAKE = 0.28f;
constexpr float SMASH_KICK = 0.3f;      // backward camera kick
constexpr float SMASH_FREEZE = 0.03f;   // hit-pause (s)
constexpr float SMASH_CHROMA = 0.6f;
constexpr int SMASH_HAPTIC_MS = 12;

// ---- party mode (all scaled by partyK)
constexpr float PARTY_IN = 0.25f;       // ease-in time constant (s)
constexpr float PARTY_OUT = 0.6f;       // ease-out time constant (s)
constexpr float GIANT_BALL_SCALE = 5.f; // relative to a pickup ball
constexpr float GIANT_BALL_HEIGHT = 5.4f;   // above the camera's look-at point: top of the screen, out of the lane
constexpr float GIANT_BALL_AHEAD = 2.4f;    // ahead of the camera's look-at point (m)
constexpr float GIANT_BALL_Z = -4.f;        // behind the hen's lane (lane is z = 0)
constexpr float GIANT_BALL_DROP = 9.f;      // drops in from this far above
constexpr float GIANT_BALL_SPIN = 2.6f;     // rad/s
constexpr int LASERS = 12;
constexpr float LASER_WIDTH = 0.06f;        // thin: the hen must stay readable
constexpr float LASER_REACH = 14.f;         // horizontal sweep amplitude (m)
constexpr float LASER_BRIGHT = 0.55f;
constexpr int SPOTLIGHTS = 4;
constexpr float SPOT_ALPHA = 0.10f;         // faint additive cones
constexpr float CONFETTI_PER_S = 70.f;
constexpr int AFTERIMAGE_PER_FRAME = 3;
constexpr float FLOOR_STEP = 0.11f;         // rave floor pattern step (s)
constexpr float FOV_ADD = 9.f;              // degrees
constexpr float BLOOM_ADD = 0.12f;          // only slightly raised
constexpr float WINDOW_FLASH = 1.4f;        // extra window emissive on each kick
constexpr float HUE_SPEED = 0.35f;          // hue cycles per second
constexpr float VIGNETTE_TINT = 0.2f;

} // namespace cs::surge
