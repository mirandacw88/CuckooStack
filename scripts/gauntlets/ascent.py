"""Level 19, the Pharaoh's Ascent (Neon Egypt's finale): the Great Pyramid as it is, everything before it mirrored
and twisted. Its door is high on the face (row 13, up the course's steps), and its way goes down before it goes up.
- The Descending Passage: a breather, then twelve steps down under a roof that follows them, room for one egg: an egg
  each step, and the blade where she lands on the next takes it (CLOSE SHAVE x12, the Grand Gallery's mirror).
- The Subterranean Chamber, at the bottom, right: pillars while the meter fills.
- The long climb: through a gate into a shaft, stacked 12 (waiting on the meter as she bounces), out over its low side,
  back left.
- The Ascending Passage, left: a breather. The Grand Gallery reprise, left and up: four steps, a blade where she lands
  on every other one, the roof letting her carry two before those and one before the others: two eggs, then one (a
  PERFECT), then two, then one (tap-tap, tap).
- The Queen's corridor, left, one egg at most: blades, a pillar to her head past each.
- The King's Chamber: through a gate into a bay; out over its low side, 5 high, back right, onto a cube saw: six eggs,
  and the roof lets her carry no more. Stacked exactly to the wall (the perfect habit), the saw gets her.
- The top, right: pillars while the meter fills; two sacred wells (two deep, three deep); the lip of the air shaft,
  room for three eggs.
- Down the air shaft, 21 rows, onto three blades in a row under a roof that comes down as they take her eggs, one by
  one (the last one a CLOSE SHAVE), to her head; the last drumroll, three hurdles a beat apart; out."""
from kit import Grid, roofed_stairs, sacred_well

NAME = "ascent"
LEVEL = 19


def build():
    g = Grid(276, 30)
    put = g.put
    # ---- the entrance: high on the face (row 13), a breather 4 high while the meter fills, then the Descending Passage
    put(13, 16, 0, 39)
    put(16, 16, 12, 14, "#")                    # a pillar to one egg over her
    put(15, 16, 26, 27, "#")                    # a pillar to her head
    end, low = roofed_stairs(g, 40, 13, [8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6, 6], rise=-1, room=1, saws=range(1, 13))
    assert (end, low) == (130, 1), (end, low)
    # ---- the Subterranean Chamber: 6 high (rows 1-6), right from the passage's foot to the gate at col 229
    put(1, 6, end, 228)
    for c0, c1, r in ((142, 144, 4), (158, 160, 3), (174, 176, 4), (190, 192, 3), (206, 208, 4)):
        put(r, 6, c0, c1, "#")                  # pillars, to one egg over her and to her head
    # the long climb: through gate R (col 229) into cols 230-233 (room for 12); its low side the gate and bumpers over
    # it (12 high: out over it, back left), its far side a bumper to the ceiling
    put(1, 3, 229, 229, "R"); put(4, 12, 229, 229, "B")
    put(1, 14, 230, 233)
    put(1, 14, 234, 234, "B")
    # ---- the Ascending Passage: 4 high (her feet at row 13), left from the shaft, a pillar; then the Grand Gallery
    # reprise, left and up: four steps, a blade where she lands on the first and third; room for two before those
    # (one for the step, one for the blade), for one before the others (exact)
    put(13, 16, 168, 229)
    put(16, 16, 196, 198, "#")
    end, top = roofed_stairs(g, 167, 13, [6, 6, 5, 6, 5], rise=1, room=2, saws=(1, 3), way=-1)
    assert (end, top) == (139, 17), (end, top)
    put(17, 17, 156, 161, "#"); put(19, 19, 145, 150, "#")
    # ---- the Queen's corridor: 3 high (rows 17-19: one egg at most), left to the King's Chamber's gate (col 70)
    put(17, 19, 71, end)
    for blade in (130, 116, 102, 88):           # a blade, a pillar to her head two cells past it
        g.set(blade, 16, "S")
        put(19, 19, blade - 4, blade - 3, "#")
    # the King's Chamber: through gate L (col 70) into cols 61-69 (room for 6); its far side (col 60) a bumper to the
    # ceiling; its low side the gate and bumpers over it (5 high: out over it, right), a cube saw just past it
    put(17, 19, 70, 70, "L"); put(20, 21, 70, 70, "B")
    put(17, 24, 61, 69)
    put(17, 24, 60, 60, "B")
    g.set(71, 21, "C")
    # ---- the top: 6 high (her feet at row 22), right from col 70 to the air shaft (cols 244-245): pillars while the
    # meter fills, two sacred wells (two deep: carry three; three deep: carry four), a pillar, the lip (room for three)
    put(22, 27, 70, 245)
    for c0, c1, r in ((100, 102, 25), (128, 130, 24), (150, 152, 25), (214, 216, 24)):
        put(r, 27, c0, c1, "#")
    sacred_well(g, 172, 22, 2, 164)
    sacred_well(g, 196, 22, 3, 188)
    put(27, 27, 230, 245, "#")
    # ---- the air shaft: down from the top at cols 244-245 to the bottom
    put(1, 21, 244, 245)
    # the bottom: three blades in a row where she lands, under a roof that comes down as they take her eggs (room for
    # two over the first two cells past the landing, one over the next two, none past the last blade); the last
    # drumroll, hurdles a beat apart; out
    put(1, 3, 246, 275)
    put(4, 4, 246, 248)
    g.set(244, 0, "S"); g.set(246, 0, "S"); g.set(248, 0, "S")
    put(3, 3, 249, 254, "#")
    for c in (260, 265, 270):
        g.set(c, 1, "B")
    g.doors(13, 1)
    return g
