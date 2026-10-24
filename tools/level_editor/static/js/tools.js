// The level editor's tools, wall kinds and overlays, by view: their icons, keys and labels (their descriptions:
// tips.js). Plain data: the views draw their tool columns from it, and the wiki draws its pictures of them.
import { C } from './view.js';
import { K_COLORS } from './courseview.js';

export const LEVEL_TOOLS = [
  { id: 'wall', key: 'w', label: 'Walls', icon: '<path d="M3 20h18M5 20V10h4v10M10 20V6h4v14M15 20v-7h4v7"/>', tip: 'ltool.wall' },
  { id: 'paint', key: 'k', label: 'Kind', icon: '<path d="M4 20h16M6 16l7-7 4 4-7 7H6zM13 9l3-3 4 4-3 3"/>', tip: 'ltool.paint' },
  { id: 'ceil', key: 'b', label: 'Barrier', icon: '<path d="M3 7h18v4H3zM6 7l-2 4M11 7l-2 4M16 7l-2 4M21 7l-2 4M7 15v5M17 15v5"/>', tip: 'ltool.ceil' },
  { id: 'block', key: 'o', label: 'Block', icon: '<path d="M5 6h14v6H5zM5 9h14M12 6v3M8.5 9v3M15.5 9v3M3 20h18M7 16v2M12 15v3M17 16v2"/>', tip: 'ltool.block' },
  { id: 'saw', key: 's', label: 'Saw', icon: '<path d="M4 17a8 8 0 0 1 16 0zM12 17l-2-6M12 17l4-5M12 17h6"/><path d="M2 17h20"/>', tip: 'ltool.saw' },
  { id: 'disco', key: 'd', label: 'Disco', icon: '<circle cx="12" cy="13" r="6"/><path d="M12 3v4M6 13h12M12 7c-3 3-3 9 0 12M12 7c3 3 3 9 0 12"/>', tip: 'ltool.disco' },
  { id: 'laser', key: 'z', label: 'Laser', icon: '<path d="M5 4v16M3 20h4M5 11h15"/><circle cx="5" cy="11" r="2"/><path d="M14 8l2 3-2 3"/>', tip: 'ltool.laser' },
  { id: 'lava', key: 'a', label: 'Lava', icon: '<path d="M2 20h20M4 16h16"/><path d="M12 3c1.5 2.5 4 4 4 7a4 4 0 0 1-8 0c0-1.6.8-2.6 1.6-3.6.3 1.3 1 2 2 2.1-.4-1.8-.3-3.6.4-5.5z"/>', tip: 'ltool.lava' },
  { id: 'cols', key: 'c', label: 'Columns', icon: '<path d="M7 4v16M17 4v16M10 12h4M12 10v4"/>', tip: 'ltool.cols' },
  { id: 'select', key: 'v', label: 'Select', icon: '<path d="M4 4h4M12 4h4M20 4v4M20 12v4M20 20h-4M12 20H8M4 20v-4M4 12V8"/>', tip: 'ltool.select' },
  { id: 'gauntlet', key: 'g', label: 'Gauntlet', icon: '<path d="M4 20h16V8l-8-5-8 5zM8 20v-6h8v6M8 11h8"/>', tip: 'ltool.gauntlet' },
  { id: 'start', key: 't', label: 'Start', icon: '<path d="M6 21V4M6 4h11l-2.5 4L17 12H6"/>', tip: 'ltool.start' },
];
// ...and the Bouncers (the game's registry groups them: Hazards.h): the spring, and the bouncer walls, each a Walls tool
// building columns of its kind (a bumper, a one-way gate: course.js setBouncer). Gate ← has no key of its own: ⇧Y.
export const BOUNCER_TOOLS = [
  { id: 'spring', key: 'j', label: 'Spring', icon: '<path d="M4 21h16M6 8h12M9 5l3-3 3 3"/><path d="M8 19l8-2.5-8-2.5 8-2.5-8-2.5"/>', tip: 'ltool.spring' },
  { id: 'bumper', key: 'u', label: 'Bumper', icon: '<path d="M9 4h6v16H9z"/><path d="M6 8l-3 4 3 4M18 8l3 4-3 4"/>', tip: 'ltool.bumper' },
  { id: 'gate>', key: 'y', label: 'Gate →', icon: '<path d="M5 4v16M19 4v16"/><path d="M8 12h8M13 8.5l3.5 3.5-3.5 3.5"/>', tip: 'ltool.gate-right' },
  { id: 'gate<', key: '', label: 'Gate ←', icon: '<path d="M5 4v16M19 4v16"/><path d="M16 12H8M11 8.5L7.5 12l3.5 3.5"/>', tip: 'ltool.gate-left' },
];
export const KINDS = [{ id: 'crate', key: '1', label: 'Crates', tip: 'kind.crate' }, { id: 'hay', key: '2', label: 'Hay', tip: 'kind.hay' }, { id: 'mix', key: '3', label: 'Hay-topped', tip: 'kind.mix' }];
export const LEVEL_OVERLAYS = [
  { id: 'bot', label: 'The bot’s run', color: K_COLORS.bot, tip: 'lover.bot' }, { id: 'notes', label: 'Notes', color: K_COLORS.note, tip: 'lover.notes' },
  { id: 'changes', label: 'Changes', color: '#ffffff', tip: 'lover.changes' }, { id: 'grid', label: 'Grid lines', color: 'rgba(255,255,255,0.5)', tip: 'lover.grid' },
];

export const GAUNTLET_TOOLS = [
  { id: 'brush', key: 'b', label: 'Brush', icon: '<path d="M18 3l3 3-9.5 9.5-3-3zM8.5 12.5l3 3c0 2.5-2 5.5-7.5 5.5 1-1.5 1.5-3 1.5-4.5a4 4 0 0 1 3-4z"/>', tip: 'gtool.brush' },
  { id: 'rect', key: 'r', label: 'Rect', icon: '<rect x="4" y="5" width="16" height="14" rx="1.5"/>', tip: 'gtool.rect' },
  { id: 'select', key: 'v', label: 'Select', icon: '<path d="M4 4h4M12 4h4M20 4v4M20 12v4M20 20h-4M12 20H8M4 20v-4M4 12V8"/>', tip: 'gtool.select' },
  { id: 'cols', key: 'c', label: 'Columns', icon: '<path d="M7 4v16M17 4v16M10 12h4M12 10v4"/>', tip: 'gtool.cols' },
  { id: 'rows', key: 'e', label: 'Rows', icon: '<path d="M4 7h16M4 17h16M10 12h4M12 10v4"/>', tip: 'gtool.rows' },
  { id: 'pick', key: 'i', label: 'Pick', icon: '<path d="M15 4l5 5-2 2-5-5zM13 6l-8 8v5h5l8-8"/>', tip: 'gtool.pick' },
  { id: 'start', key: 't', label: 'Start', icon: '<path d="M6 21V4M6 4h11l-2.5 4L17 12H6"/>', tip: 'gtool.start' },
];
export const GAUNTLET_OVERLAYS = [
  { id: 'path', label: 'Bot’s way', color: C.pathR, tip: 'gover.path' }, { id: 'misses', label: 'Near misses', color: C.perfect, tip: 'gover.misses' },
  { id: 'reach', label: 'Where she gets', color: '#3dff8a', tip: 'gover.reach' }, { id: 'changes', label: 'Changes', color: '#ffffff', tip: 'gover.changes' },
  { id: 'grid', label: 'Grid lines', color: 'rgba(255,255,255,0.5)', tip: 'gover.grid' },
];
