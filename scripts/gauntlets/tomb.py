"""Level 11, the tomb (Neon Egypt): five floors from 4 to 15 high, 168 long, two shafts and a bay. After every climb
(where the meter runs dry) a breather of free squeezes, then eggs no faster than the meter refills.
- The door shuts behind her. The tunnel, 4 high: a saw; the roof dips (one egg under it); two saws back to back; the
  roof dips again over a saw. Through a gate into a shaft: stacked 5 high over its wall.
- Left along the middle floor, 6 high, under blocks hanging from its roof (no egg under the first, one under the
  second), a saw past each. Through a gate into the second shaft: stacked 8 high, into
- the great hall, 15 high: pillars hanging from its roof (one egg under the first, three, then none), two saws, then
  five eggs stacked fast past the last pillar for a cube wall 4 high with a cube saw on top; a gate the hall's full
  height into a bay, stacked 9 high over its bumper wall; cube stairs; two saws.
- Down a deep shaft, then left along the lower floor, 6 high: a cube step with a cube saw on top, two saws back to
  back under a lowered roof (no room to spare), a stone step with a saw in its top; down a hole.
- Out along the bottom hall, 4 high: saws, two back to back, a cube step 2 high (no room to spare), a saw under a dip
  in the roof, two back to back by the exit."""
from kit import Grid

NAME = "tomb"
LEVEL = 11


def build():
    g = Grid(168, 30)
    put = g.put
    # ---- the left tower, cols 0..47
    # the tunnel: 4 high (rows 1-4), right from the door to the gate at col 42; its roof dips (one egg at most)
    put(1, 4, 0, 41)
    g.set(12, 0, "S")                           # a saw
    put(4, 4, 18, 24, "#")                      # the roof dips
    for c in (33, 35):
        g.set(c, 0, "S")                        # two saws back to back (two eggs: room for them past the dip)
    put(4, 4, 37, 41, "#"); g.set(39, 0, "S")   # the roof dips again, a saw under it
    # shaft 1: through gate R (col 42, rows 1-4) into cols 43-46 (rows 1-11); out over col 42 (5 eggs) onto the middle
    put(1, 4, 42, 42, "R"); put(5, 5, 42, 42, "B")
    put(1, 11, 43, 46); put(1, 11, 47, 47, "B")
    # the middle floor, the squeeze: 6 high (rows 6-11), left from col 42 to the gate at col 4
    put(6, 11, 5, 42)
    put(8, 11, 30, 33, "#")                     # hanging to her head: no egg under it
    g.set(22, 5, "S")
    put(9, 11, 16, 19, "#")                     # one egg at most
    g.set(11, 5, "S")
    # shaft 2: through gate L (col 4, rows 6-11) into cols 1-3 (rows 6-13); out over col 4 (8 eggs) into the hall
    put(6, 11, 4, 4, "L"); put(12, 13, 4, 4, "B")
    put(6, 13, 1, 3)
    put(5, 28, 0, 0, "B")                       # the left wall above the door: bumpers
    # ---- the great hall: 15 high (rows 14-28), right from col 1 to the drop shaft at cols 161-163
    put(14, 28, 1, 163)
    put(17, 28, 12, 15, "#")                    # hanging pillars: one egg at most,
    put(19, 28, 22, 25, "#")                    # ...three
    for c in (32, 38):
        g.set(c, 13, "S")
    put(16, 28, 57, 60, "#")                    # ...none: then stack five fast for
    put(14, 17, 85, 89, "H"); g.set(87, 17, "C")  # a cube wall 4 high, a cube saw on top
    put(14, 28, 96, 96, "R")                    # a gate the hall's full height: no way back
    put(14, 22, 102, 102, "B")                  # ...and the bay's far wall, a bumper 9 high: stack 9 to get over it
    put(14, 14, 116, 126, "H"); put(14, 15, 127, 137, "H"); put(14, 16, 138, 148, "H")  # cube stairs 1, 2, 3
    for c in (152, 158):
        g.set(c, 13, "S")
    put(12, 13, 161, 163)                       # the drop shaft
    put(14, 28, 164, 164, "B")
    # ---- the right tower (the descent), cols 48..167, under the hall
    # the lower floor: 6 high (rows 6-11), left from the shaft's foot (turning off col 167) to the hole at cols 51-52
    put(6, 11, 49, 166); put(6, 11, 167, 167, "B")
    put(6, 7, 130, 134, "H"); g.set(132, 7, "C")  # a cube step 2 high, a cube saw on top
    put(10, 11, 100, 118, "#")                  # the roof comes down (two eggs at most)
    for c in (108, 106):
        g.set(c, 5, "S")                        # ...over two saws back to back
    put(6, 7, 68, 72, "#"); g.set(70, 7, "S")   # a stone step 2 high, a saw in its top
    put(5, 5, 51, 52)                           # the hole down to the bottom hall
    put(6, 11, 48, 48, "B")
    # the bottom hall: 4 high (rows 1-4), right from the hole's foot (turning off col 48) to the exit
    put(1, 4, 49, 166); put(1, 4, 48, 48, "B")
    g.set(66, 0, "S")
    for c in (84, 86):
        g.set(c, 0, "S")
    g.set(104, 0, "S")
    put(1, 2, 122, 125, "H")                    # a cube step 2 high: no room to spare
    put(4, 4, 132, 140, "#"); g.set(136, 0, "S")  # the roof dips, a saw under it
    for c in (150, 152):
        g.set(c, 0, "S")
    put(1, 2, 167, 167)                         # the exit's door
    g.doors(1, 1)
    return g
