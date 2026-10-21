"""Level 15, the Sacred Wells (Neon Egypt): stack before you fall. Her stack falls with her, and every well has a blade
at its foot, so she stacks on the brink, under a roof that lets her carry exactly what the well will take. Three
tiers, 234 long: right along the top, down the turnstile, left along the middle, down a well, right along the bottom.
The door is high (row 6), up the course's steps.
- In: a low passage, a blade (one egg). Through a gate into a shaft: stacked 9 over its far wall.
- The top tier, right. Pit 1: the brink lets her carry one egg; two rows down onto a blade (CLOSE SHAVE); an egg
  laid in the pit climbs its rim (PERFECT). Pit 2: the brink lets her carry two; three rows down onto two blades,
  room for one between them (CLOSE SHAVE); an egg climbs out. A block hanging to her head on the way (no egg under it).
- The three sacred wells, one, two and three deep: carry one more than the well is deep (the brink's roof lets her
  carry no more), the blade at its foot takes one, and the rest climb its far wall exactly (PERFECT each).
- The turnstile: down seven rows to the middle tier, on over a blade to a bumper, and back over the same blade: an egg
  each way, in a passage with room for one (CLOSE SHAVE twice).
- The middle tier, left, one egg at most: blades, each with a block hanging to her head just past it (tap after the
  block, never before); two blades close together; three closer still. Down a well at its end.
- The bottom, right, one egg at most: she turns off its end wall and runs the hurdles (a blade, then a bumper one
  high to hop: an egg each, the second laid between them), closer and closer; a low passage; out."""
from kit import Grid, sacred_well

NAME = "wells"
LEVEL = 15


def build():
    g = Grid(234, 23)
    put = g.put
    # ---- in: 3 high (rows 6-8), right from the door (row 6)
    put(6, 8, 0, 24)
    g.set(13, 5, "S")                           # a blade: one egg
    # shaft 1: through gate R (col 25, rows 6-8) into cols 26-29 (room for 9); its far wall a bumper 9 high (col 30)
    put(6, 8, 25, 25, "R"); put(9, 17, 25, 25, "B")
    put(6, 16, 26, 29)
    put(6, 14, 30, 30, "B")
    # ---- the top tier: T1, 6 high (her feet at row 15), right from col 30
    put(15, 20, 30, 62)
    put(18, 20, 38, 41, "#")                    # hanging to one egg over her
    # pit 1: the brink (cols 48-62) lets her carry one; dug 2 deep at cols 60-62, a blade where she lands, a rim 1 high
    put(18, 20, 48, 62, "#")
    put(13, 14, 60, 62)
    g.set(60, 12, "S")
    # T2, feet at row 14, right from col 63
    put(14, 19, 63, 108)
    put(16, 19, 75, 77, "#")                    # hanging to her head: no egg under it
    # pit 2: the brink (cols 96-108) lets her carry two; dug 3 deep at cols 108-113, a blade where she lands and one
    # two on, a block over the pit past the landing (room for one), a rim 1 high
    put(18, 19, 96, 108, "#")
    put(11, 13, 108, 113)
    put(14, 19, 109, 113, "#")
    g.set(108, 10, "S"); g.set(110, 10, "S")
    # T3, feet at row 12, right from col 114
    put(12, 17, 114, 215)
    put(15, 17, 122, 124, "#")                  # hanging to one egg over her
    sacred_well(g, 142, 12, 1, 132)             # well A: carry two
    sacred_well(g, 170, 12, 2, 160)             # well B: carry three
    sacred_well(g, 198, 12, 3, 186)             # well C: carry four (T3's own roof)
    # ---- the turnstile: the brink (cols 206-215) lets her carry one; down a hole (cols 214-215) seven rows into the
    # middle tier's end: on over a blade (col 217) to a bumper (col 221), back over it, and left along the middle tier
    put(15, 17, 206, 215, "#")
    put(8, 11, 214, 215)
    put(5, 7, 214, 220)
    g.set(217, 4, "S")
    put(5, 7, 221, 221, "B")
    # ---- the middle tier: 3 high (rows 5-7: one egg at most), left from col 213 to the well at cols 33-34
    put(5, 7, 33, 213)
    for blade in (196, 182, 168):               # a blade, then a block hanging to her head just past it (her way)
        g.set(blade, 4, "S")
        put(7, 7, blade - 7, blade - 5, "#")
    g.set(150, 4, "S"); g.set(146, 4, "S")      # two close together
    for blade in (130, 126, 122):               # three closer still
        g.set(blade, 4, "S")
    put(7, 7, 100, 103, "#"); put(7, 7, 70, 72, "#")
    put(1, 4, 33, 34)                           # the well down to the bottom
    # ---- the bottom: 3 high (rows 1-3: one egg at most), from its end wall (col 31) right to the exit
    put(1, 3, 32, 233)
    put(1, 3, 31, 31, "B")
    for blade, hurdle in ((58, 62), (74, 78), (89, 93), (103, 107), (116, 120), (128, 131)):
        g.set(blade, 0, "S")                    # a blade...
        g.set(hurdle, 1, "B")                   # ...then a hurdle: a bumper one high, hopped with an egg
    put(3, 3, 150, 156, "#")                    # a low passage: no egg
    put(3, 3, 175, 180, "#")
    for blade, hurdle in ((192, 195), (204, 207), (216, 219)):
        g.set(blade, 0, "S"); g.set(hurdle, 1, "B")
    g.doors(6, 1)
    return g
