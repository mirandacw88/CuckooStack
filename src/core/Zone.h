// The campaign's zones: each band of levels has its own scenery (World.cpp), colour mood (Game.cpp grade()) and
// soundtrack (Music.h). Levels 1-10 play in the neon city, 11-20 in Neon Egypt (pyramids and obelisks lit like Tron),
// 21-30 in Neon Japan (pagodas, torii and cherry trees, blossoms blowing through it all); later levels return to the
// city until a later zone exists.
#pragma once

namespace cs {

enum class Zone { City, Egypt, Japan };

inline Zone zoneOf(int level) {
    if (level >= 11 && level <= 20) return Zone::Egypt;
    if (level >= 21 && level <= 30) return Zone::Japan;
    return Zone::City;
}
// its soundtrack (MusicTracks.inc order): 0 the city's (Midnight City Sprint), 1 Giza Overdrive, 2 Katana Overdrive
// (a track that isn't built in falls back to 0)
inline int zoneTrack(Zone z) { return z == Zone::Egypt ? 1 : z == Zone::Japan ? 2 : 0; }

} // namespace cs
