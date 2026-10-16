// A finger (or the mouse button) as every platform reports it, for Game::touch(): down, moved while down, lifted (or
// taken away by the system). The platforms only translate their events into these; what they do is the game's.
#pragma once

namespace cs {

enum class TouchPhase { Began, Moved, Ended, Cancelled };

// the desktop mouse wheel: one notch (dy = 1, up) scrolls a list 40 points toward its top
inline float wheelToScroll(double dy) { return float(-dy) * 40.f; }

} // namespace cs
