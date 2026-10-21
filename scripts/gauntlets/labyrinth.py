"""Level 17, the Labyrinth (Neon Egypt; Hawara's): she chooses her way with her stack. At each fork she drops into a
passage with a bumper wall ahead: stacked over it, she goes on; short of it, she bounces back the way she came. One
way lives, the other ends on a bed of blades. Teach (stack to go on), test (stack exactly, under a roof), twist (back
is the way: don't stack). Up a zigzag of gated shafts first; 258 long.
- The hurdle run, one egg at most: a blade, then four hurdles a beat apart (a bumper one high to hop: an egg each,
  each one exact); a blade and a hurdle; the limbo: under the roof with no egg, an egg, over a hurdle, three times.
  Through a gate into a shaft: stacked 6, out over its LOW side, back the way she came.
- The pillared passage, left, two eggs at most: blades, a pillar to one egg over her before some, and after two of
  them a pillar to her head (the blade must take her only egg). Through a gate into a shaft: stacked 6, out right.
- The top: pillars while the meter fills. The sawtooth: a roof that follows teeth one high, room for one egg: up a
  tooth (one egg, exact), an egg on it, down onto a blade (its last egg), an egg, up the next; three times. A cube
  step 2 high with a cube saw on it (three eggs, the roof lets her carry no more). A low brink: in empty-handed.
- Fork 1, the teach: down into a passage; ahead a bumper 2 high (stack two and on); behind, blades.
- Fork 2, the test: down into a passage with room for two and no more; a bumper 2 high; behind, blades.
- Fork 3, the twist: down into a passage with room for two; a bumper 2 high with a bed of blades beyond it. Bounce
  back off it, and down the hole behind where she landed.
- The bottom, one egg at most: she turns off its end wall; two blades; the saw before the squeeze: room for two, a
  blade, and the roof down to her head (exactly one egg, or the roof or the blade gets her); the last drumroll, four
  hurdles a beat apart; out."""
from kit import Grid

NAME = "labyrinth"
LEVEL = 17


def build():
    g = Grid(258, 21)
    put = g.put
    # ---- the hurdle run: 3 high (rows 1-3: one egg at most), right from the door
    put(1, 3, 0, 95)
    g.set(12, 0, "S")
    for c in (18, 23, 28, 33):                  # four hurdles a beat apart
        g.set(c, 1, "B")
    g.set(46, 0, "S"); g.set(56, 1, "B")        # a blade, a hurdle
    for duck in (64, 71, 78):                   # the limbo: under the roof (no egg), an egg, over a hurdle
        put(3, 3, duck, duck + 1, "#")
        g.set(duck + 5, 1, "B")
    # shaft A: through gate R (col 96) into cols 97-100 (room for 6); its low side the gate and a bumper over it (6
    # high: out over it, back left), its far side a bumper to the ceiling
    put(1, 3, 96, 96, "R"); put(4, 6, 96, 96, "B")
    put(1, 8, 97, 100)
    put(1, 8, 101, 101, "B")
    # ---- the pillared passage: 4 high (rows 7-10: two eggs at most), left from col 96 to the gate at col 8
    put(7, 10, 9, 96)
    for blade, pillar in ((73, 78), (61, None), (47, 53), (35, None), (22, 27)):
        g.set(blade, 6, "S")
        if pillar: put(10, 10, pillar, pillar + 2, "#")  # a pillar to one egg over her, before it
    for blade in (73, 47):                      # past these, a pillar to her head: the blade must take her only egg
        put(9, 10, blade - 6, blade - 4, "#")
    # shaft B: through gate L (col 8, rows 7-10) into cols 3-7 (room for 6); its low side the gate and a bumper over
    # it (6 high: out over it, right), its far side a bumper to the ceiling
    put(7, 10, 8, 8, "L"); put(11, 12, 8, 8, "B")
    put(7, 14, 3, 7)
    put(7, 14, 2, 2, "B")
    # ---- the top: 6 high (her feet at row 13), right from col 8: pillars to one egg, to her head, to two eggs
    put(13, 18, 8, 57)
    put(16, 18, 22, 24, "#")
    put(15, 18, 36, 37, "#")
    put(17, 18, 48, 50, "#")
    # the sawtooth (cols 58-87): room for one egg over the floor and over each tooth (a cube block one high, 4 long),
    # a blade where she comes down off each (the roof over it as high as over the tooth: she steps off it with her egg)
    put(13, 15, 58, 87)
    for t in (62, 71, 80):
        put(13, 13, t, t + 3, "H")
        put(14, 16, t, t + 4)
        g.set(t + 4, 12, "S")
    put(13, 18, 88, 150)
    # a cube step 2 high with a cube saw where she lands on it: three eggs (the roof lets her carry no more)
    put(18, 18, 112, 120, "#")
    put(13, 14, 120, 125, "H"); g.set(120, 14, "C")
    put(15, 18, 140, 150, "#")                  # the brink: in empty-handed
    # ---- fork 1, the teach: down a hole (cols 149-150) four rows; ahead (cols 151-160, room for 3) a bumper 2 high,
    # then a passage on (room for 1) to the next hole; behind (cols 144-148, room for 1) blades, and a wall
    put(9, 12, 149, 150)
    put(9, 11, 144, 148); g.set(147, 8, "S"); g.set(145, 8, "S")
    put(9, 13, 151, 160)
    put(9, 10, 161, 161, "B")
    put(11, 13, 161, 168)
    # ---- fork 2, the test: down a hole (cols 167-168) four rows; ahead (cols 169-179) room for two and no more, a
    # bumper 2 high, a passage on; behind (cols 162-166) blades
    put(7, 10, 167, 168)
    put(7, 9, 162, 166); g.set(165, 6, "S"); g.set(163, 6, "S")
    put(7, 10, 169, 179)
    put(7, 8, 180, 180, "B")
    put(9, 11, 180, 187)
    # ---- fork 3, the twist: down a hole (cols 186-187) four rows; ahead (cols 188-195, room for 2) a bumper 2 high
    # with a bed of blades beyond it (cols 197-201) and a wall; behind, past a cell of floor, the way: a hole down
    put(5, 8, 186, 187)
    put(5, 7, 181, 185)
    put(5, 8, 188, 195)
    put(5, 6, 196, 196, "B"); put(7, 9, 196, 196)  # (open over it: stacked over it, she goes on, onto the blades)
    put(5, 9, 197, 201)
    for c in (197, 199, 201):
        g.set(c, 4, "S")
    put(1, 4, 182, 183)
    # ---- the bottom: 3 high (rows 1-3: one egg at most), from its end wall (col 171) right to the exit
    put(1, 3, 172, 257)
    put(1, 3, 171, 171, "B")
    g.set(194, 0, "S"); g.set(204, 0, "S")
    # the saw before the squeeze: room for two (cols 210-218), a blade at its end, the roof down to her head after it
    put(4, 4, 210, 218)
    g.set(218, 0, "S")
    put(3, 3, 219, 225, "#")
    for c in (234, 239, 244, 249):              # the last drumroll: hurdles a beat apart
        g.set(c, 1, "B")
    g.doors(1, 1)
    return g
