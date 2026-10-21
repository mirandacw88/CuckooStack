"""Level 13, the Grand Gallery (Neon Egypt): inside the Great Pyramid, five floors in a C, 212 long, up to its King's
Chamber and down its Well Shaft. Its signature: the Grand Gallery, a staircase 15 steps high under a roof that follows
it, so close over her head that she climbs it one egg at a time: tap once a step, never twice (two eggs hit the roof,
none hits the step). The beat quickens as she climbs; every climb is exact (PERFECT x1 ... x15).
- The Entrance Passage, 3 high (one egg at most), right from the door: five saws, closer and closer, each taking her
  only egg (CLOSE SHAVE x5); three granite plugs to hop, an egg each, her head brushing the roof over them; a cube step
  with no room to spare. Through a gate into a shaft: stacked 5 high.
- The Antechamber, 6 high, left: three portcullises hanging to her head (no egg under them) while the meter fills.
- The Grand Gallery, left and up: 15 steps, treads of 8 cells (steps 1-5), 7 (6-10), 6 (11-15).
- The King's Chamber, through a gate at the top: stacked 4 over it into the Relieving Chambers, right along the top:
  the twist, a cube block with a cube saw (three eggs: the one-tap habit fails); a low chamber with three saws one at
  a time; two saws back to back with room for two and no more; a portcullis; the lip of the Well Shaft, where she
  carries one egg and no more.
- Down the Well Shaft, 24 rows, landing on a blade (the egg she carried down sawn under her); a cube step with no room
  to spare; out."""
from kit import Grid, roofed_stairs

NAME = "gallery"
LEVEL = 13


def build():
    g = Grid(212, 32)
    put = g.put
    # ---- the Entrance Passage: 3 high (rows 1-3), right from the door to the gate at col 179
    put(1, 3, 0, 178)
    for c in (12, 23, 32, 40, 47):              # five saws, closer and closer: one egg each
        g.set(c, 0, "S")
    for c in (64, 82, 100):                     # the granite plugs: one high, the roof right over her on top
        put(1, 1, c, c + 3, "#")
    put(4, 4, 138, 152)                         # the passage 4 high here: a cube step 2 high, no room to spare on it
    put(1, 2, 145, 149, "H")
    # shaft A: through gate R (col 179, rows 1-3) into cols 180-183 (rows 1-11); out over col 179 (5 eggs) leftward
    put(1, 3, 179, 179, "R"); put(4, 5, 179, 179, "B")
    put(1, 11, 180, 183); put(1, 11, 184, 184, "B")
    # ---- the Antechamber: 6 high (rows 6-11), left from col 178 to the Gallery's foot at col 119
    put(6, 11, 120, 179)
    for c in (165, 149, 133):                   # portcullises hanging to her head (no egg under them)
        put(8, 11, c, c + 2, "#")
    # ---- the Grand Gallery: left and up from col 119, 15 steps (a foot, then treads 8, 7 and 6 long)
    end, top = roofed_stairs(g, 119, 6, [6] + [8] * 5 + [7] * 5 + [6] * 5, rise=1, room=1, way=-1)
    assert end == 8 and top == 21, (end, top)
    # ---- the King's Chamber: through gate L (col 8, rows 21-23) into cols 1-7 (rows 21-28); out over col 8 (4 eggs)
    put(21, 23, 8, 8, "L"); put(24, 24, 8, 8, "B")
    put(21, 28, 1, 7)
    put(4, 30, 0, 0, "B")                       # the left wall above the door: bumpers
    # ---- the Relieving Chambers: 6 high (rows 25-30), right from col 9 to the Well Shaft at col 190
    put(25, 30, 8, 189)
    put(25, 26, 50, 54, "H"); g.set(52, 26, "C")  # the twist: a cube block 2 high, a cube saw on top (three eggs)
    put(28, 30, 70, 100, "#")                   # a low chamber (3 high: one egg at most)...
    for c in (78, 88, 97):
        g.set(c, 24, "S")                       # ...three saws, one at a time
    put(29, 30, 105, 130, "#")                  # 4 high (two eggs at most)...
    for c in (115, 117):
        g.set(c, 24, "S")                       # ...over two saws back to back: no room to spare
    put(27, 30, 145, 147, "#")                  # a portcullis (no egg under it)
    put(28, 30, 178, 189, "#")                  # the lip: one egg at most
    # ---- the Well Shaft: down from the Relieving Chambers' floor (row 24) at cols 190-192 to the bottom
    put(1, 27, 190, 192)                        # (open up to the lip's roof: she runs off the edge into it)
    put(25, 30, 193, 193, "B")
    # ---- the bottom: 3 high (rows 1-3), right from the Well Shaft's foot to the exit
    put(1, 3, 186, 211)
    g.set(190, 0, "S")                          # where she lands: a blade (the egg she carried down, sawn)
    put(4, 4, 196, 206)                         # 4 high here: a cube step 2 high, no room to spare on it
    put(1, 2, 200, 203, "H")
    g.doors(1, 1)
    return g
