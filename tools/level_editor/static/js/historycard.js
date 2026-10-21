// The History card (both views): a level's or a grid's edits, newest first, each with what it did and how long ago;
// the one it's at highlighted, those undone dimmed (click one: made again), the saved state marked. Click an edit to
// go back (or on) to just after it; hover one to see where it was on the canvas; its target button looks there.
import { el, esc } from './util.js';
import * as H from './history.js';

const ICON = {
  start: '<circle cx="12" cy="12" r="3"/>',
  walls: '<path d="M4 20h16M6 20v-8h4v8M12 20V7h4v13"/>',
  kind: '<path d="M4 20h16M6 16l7-7 4 4-7 7H6z"/>',
  ceil: '<path d="M3 8h18v4H3zM7 8l-2 4M12 8l-2 4M17 8l-2 4"/>',
  'ceil-off': '<path d="M3 8h18v4H3zM5 18l14-14"/>',
  'saw-add': '<path d="M4 16a8 8 0 0 1 16 0zM2 16h20"/>', 'saw-move': '<path d="M4 16a8 8 0 0 1 16 0zM2 16h20M9 5l3-3 3 3"/>', 'saw-remove': '<path d="M4 16a8 8 0 0 1 16 0zM2 16h20M6 4l12 12"/>',
  'disco-add': '<circle cx="12" cy="12" r="6"/><path d="M6 12h12"/>', 'disco-move': '<circle cx="12" cy="12" r="6"/><path d="M6 12h12"/>', 'disco-remove': '<circle cx="12" cy="12" r="6"/><path d="M6 6l12 12"/>',
  'cols-in': '<path d="M7 4v16M17 4v16M10 12h4M12 10v4"/>', 'cols-out': '<path d="M7 4v16M17 4v16M10 12h4"/>', paste: '<path d="M8 4h8v4H8zM6 6H5v14h14V6h-1"/>', cut: '<path d="M6 6l12 12M6 18L18 6"/>',
  gauntlet: '<path d="M4 20h16V9l-8-5-8 5zM9 20v-5h6v5"/>', finish: '<path d="M6 21V4M6 4h12l-3 4 3 4H6"/>', difficulty: '<path d="M4 18a8 8 0 0 1 16 0M12 18l4-6"/>',
  raids: '<circle cx="12" cy="11" r="5"/><path d="M8 19h8"/>', notes: '<path d="M5 5h14M5 10h14M5 15h9"/>', revert: '<path d="M9 7H4V2M4 7a9 9 0 1 1-1.8 5.6"/>',
  refit: '<path d="M4 12h16M8 8l-4 4 4 4M16 8l4 4-4 4"/>', draft: '<path d="M4 20l4-1 11-11-3-3L5 16z"/>',
  brush: '<path d="M18 3l3 3-9.5 9.5-3-3zM8.5 12.5l3 3c0 2.5-2 5.5-7.5 5.5 1-1.5 1.5-3 1.5-4.5a4 4 0 0 1 3-4z"/>', rect: '<rect x="4" y="6" width="16" height="12" rx="1.5"/>',
  fill: '<rect x="4" y="6" width="16" height="12" rx="1.5"/><path d="M4 12h16"/>', door: '<path d="M6 21V3h12v18M13 12h1"/>', move: '<path d="M12 3v18M3 12h18M9 6l3-3 3 3M9 18l3 3 3-3"/>',
  flip: '<path d="M12 3v18M4 7l5 5-5 5zM20 7l-5 5 5 5z"/>', 'rows-in': '<path d="M4 7h16M4 17h16M10 12h4M12 10v4"/>', 'rows-out': '<path d="M4 7h16M4 17h16M10 12h4"/>', edit: '<circle cx="12" cy="12" r="3"/>',
};
const icon = (kind) => `<svg viewBox="0 0 24 24">${ICON[kind] || ICON.edit}</svg>`;

// box: the card; t: the timeline; summary: what's changed since the save, in a few words
export function renderHistory(box, { t, summary, onJump, onHover, onShow }) {
  const since = H.sinceSaved(t).length, before = t.saved > t.cursor; // (undone back past the save)
  const head = before ? `back before the save${summary ? ` · ${summary}` : ''}` : since ? `${since} edit${since === 1 ? '' : 's'} since the save${summary ? ` · ${summary}` : ''}` : 'as saved';
  box.innerHTML = `<h3>History <span class="aside">${esc(head)}</span></h3>`;
  const ul = el('ul', 'history');
  const now = Date.now();
  for (let i = t.states.length - 1; i >= 0; i--) {
    const e = t.entries[i];
    const cls = ['h-row', i === t.cursor ? 'at' : '', i > t.cursor ? 'undone' : '', i === t.saved ? 'saved' : ''].filter(Boolean).join(' ');
    const li = el('li', cls, `<span class="h-ico">${icon(e.kind)}</span><span class="h-label">${esc(e.label)}</span>${i === t.saved ? '<span class="h-badge">SAVED</span>' : ''}<span class="h-time">${esc(H.ago(e.at, now))}</span>`);
    li.dataset.tip = i === t.cursor ? 'You are here.' : i > t.cursor ? 'Undone: click to make it again (and everything up to it).' : 'Click to go back to just after this edit; undo and redo still step from there.';
    li.dataset.tipTitle = e.label;
    li.dataset.tipSide = 'left';
    li.onclick = () => onJump(i);
    if (e.focus) {
      li.onpointerenter = () => onHover(e.focus);
      li.onpointerleave = () => onHover(null);
      const look = el('button', 'h-look', '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/></svg>');
      look.setAttribute('aria-label', 'Look there');
      look.dataset.tip = 'Shows where this edit was, without going back to it.';
      look.dataset.tipTitle = 'Look there';
      look.dataset.tipSide = 'left';
      look.onclick = (ev) => { ev.stopPropagation(); onShow(e.focus); };
      li.append(look);
    }
    ul.append(li);
  }
  box.append(ul);
  const at = ul.querySelector('.at');
  if (at) { const top = at.offsetTop - ul.offsetTop - 40; if (top < ul.scrollTop || top > ul.scrollTop + ul.clientHeight - 40) ul.scrollTop = Math.max(0, top); }
}
