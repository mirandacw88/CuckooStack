// The level editor's tooltips: one card, drawn in the page (the browser's own `title` tips don't show everywhere, the
// Claude app's browser pane among them). Anything with data-tip (data-tip-title: its first line; data-tip-key: a key
// to press) shows it after a moment's hover or on keyboard focus, beside it and on screen; and the canvases show it
// at the pointer (showAt) for what's under it.
const DELAY = 320, QUICK = 120; // (hover; moving from one tip straight to the next)
let card = null, timer = 0, shownFor = null, hiddenAt = 0;

function build() {
  card = document.createElement('div');
  card.className = 'tip';
  card.setAttribute('role', 'tooltip');
  document.body.append(card);
}
function fill(title, text, key) {
  card.innerHTML = '';
  if (title || key) {
    const head = document.createElement('div');
    head.className = 'tip-head';
    if (title) { const b = document.createElement('b'); b.textContent = title; head.append(b); }
    if (key) for (const k of key.split(' ')) { const kbd = document.createElement('kbd'); kbd.textContent = k; head.append(kbd); }
    card.append(head);
  }
  if (text) { const p = document.createElement('div'); p.className = 'tip-text'; p.textContent = text; card.append(p); }
}
function place(x, y, w, h, side) { // (beside a box: its side if it fits, else the other; always on screen)
  const cw = card.offsetWidth, ch = card.offsetHeight, gap = 10, W = window.innerWidth, H = window.innerHeight;
  let left, top;
  if (side === 'right') { left = x + w + gap; top = y + h / 2 - ch / 2; if (left + cw > W - 8) left = x - cw - gap; }
  else if (side === 'left') { left = x - cw - gap; top = y + h / 2 - ch / 2; if (left < 8) left = x + w + gap; }
  else if (side === 'above') { left = x + w / 2 - cw / 2; top = y - ch - gap; if (top < 8) top = y + h + gap; }
  else { left = x + w / 2 - cw / 2; top = y + h + gap; if (top + ch > H - 8) top = y - ch - gap; }
  card.style.left = `${Math.round(Math.max(8, Math.min(W - cw - 8, left)))}px`;
  card.style.top = `${Math.round(Math.max(8, Math.min(H - ch - 8, top)))}px`;
  card.dataset.side = side;
}
function sideFor(elt, r) {
  if (elt.dataset.tipSide) return elt.dataset.tipSide;
  if (elt.closest('.tools')) return 'right';
  if (elt.closest('.inspector')) return 'left';
  if (elt.closest('.replay')) return 'above';
  if (r.top < 70) return 'below';
  return 'above';
}
function show(elt) {
  if (!card) build();
  fill(elt.dataset.tipTitle, elt.dataset.tip, elt.dataset.tipKey);
  card.classList.remove('in');
  card.style.display = 'block';
  const r = elt.getBoundingClientRect();
  place(r.left, r.top, r.width, r.height, sideFor(elt, r));
  void card.offsetWidth;
  card.classList.add('in');
  shownFor = elt;
}
export function hide() {
  clearTimeout(timer);
  if (card && card.classList.contains('in')) { card.classList.remove('in'); hiddenAt = performance.now(); }
  if (card) card.style.display = 'none';
  shownFor = null;
}
// a card at a point (the canvases: what's under the pointer); content: { title, text, key }
export function showAt(px, py, content) {
  if (!card) build();
  fill(content.title, content.text, content.key);
  card.style.display = 'block';
  if (!card.classList.contains('in')) { card.classList.remove('in'); void card.offsetWidth; card.classList.add('in'); }
  place(px + 14, py + 14, 0, 0, 'below');
  const r = card.getBoundingClientRect();
  if (r.right > window.innerWidth - 8 || r.bottom > window.innerHeight - 8) place(px - 14, py - 14, 0, 0, 'above');
  shownFor = null;
}
export function initTooltips() {
  build();
  hide();
  document.addEventListener('pointerover', (e) => {
    const elt = e.target.closest && e.target.closest('[data-tip]');
    if (!elt || elt === shownFor) return;
    clearTimeout(timer);
    const quick = performance.now() - hiddenAt < 400 || shownFor;
    timer = setTimeout(() => { if (elt.isConnected && elt.matches(':hover')) show(elt); }, quick ? QUICK : DELAY);
  });
  document.addEventListener('pointerout', (e) => {
    const elt = e.target.closest && e.target.closest('[data-tip]');
    if (elt && !(e.relatedTarget && elt.contains(e.relatedTarget))) hide();
  });
  document.addEventListener('pointerdown', hide, true);
  document.addEventListener('focusin', (e) => { const elt = e.target.closest && e.target.closest('[data-tip]'); if (elt && e.target.matches(':focus-visible')) show(elt); });
  document.addEventListener('focusout', hide);
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') hide(); }, true);
  window.addEventListener('scroll', hide, true);
  window.addEventListener('blur', hide);
}
