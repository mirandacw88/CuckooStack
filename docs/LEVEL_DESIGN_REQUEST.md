# Level design request (paste to Claude)

The user's standing requirements for level and gauntlet design, as a prompt to paste back whenever more is wanted.
Fill in the brackets.

```text
Use the level editor (tools/level_editor) to design [WHAT: e.g. "levels 21–29" or "the gauntlets of levels 21, 23, 25"].

How to work
- Design inside the level editor itself: every edit goes through its tools (or its command layer,
  window.levelEditor.levels/gauntlets.commit, which runs the same functions), shows on its canvas, is recorded in
  its History, is judged by its checks ("Ready to ship", the egg budget, notes, the solver), is played by its bot,
  and is saved with its Save. If anything is unsaved in the editor before you start, ask me what to do with it.
  If the editor lacks something a design needs, add it to the editor (with its wiki section, tip and test).
- Work on a new branch: [BRANCH NAME]. Archive what the new designs replace (don't delete it). Plan first.

What the designs must be
- Very creative. Get really creative: ideas no one has seen in the genre, mechanics used in surprising ways,
  each level built around its own signature moment, its zone's theme carried by the geometry itself.
  Nothing generic, predictable, or anything the generator could have made.
- Think like the best Super Mario Maker user-created levels, the wild ones players build when nothing holds them
  back: towers into the sky, halls carpeted in saws, parades of giant shapes, contraptions, set pieces back to back.
  Pictures drawn in the geometry, one gimmick explored until it surprises, fair tricks. The critics think this way
  too: they reward excess and audacity (fairly), and fail the timid and tidy.
- Levels get gradually harder: each one escalates from the level before it.
- The critics review one level at a time (with its gauntlet), and the work stays on it until all of them pass it.
- A lot of near misses (PERFECT, CLOSE SHAVE, CLOSE CALL), forced by the design rather than by luck,
  choreographed to chain: at least 40 on the bot's run through each course, and at least 40 in each gauntlet.
- Gauntlets on par with level 11's tomb or better. Designs must always get better than the ones before them.

The critics
- Spin up multiple critic sub-agents (at least three, each with a different lens), briefed with
  docs/LEVEL_CRITIC_PROMPT.md (create or update it if needed). They think like world-renowned game level
  designers, judge against the best levels ever shipped, and demand very creative work.
- Each critic scores every design 1 (lowest) to 10 (highest) on the parameters world-renowned level designers
  grade on: creativity and originality; signature moment and memorability; core-mechanic expression; teach, test,
  twist; pacing and flow; near-miss choreography; challenge and difficulty curve; fairness and readability;
  theme through geometry; replayability and mastery.
- A design passes only with 9 or 10 on every parameter from every critic. Revise and re-review until every
  design passes, then show me the final scorecards.

Proof
- Test every level with the bot so a player can surely finish it: the editor shows "Ready to ship", the bot
  clears every level (and gets out of every gauntlet with the meter at its hardest), and the full test suite passes.
- Update the docs, the editor's wiki and the tests to match. Don't commit until I ask.
```
