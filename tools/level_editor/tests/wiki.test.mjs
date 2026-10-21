// The level editor's wiki (tools/level_editor/wiki) documents every feature: each tooltip's key (static/js/tips.js) has
// its section there (id="tip-<key>"), and none is left for a tooltip that's gone; its links all go somewhere, the
// pictures' (figures.js) too; each picture it asks for is one figures.js draws. A feature added to the editor without
// its documentation fails here, and so blocks commits: add its section to wiki/index.html (and its changelog line).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { TIPS } from '../static/js/tips.js';

const read = (path) => readFileSync(new URL(path, import.meta.url), 'utf8');
const html = read('../wiki/index.html'), figures = read('../wiki/figures.js');
const ids = [...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]);
const has = new Set(ids);

test('every tooltip has its section in the wiki', () => {
  const missing = Object.keys(TIPS).filter((key) => !has.has(`tip-${key}`));
  assert.deepEqual(missing, [], `tools/level_editor/wiki/index.html needs a section (id="tip-<key>") for: ${missing.join(', ')}`);
});

test('no section is left for a tooltip that has gone', () => {
  const stale = ids.filter((id) => id.startsWith('tip-') && !Object.hasOwn(TIPS, id.slice(4)));
  assert.deepEqual(stale, [], 'tips.js has no tip for these sections: rename or remove them');
});

test('each section has one name', () => {
  const twice = ids.filter((id, i) => ids.indexOf(id) !== i);
  assert.deepEqual(twice, []);
});

test('every link in the wiki goes somewhere, its contents and its pictures’ too', () => {
  const page = [...html.matchAll(/href="#([^"]+)"/g)].map((m) => m[1]);
  const pictures = [...figures.matchAll(/\['#([^']+)'/g)].map((m) => m[1]);
  assert.ok(page.length > 20 && pictures.length >= 20);
  assert.deepEqual([...page, ...pictures].filter((id) => !has.has(id)), []);
});

test('every picture the wiki asks for is one figures.js draws', () => {
  const figs = new Set([...figures.matchAll(/FIGS\.(\w+) = /g)].map((m) => m[1]));
  const asked = new Set([...html.matchAll(/data-fig="(\w+)"/g)].map((m) => m[1]));
  assert.deepEqual([...asked].filter((f) => !figs.has(f)), []);
  for (const [, scene] of html.matchAll(/data-scene="([\w-]+)"/g)) assert.match(figures, new RegExp(`\\n  ${scene}: \\(\\) =>`), `figures.js has no scene "${scene}"`);
});

test('the changelog has a dated line', () => {
  const dates = [...html.matchAll(/<time>(\d{4}-\d{2}-\d{2})<\/time>/g)].map((m) => m[1]);
  assert.ok(dates.length > 0);
  assert.deepEqual(dates, [...dates].sort().reverse(), 'newest first');
});
