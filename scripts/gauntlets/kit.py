"""The gauntlet designer's kit: a grid of cells (src/core/Gauntlet.h) written bottom up, row 0 at the bottom as the game
reads it, and printed top first; and the patterns the gauntlets are built from.

Cells: '#' stone, 'H' cyber cube, 'B' bumper, 'R' / 'L' one-way gate (passed running right / left), 'S' a saw in
stone, 'C' a saw in a cube, '(' / ')' stone with a wall laser in its left / right face, '~' lava, '^' a spring (a floor she is
launched off: up 5 rows and on, Gauntlet::flightFrom), '>' the entry (left edge), 'X' the exit (right edge), '.' air. One cell is one egg high.
The hen's body fills two cells over her feet (Gauntlet::kBodyRows): a room `room` eggs high has air for
2 + room rows, its roof right over that.
"""

BODY = 2  # Gauntlet::kBodyRows


class Grid:
    def __init__(self, cols, rows, fill="#"):
        self.cols, self.rows = cols, rows
        self.cells = [[fill] * cols for _ in range(rows)]

    def put(self, r0, r1, c0, c1, ch="."):
        """fill rows r0..r1 and columns c0..c1 (both inclusive, either order) with ch"""
        for r in range(min(r0, r1), max(r0, r1) + 1):
            for c in range(min(c0, c1), max(c0, c1) + 1):
                self.cells[r][c] = ch

    def set(self, c, r, ch):
        self.cells[r][c] = ch

    def get(self, c, r):
        return self.cells[r][c]

    def doors(self, entry_row, exit_row):
        self.cells[entry_row][0] = ">"
        self.cells[exit_row][self.cols - 1] = "X"

    def rows_top_first(self):
        return ["".join(self.cells[r]) for r in range(self.rows - 1, -1, -1)]


def roofed_stairs(g, c, feet, treads, rise=1, room=1, saws=(), cube=False, way=1):
    """A staircase under a roof that follows it, run the way `way` (1 right, -1 left) from column c: tread k (k = 0 ..
    len(treads) - 1) is treads[k] cells long, her feet at row feet + rise * k, with air for her body and `room` eggs over
    it and the roof right over that (so `room` eggs is all she can carry there). rise 1 climbs (one egg climbs each
    step, knocked off as she climbs it); rise -1 descends (she drops a step at its end). saws: the treads with a blade
    in the floor of their first cell her way (where she lands on it). cube: the treads' floors and the steps are cyber
    cubes (a saw in one is a cube saw). Going down, each tread's first cell has the tread above's headroom: she steps
    off with her stack before she drops (Gauntlet::step looks at her body there first). Returns the column after the
    last tread (her way) and her feet's row there."""
    x = c
    for k, t in enumerate(treads):
        f = feet + rise * k
        x0, x1 = (x, x + t - 1) if way > 0 else (x - t + 1, x)
        g.put(f, f + BODY + room - 1, x0, x1)
        if rise < 0 and k > 0:
            g.put(f, f + BODY + room - 1 - rise, x, x)
        if cube:
            g.put(f - 1, f - 1, x0, x1, "H")
        if k in saws:
            g.set(x, f - 1, "C" if cube else "S")
        x += way * t
    return x, feet + rise * (len(treads) - 1)


def sacred_well(g, c, feet, depth, brink_from, room_over=None):
    """a sacred well in a floor 6 high, her feet at row `feet`, running right: cols c..c+2 dug `depth` deep, a blade where
    she lands (col c), a block hanging over c+1..c+2 so she can't stack in it, its far wall (col c+3) `depth` high; the
    brink's roof from col brink_from to c lets her carry exactly depth + 1 (or room_over, if given)"""
    floor = feet - depth                      # her feet in the well
    g.put(floor, feet - 1, c, c + 2)          # dug out
    g.set(c, floor - 1, "S")                  # the blade at its foot, where she lands
    g.put(floor + depth + 2, feet + 5, c + 1, c + 2, "#")  # the block over the rest: room for `depth` eggs, no more
    room = depth + 1 if room_over is None else room_over
    if room < 4:                              # the brink's roof (room for 4: the floor's own)
        g.put(feet + room + 2, feet + 5, brink_from, c, "#")
