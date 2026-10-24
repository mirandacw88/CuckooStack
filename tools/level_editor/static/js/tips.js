// Every description the level editor's tooltips show (tooltip.js), in one place: each tool, wall kind, grid cell,
// overlay, button and hazard. The wiki (tools/level_editor/wiki) has a section for each key (id "tip-<key>"), and
// tests/wiki.test.mjs checks it: add a feature, add its tip here and its section there.
import { esc } from './util.js';

export const TIPS = {
  // the two views
  'view.levels': { title: 'Levels', key: '⇧G', text: 'A level’s whole course: its walls, hazards, finish line and settings, and the maze gauntlet it plays.' },
  'view.gauntlets': { title: 'Gauntlets', key: '⇧G', text: 'A maze gauntlet’s grid, cell by cell, checked as the level laying it plays it.' },
  // the top bar
  'top.undo': { title: 'Undo', key: '⌘Z', text: 'Takes back the last edit to what’s on screen (the History card lists them).' },
  'top.redo': { title: 'Redo', key: '⇧⌘Z', text: 'Makes an undone edit again.' },
  'top.unsaved': { title: 'Unsaved edits', text: 'How many levels and grids have edits not saved yet. Click to see them: revert some, or save them all.' },
  'top.revert': { title: 'Revert', text: 'Lists every unsaved edit, the ones on screen ticked, and takes the ticked ones back to what’s saved. Undo brings them back.' },
  'top.save': { title: 'Save', key: '⌘S', text: 'Saves every edit together into the game’s files (assets/levels, assets/gauntlets) and its two layouts files. Rebuild the game to ship them.' },
  'top.play': { title: 'Play', text: 'Opens the desktop game on this level as edited, saved or not, and you play: tap or Space lays an egg. From the start point if one is set.' },
  'top.watch': { title: 'Watch the bot', text: 'The desktop game, the bot playing this level as edited, from the start point if one is set.' },
  'top.start': { title: 'Start point', text: 'Where Play and Watch the bot start (the Start tool, T, sets it). ✕: from the level’s beginning again (in the Gauntlets view, the gauntlet’s door).' },
  'top.sim': { title: 'Simulator', text: 'Builds the iOS app with what’s saved and installs it on every running iOS Simulator. Takes a few minutes.' },
  'top.help': { title: 'Wiki', text: 'This editor’s documentation: every feature, with pictures.' },
  // the level navigator
  'level.prev': { title: 'The level before', key: '⌥←', text: 'Opens the level before this one.' },
  'level.next': { title: 'The level after', key: '⌥→', text: 'Opens the level after this one.' },
  'level.picker': { title: 'Every level', key: 'L', text: 'The campaign by zone: each level’s state (ready, not ready, checking), its gauntlet, unsaved edits. Click one to open it.' },
  // the Levels view's tools
  'ltool.wall': { title: 'Walls', key: 'W', text: 'Drag across columns: each wall as high as the pointer (⇧: as high as where you started), of the kind picked. ⌥ or right drag: down to the street.' },
  'ltool.paint': { title: 'Kind', key: 'K', text: 'Drag across walls to give them the kind picked (crates, hay, hay-topped crates). Their heights stay.' },
  'ltool.ceil': { title: 'Barrier', key: 'B', text: 'Drag across walls at a row: a barrier’s underside there, at least 2 eggs over the wall (her body goes under it). ⌥ or right drag: barriers off.' },
  'ltool.block': { title: 'Floating block', key: 'O', text: 'Drag a box over columns: a block in the air, from its underside (1 egg over the wall at least) to its top. She ducks under it, or climbs onto it with a stack as high as its top and rides it. Of the kind picked over the street. ⌥ or right drag: blocks off.' },
  'ltool.saw': { title: 'Saw', key: 'S', text: 'Click a wall top for a saw blade. Near another it snaps a blade’s gap away, so blades sit back to back. Drag one along; ⌥ or right click removes it.' },
  'ltool.disco': { title: 'Disco ball', key: 'D', text: 'Click for a disco ball: +5 points and an egg and a half for the meter when the top of her stack grabs it. Drag one; ⌥ or right click removes it.' },
  'ltool.laser': { title: 'Wall laser', key: 'Z', text: 'Click by a wall’s or a floating block’s side for a laser set into it at the row clicked (eggs up from the street), firing along the course away from it; it snaps to the nearest side within two columns. Drag it to another side or row; ⌥ or right click removes it. Never in a gauntlet: its lasers are its grid’s.' },
  'ltool.lava': { title: 'Lava', key: 'A', text: 'Click a surface for lava, 3 cells long (an egg burned a cell). Drag one along, or drag its right end to make it 1 to 8 cells long; ⌥ or right click removes it. Never in a gauntlet: its lava is its grid’s.' },
  'ltool.spring': { title: 'Spring', key: 'J', text: 'Click a surface for a spring pad, 6 rows of lift. Drag it along; drag the handle over a selected one (or press ↑ ↓) for 2 to 12 rows of lift; ⌥ or right click removes it. Its dotted arc is her flight from the last check: a ring where she lands, red where she meets a wall’s face. 1.2 m from another at least; never in a gauntlet.' },
  'ltool.bumper': { title: 'Bumper', key: 'U', text: 'Drag across columns for a bumper wall as high as the pointer (1 at least; ⇧: as high as where you started). Running into it at her body bounces her back; stacked over it, she climbs onto it. Bouncer walls 40 cells apart at most make a bounce zone. ⌥ or right drag: down to the street. Never the course’s first wall; never under a barrier or a block.' },
  'ltool.gate-right': { title: 'Gate →', key: 'Y', text: 'Drag across columns for a one-way gate as high as the pointer (1 at least): she runs through it heading right; heading left it bounces her, like a bumper. Never a floor: she drops through it. ⌥ or right drag: down to the street.' },
  'ltool.gate-left': { title: 'Gate ←', key: '⇧Y', text: 'Drag across columns for a one-way gate as high as the pointer (1 at least): she runs through it heading left; heading right it bounces her, like a bumper. Never a floor. ⌥ or right drag: down to the street.' },
  'ltool.cols': { title: 'Columns', key: 'C', text: 'Click or drag across columns to copy them in right after themselves; what’s after them moves on (saws, disco balls, the finish). ⌥ or right: taken out.' },
  'ltool.select': { title: 'Select', key: 'V', text: 'Select columns. ⌘C or ⌘X copies or cuts them with their saws and disco balls; ⌘V puts them in where you point; ⌫ takes them out.' },
  'ltool.gauntlet': { title: 'Gauntlet', key: 'G', text: 'Drag the level’s maze gauntlet along the course; its forecourt, lintel and porch come with it. Double-click it with any tool to edit its grid.' },
  'ltool.start': { title: 'Start point', key: 'T', text: 'Click where Play and Watch the bot should start: on the course, or on a cell inside the maze. Click her in the maze to turn her round; ⌥ click clears it.' },
  // wall kinds
  'kind.crate': { title: 'Crates', key: '1', text: 'A wall of crates. A saw on its top is a floor saw.' },
  'kind.hay': { title: 'Hay', key: '2', text: 'A wall of hay bales. A saw on its top is a cube saw.' },
  'kind.mix': { title: 'Hay-topped crates', key: '3', text: 'Crates with a hay bale on top. A saw on its top is a cube saw.' },
  // the Levels view's overlays
  'lover.bot': { title: 'The bot’s run', text: 'The real game’s bot, played headless through the whole level: its stack all along, its near misses, and where it crashed.' },
  'lover.notes': { title: 'Notes', text: 'Where the checker finds the course unfair: an egg budget running short, a saw on an edge, no landing after a drop, a door off its height.' },
  'lover.changes': { title: 'Changes', text: 'What’s changed since the save. The saved course shows as a dashed ghost, saws and disco balls added are ringed green, removed ones ghosted in red.' },
  'lover.grid': { title: 'Grid lines', text: 'Cell lines, a cell every 0.6 m (one egg) along and up.' },
  // the Levels view's footer, minimap, zoom
  'lreplay.play': { title: 'Play the bot’s run', key: 'P', text: 'Replays the bot’s run in the game along the course: the hen and her stack, at the speed picked.' },
  'lreplay.start': { title: 'To its start', key: 'Home', text: 'Back to the start of the bot’s run.' },
  'lreplay.follow': { title: 'Follow her', text: 'While it plays, the course scrolls to keep her on screen.' },
  'lreplay.speed': { title: 'Speed', text: 'The replay’s speed against the game’s own: 0.5×, 1×, 2× or 4×.' },
  'lreplay.track': { title: 'The run, metre by metre', text: 'The eggs a careful player holds (the checker’s egg budget, cyan) and the bot’s stack (volt), with its near misses. Click or drag to look.' },
  'lview.minimap': { title: 'The whole course', text: 'The level start to finish: its walls, gauntlet, saws and the bot’s run. Click or drag to look along it.' },
  'view.zoom': { title: 'Zoom', key: '[ ] F', text: 'Zoom out, fit, zoom in. ⌘-scroll or pinch zooms at the pointer; Space-drag pans.' },
  // the level's properties
  'prop.finish': { title: 'Finish line', text: 'How far the level runs, in metres. You can drag its flag on the course too.' },
  'prop.difficulty': { title: 'Difficulty', text: 'Where the level starts on the game’s difficulty curve, and how fast it climbs per metre: what her speed and the egg refill are.' },
  'prop.raids': { title: 'Droid raids', text: 'Auto is the game’s rule: none before level 5, one, then one or two from 7. Or none, or exactly 1, 2 or 3.' },
  'prop.gauntlet': { title: 'Gauntlet', text: 'The maze gauntlet this level plays. Picking one in another level moves it here; a gauntlet plays in one level at most.' },
  'prop.edit': { title: 'Edit its grid', text: 'Opens the gauntlet’s grid in the Gauntlets view.' },
  'gauntlet.rename': { title: 'Rename', text: 'A new name for the gauntlet: its file and the level that lays it follow (nothing else in the level changes). The game’s banner shows _ as spaces.' },
  'prop.new': { title: 'New gauntlet', text: 'A new gauntlet, from a plain room to design. It goes into this level if the level has none.' },
  // the inspector's cards
  'card.verdict': { title: 'Ready to ship', text: 'Every check the game’s tests make, item by item. Not ready: the tests fail and block commits until it is.' },
  'card.history': { title: 'History', text: 'Every edit, newest first, with what it did and when. Click one to go back (or forward) to it; hover to see where it was.' },
  // the Gauntlets view's tools
  'gtool.brush': { title: 'Brush', key: 'B', text: 'Paint cells: the first cell, or right drag for the second; ⌥ click picks one up. With a door picked, click a row to move the door there.' },
  'gtool.rect': { title: 'Rectangle', key: 'R', text: 'Fill a rectangle with the first cell (right drag: the second).' },
  'gtool.select': { title: 'Select', key: 'V', text: 'Select cells, then drag them somewhere (what they leave fills from their sides). ⌘C ⌘X ⌘V; F flips them; ⌫ fills them.' },
  'gtool.cols': { title: 'Columns', key: 'C', text: 'Click or drag across columns to copy them in right after themselves (a floor made longer). ⌥ or right: taken out.' },
  'gtool.rows': { title: 'Rows', key: 'E', text: 'Click or drag across rows to copy them in above themselves (everything over them moves up). ⌥ or right: taken out.' },
  'gtool.pick': { title: 'Pick', key: 'I', text: 'Click a cell to make it the first cell.' },
  'gtool.start': { title: 'Start point', key: 'T', text: 'Click a cell where Play and Watch the bot should start, inside the maze, with no eggs. Click her again to turn her round; ⌥ click clears it.' },
  // the gauntlets' cells
  'cell.air': { title: 'Air', key: '1', text: 'Where she runs. Carve rooms and passages out of it.' },
  'cell.stone': { title: 'Stone', key: '2', text: 'Floors, roofs and walls. Stone at her body crashes her; a step rising from her floor, she climbs if her stack’s as high.' },
  'cell.cube': { title: 'Cyber cube', key: '3', text: 'Stone, in the cyber cubes’ look.' },
  'cell.bumper': { title: 'Bumper', key: '4', text: 'Turns her back the other way. Stacked over it, she climbs it like a step.' },
  'cell.gate-right': { title: 'Gate →', key: '5', text: 'A one-way gate she passes running right; running left it turns her back like a bumper. Never a floor: she drops through it.' },
  'cell.gate-left': { title: 'Gate ←', key: '6', text: 'A one-way gate she passes running left; running right it turns her back.' },
  'cell.saw': { title: 'Saw', key: '7', text: 'A blade in stone. Each time her stack crosses it, it takes an egg; with no egg under her, it gets her.' },
  'cell.cube-saw': { title: 'Cube saw', key: '8', text: 'A blade in a cyber cube: the same as a saw.' },
  'cell.laserL': { title: 'Laser, firing left', key: '[', text: 'Stone with a wall laser in its left face: it fires left along its row through the air to the first cell that isn’t. It charges, then fires: an egg in its row is cut out, her body in it is cut in half, over her head harmless.' },
  'cell.laserR': { title: 'Laser, firing right', key: ']', text: 'Stone with a wall laser in its right face: it fires right along its row through the air to the first cell that isn’t. It charges, then fires: an egg in its row is cut out, her body in it is cut in half, over her head harmless.' },
  'cell.lava': { title: 'Lava', key: '=', text: 'A floor that burns the egg under her stack, one each cell she crosses; with no egg under her, it burns her.' },
  'cell.spring': { title: 'Spring', key: '\\', text: 'A pad she’s launched off as her middle reaches its middle: up 5 rows and on at 8 m/s whatever the run’s speed, no eggs laid in the air. She lands on the first floor or ledge under her; a wall or roof in her way ends her run, a bumper or gate against her bounces her mid-air, and a blade, lava, a laser’s beam or the exit are no place to fly into or land on. Put the landing in a clear stretch.' },
  'cell.entry': { title: 'Entry', key: '9', text: 'The door in, on the left edge; it shuts behind her. Pick it, then click a row to move it there.' },
  'cell.exit': { title: 'Exit', key: '0', text: 'The door out, on the right edge. Pick it, then click a row to move it there.' },
  // the Gauntlets view's overlays
  'gover.path': { title: 'Bot’s way', text: 'The solver’s way through: cyan running right, pink running left, a dot where it lays an egg.' },
  'gover.misses': { title: 'Near misses', text: 'Where the game shows them on the bot’s way: PERFECT (an exact climb), CLOSE SHAVE (a blade takes her last egg), CLOSE CALL (a squeeze).' },
  'gover.reach': { title: 'Where she gets', text: 'Every cell her feet can reach, stacking any way: green on a way through, red in a dead end.' },
  'gover.changes': { title: 'Changes', text: 'The cells changed since the save, outlined. Hover one to see what it was.' },
  'gover.grid': { title: 'Grid lines', text: 'Cell lines, and stronger ones every 10.' },
  // the Gauntlets view's footer
  'greplay.play': { title: 'Play the bot’s run', key: 'P', text: 'Replays the solver’s bot through the grid, move by move.' },
  'greplay.prev': { title: 'A move back', key: ',', text: 'Steps the replay a move back.' },
  'greplay.next': { title: 'A move on', key: '.', text: 'Steps the replay a move on.' },
  'greplay.speed': { title: 'Speed', text: 'The replay’s speed against the game’s own.' },
  'greplay.rate': { title: 'Meter', text: 'The egg meter refilling at the level’s rate, or 1.25 or 1.4 times slower: the adaptive difficulty at its hardest. A “hard N” maze (N 6 to 10) is judged on less spare: 1.25 down to 1.0 times slower at 10, and 15 + (N − 5) near misses.' },
  'greplay.track': { title: 'The run, move by move', text: 'The eggs in the meter (cyan) and her stack (volt) through the gauntlet, with its near misses. Click or drag to look.' },
  'gview.minimap': { title: 'The whole gauntlet', text: 'The grid, with the bot’s way. Click or drag to look around.' },
  // hazards and pieces on the course (the canvases' cards)
  'hazard.saw': { title: 'Saw blade', text: 'As her stack crosses it, it saws off the bottom egg; with no egg under her, it gets her. Blades a gap (1.8 m) apart cut one after another.' },
  'hazard.cube-saw': { title: 'Cube saw', text: 'A saw on a hay or hay-topped wall: it cuts the same.' },
  'hazard.barrier': { title: 'Barrier', text: 'A girder under a red laser curtain. Her head must pass under it, so only so many eggs fit under it.' },
  'hazard.block': { title: 'Floating block', text: 'A block in the air: duck under it (its underside a barrier), or climb onto it with a stack as high as its top (the eggs below it knocked off) and ride it to its end. A saw on its top cuts only up there.' },
  'hazard.disco': { title: 'Disco ball', text: 'Grabbed by the top of her stack: +5 points, and an egg and a half for the meter.' },
  'hazard.laser': { title: 'Wall laser', text: 'Set into a wall’s or a block’s side, it fires along the course as far as the first solid cell in its row. As she nears it, it charges (a thin pointer line and dot), then fires a thick beam, once a pass: an egg in its row is cut out (the ones over it drop), her body in it is cut in half, over her head it’s harmless (a row over: a close call).' },
  'hazard.lava': { title: 'Lava', text: 'A stretch of floor that burns the egg under her stack, one each cell she crosses (an egg every 0.6 m); with no egg under her, it burns her. Stack as many eggs as it’s long.' },
  'hazard.spring': { title: 'Spring', text: 'A pad that launches her, stack and all, as she runs over it: up as many rows as its lift, her speed kept, onto what’s ahead. It costs no eggs; mind the walls’ faces and barriers on the way, and land her somewhere long enough.' },
  'hazard.bumper': { title: 'Bumper', text: 'A bumper wall: running into it at her body bounces her back the way she came; stacked over it, she climbs onto it. A gate or bumper before it bounces her back again: she stacks while she bounces to climb out.' },
  'hazard.gate-right': { title: 'Gate →', text: 'A one-way gate: she runs through it heading right; heading left it bounces her, like a bumper. Never a floor (she drops through it).' },
  'hazard.gate-left': { title: 'Gate ←', text: 'A one-way gate: she runs through it heading left; heading right it bounces her, like a bumper. Never a floor.' },
  'hazard.bounce-zone': { title: 'Bounce zone', text: 'Bouncer walls 40 cells apart at most, played by a maze’s rules: she bounces off bumpers and gates set against her, back and forth, and stacks while she bounces to climb out over the last one. The checker judges it with the maze’s solver; a spring on a wall’s top is a spring cell there (the same arc as a maze’s), and saws and lava sit in wall tops only; no wall lasers in one yet.' },
  'hazard.wall': { title: 'Wall', text: 'She climbs onto a wall if her stack is as high as it, or crashes. Its top is the disco floor.' },
  'hazard.gauntlet': { title: 'Maze gauntlet', text: 'Its door shuts behind her, and she runs it on her own at full speed. Double-click to edit its grid.' },
  'hazard.finish': { title: 'Finish line', text: 'Where the level ends. Drag its flag to move it.' },
  'hazard.start': { title: 'Start point', text: 'Where Play and Watch the bot start, with a full meter and no eggs. Drag it; ⌥ click clears it.' },
  'hazard.note': { title: 'Note', text: 'Where the checker finds the course unfair, and why.' },
};

// a grid cell's tip key
export const CELL_TIP = { '.': 'cell.air', '#': 'cell.stone', H: 'cell.cube', B: 'cell.bumper', R: 'cell.gate-right', L: 'cell.gate-left', S: 'cell.saw', C: 'cell.cube-saw', '(': 'cell.laserL', ')': 'cell.laserR', '~': 'cell.lava', '^': 'cell.spring', '>': 'cell.entry', X: 'cell.exit' };

// a tip's attributes, for HTML (extra: a line more, after its text)
export function tipAttrs(key, extra = '') {
  const t = TIPS[key];
  if (!t) return '';
  return ` data-tip="${esc(t.text + (extra ? ' ' + extra : ''))}" data-tip-title="${esc(t.title)}"${t.key ? ` data-tip-key="${esc(t.key)}"` : ''} data-tip-id="${esc(key)}"`;
}
// ...or set on an element
export function setTip(el, key, extra = '') {
  const t = TIPS[key];
  if (!t || !el) return el;
  el.dataset.tip = t.text + (extra ? ' ' + extra : '');
  el.dataset.tipTitle = t.title;
  if (t.key) el.dataset.tipKey = t.key; else delete el.dataset.tipKey;
  el.dataset.tipId = key;
  el.removeAttribute('title');
  return el;
}
