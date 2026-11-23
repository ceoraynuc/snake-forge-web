/*
 * SnakeForge front end.
 * This file only draws things and sends commands to the C engine
 * (through server.js). All game rules live in engine.c.
 */
'use strict';

const $ = (s) => document.querySelector(s);
const SVGNS = 'http://www.w3.org/2000/svg';
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
const COLORS = ['#C8102E', '#1D4ED8', '#F4B223', '#6D28D9'];
const TEXT_ON = ['#fff', '#fff', '#14202B', '#fff'];
const OFFSETS = [[-20, -4], [20, -4], [-20, 22], [20, 22]];
const SNAKE_COLORS = ['#2F6B3A', '#B5472B', '#6D3A8C', '#8A6D00', '#1F5F7A'];

let S = null;              // last state from the engine
let disp = [1, 1, 1, 1];   // squares where the tokens are drawn right now
let busy = false;
let tool = 'draw';
let pendingFrom = null;
let muted = false;
let computerTimer = null;
let tokenEls = [];
let setupCount = 2;
const setupPlayers = [
  { type: 'h', name: '' }, { type: 'c', name: '' },
  { type: 'c', name: '' }, { type: 'c', name: '' },
];

/* ---------- helpers ---------- */
const sleep = (ms) => new Promise((r) => setTimeout(r, reduceMotion ? Math.min(ms, 40) : ms));

function center(sq) {
  const idx = sq - 1;
  const row = Math.floor(idx / 10);
  const col = row % 2 === 0 ? idx % 10 : 9 - (idx % 10);
  return { x: col * 100 + 50, y: (9 - row) * 100 + 50, row, col };
}

function el(name, attrs, parent) {
  const e = document.createElementNS(SVGNS, name);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}

function setStatus(text, kind) {
  const s = $('#status');
  s.textContent = text;
  s.className = 'status' + (kind ? ' ' + kind : '');
}

async function api(cmd) {
  try {
    const res = await fetch('/api', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd }),
    });
    return await res.json();
  } catch (e) {
    setStatus('Cannot reach the game server. Start it with "node server.js" and reload this page.', 'err');
    return { ok: 0, msg: 'offline' };
  }
}

/* ---------- sound (made with the Web Audio API, no files) ---------- */
let audio = null;
function tone(freq, dur, type, when, vol) {
  if (muted) return;
  try {
    audio = audio || new (window.AudioContext || window.webkitAudioContext)();
    const t0 = audio.currentTime + (when || 0);
    const osc = audio.createOscillator();
    const gain = audio.createGain();
    osc.type = type || 'sine';
    osc.frequency.setValueAtTime(freq, t0);
    gain.gain.setValueAtTime(vol || 0.08, t0);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    osc.connect(gain).connect(audio.destination);
    osc.start(t0);
    osc.stop(t0 + dur + 0.02);
  } catch (e) { /* sound is optional */ }
}
const sound = {
  step() { tone(520, 0.07, 'triangle'); },
  roll() { for (let i = 0; i < 5; i++) tone(300 + Math.random() * 200, 0.05, 'square', i * 0.07, 0.04); },
  ladder() { [392, 494, 587, 784].forEach((f, i) => tone(f, 0.16, 'triangle', i * 0.1)); },
  snake() { [440, 370, 311, 247, 196].forEach((f, i) => tone(f, 0.16, 'sawtooth', i * 0.1, 0.05)); },
  win() { [523, 659, 784, 1047, 784, 1047].forEach((f, i) => tone(f, 0.22, 'triangle', i * 0.13, 0.1)); },
  error() { tone(160, 0.18, 'square', 0, 0.05); },
};

/* ---------- board drawing ---------- */
const svg = $('#board');
let gCells, gJumps, gTokens;

function buildBoard() {
  svg.textContent = '';
  gCells = el('g', {}, svg);
  gJumps = el('g', { 'pointer-events': 'none' }, svg);
  const gNums = el('g', { 'pointer-events': 'none', class: 'nums' }, svg);
  gTokens = el('g', { 'pointer-events': 'none' }, svg);

  for (let sq = 1; sq <= 100; sq++) {
    const c = center(sq);
    const g = el('g', { class: 'cell', 'data-sq': sq, role: 'button', 'aria-label': 'Square ' + sq }, gCells);
    el('rect', { x: c.x - 50, y: c.y - 50, width: 100, height: 100, fill: (c.row + c.col) % 2 ? '#D3E6DC' : '#EAF2EC' }, g);
    const t = el('text', { x: c.x - 42, y: c.y - 26 }, gNums);
    t.textContent = sq === 1 ? 'Start' : sq;
    g.addEventListener('click', () => cellClick(sq));
    g.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); cellClick(sq); }
    });
  }
}

function drawLadder(from, to) {
  const a = center(from), b = center(to);
  const dx = b.x - a.x, dy = b.y - a.y, len = Math.hypot(dx, dy);
  const ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
  const g = el('g', {}, gJumps);
  [-1, 1].forEach((s) => {
    const x1 = a.x + nx * 13 * s, y1 = a.y + ny * 13 * s, x2 = b.x + nx * 13 * s, y2 = b.y + ny * 13 * s;
    el('line', { x1, y1, x2, y2, stroke: '#fff', 'stroke-width': 13, 'stroke-linecap': 'round', opacity: 0.85 }, g);
    el('line', { x1, y1, x2, y2, stroke: '#7A4B22', 'stroke-width': 7, 'stroke-linecap': 'round' }, g);
  });
  for (let d = 22; d < len - 10; d += 30) {
    const cx = a.x + ux * d, cy = a.y + uy * d;
    el('line', { x1: cx - nx * 13, y1: cy - ny * 13, x2: cx + nx * 13, y2: cy + ny * 13, stroke: '#B97A3F', 'stroke-width': 6, 'stroke-linecap': 'round' }, g);
  }
}

function drawSnake(head, tail, color) {
  const a = center(head), b = center(tail);
  const dx = b.x - a.x, dy = b.y - a.y, len = Math.hypot(dx, dy);
  const ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
  const waves = Math.max(2, Math.round(len / 110));
  let d = '';
  const N = 48;
  for (let i = 0; i <= N; i++) {
    const t = i / N;
    const amp = 17 * (0.35 + 0.65 * Math.sin(Math.PI * t));
    const off = amp * Math.sin(2 * Math.PI * waves * t);
    d += (i ? 'L' : 'M') + (a.x + dx * t + nx * off).toFixed(1) + ' ' + (a.y + dy * t + ny * off).toFixed(1);
  }
  const g = el('g', {}, gJumps);
  const common = { d, fill: 'none', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' };
  el('path', { ...common, stroke: '#fff', 'stroke-width': 24, opacity: 0.85 }, g);
  el('path', { ...common, stroke: color, 'stroke-width': 16 }, g);
  el('path', { ...common, stroke: 'rgba(255,255,255,.45)', 'stroke-width': 5, 'stroke-dasharray': '3 15' }, g);
  const angle = (Math.atan2(dy, dx) * 180) / Math.PI + 180;
  const h = el('g', { transform: `translate(${a.x} ${a.y}) rotate(${angle})` }, g);
  el('path', { d: 'M20 0 L38 -5 M20 0 L38 5', stroke: '#C8102E', 'stroke-width': 3, 'stroke-linecap': 'round', fill: 'none' }, h);
  el('ellipse', { cx: 6, cy: 0, rx: 24, ry: 17, fill: color, stroke: '#fff', 'stroke-width': 3 }, h);
  el('circle', { cx: 12, cy: -8, r: 5, fill: '#fff' }, h);
  el('circle', { cx: 12, cy: 8, r: 5, fill: '#fff' }, h);
  el('circle', { cx: 14, cy: -8, r: 2.4, fill: '#14202B' }, h);
  el('circle', { cx: 14, cy: 8, r: 2.4, fill: '#14202B' }, h);
}

function renderJumps() {
  gJumps.textContent = '';
  let n = 0;
  S.jumps.forEach(([from, to]) => {
    if (to > from) drawLadder(from, to);
    else drawSnake(from, to, SNAKE_COLORS[n++ % SNAKE_COLORS.length]);
  });
}

/* ---------- tokens ---------- */
function placeToken(i, sq, slide) {
  const c = center(sq), o = OFFSETS[i];
  const t = tokenEls[i];
  t.classList.toggle('slide', !!slide);
  t.style.transform = `translate(${c.x + o[0]}px, ${c.y + o[1]}px)`;
}

function buildTokens() {
  gTokens.textContent = '';
  tokenEls = [];
  S.players.forEach((p, i) => {
    const g = el('g', { class: 'token' }, gTokens);
    el('circle', { r: 16, fill: COLORS[i], stroke: '#fff', 'stroke-width': 3.5 }, g);
    const t = el('text', { 'text-anchor': 'middle', dy: '.35em', 'font-size': 17, 'font-weight': 800, fill: TEXT_ON[i] }, g);
    t.textContent = i + 1;
    tokenEls.push(g);
    disp[i] = p.pos;
    placeToken(i, p.pos, false);
  });
}

/* ---------- dice ---------- */
function setDie(v) {
  const d = $('#die');
  d.dataset.v = v;
  d.setAttribute('aria-label', v ? 'Die shows ' + v : 'Die, no roll yet');
}
async function rollDieAnimation(final) {
  const d = $('#die');
  sound.roll();
  d.classList.add('rolling');
  for (let i = 0; i < 7; i++) {
    setDie(1 + Math.floor(Math.random() * 6));
    await sleep(70);
  }
  d.classList.remove('rolling');
  setDie(final);
}

/* ---------- applying engine replies ---------- */
function apply(r, quiet) {
  if (r && r.jumps) {
    const oldN = S ? S.players.length : -1;
    S = r;
    renderJumps();
    if (S.players.length !== oldN || !S.active) {
      if (S.players.length !== oldN) buildTokens();
    }
  }
  if (!quiet && r && r.msg) setStatus(r.msg, r.ok ? 'ok' : 'err');
  if (r && !r.ok && r.msg !== 'offline') sound.error();
}

async function refreshAnalysis() {
  const r = await api('ANALYZE');
  if (r.analysis) {
    const a = r.analysis;
    $('#analysis').innerHTML =
      '<dl><dt>Ladders</dt><dd>' + a.ladders + '</dd>' +
      '<dt>Snakes</dt><dd>' + a.snakes + '</dd>' +
      '<dt>Fastest possible win</dt><dd>' + a.fastest + ' throws</dd>' +
      '<dt>Average game</dt><dd>' + a.average + ' throws</dd>' +
      '<dt>Difficulty</dt><dd><span class="level ' + a.difficulty + '">' + a.difficulty.toLowerCase() + '</span></dd></dl>';
  }
}

/* ---------- render the panel ---------- */
function render() {
  const live = S.active;
  $('#setup').hidden = live;
  $('#live').hidden = !live;
  $('#designLock').hidden = !live;
  $('#ruleExact').checked = !!S.exact;
  $('#ruleExtra').checked = !!S.extra;
  ['#defaultBtn', '#clearBtn'].forEach((id) => { $(id).disabled = live || busy; });
  $('#toolSeg').querySelectorAll('button').forEach((b) => { b.disabled = live; });

  document.querySelectorAll('.cell').forEach((c) => {
    const sq = Number(c.dataset.sq);
    const editing = !live && !busy;
    c.classList.toggle('edit', editing);
    c.classList.toggle('pending', editing && pendingFrom === sq);
    c.setAttribute('tabindex', editing ? '0' : '-1');
  });

  if (live) {
    const list = $('#playerList');
    list.textContent = '';
    S.players.forEach((p, i) => {
      const li = document.createElement('li');
      if (i === S.current) li.className = 'turn';
      li.innerHTML = '<span class="chip" style="background:' + COLORS[i] + ';color:' + TEXT_ON[i] + '">' + (i + 1) + '</span>' +
        '<span class="nm"></span><span class="meta"></span>';
      li.querySelector('.nm').textContent = p.name + (p.computer ? ' (computer)' : '');
      li.querySelector('.meta').textContent = 'Square ' + p.pos + ', ' + p.throws + ' throws';
      list.appendChild(li);
    });
    const cur = S.players[S.current];
    $('#rollBtn').disabled = busy || cur.computer;
    $('#hintBtn').disabled = busy || cur.computer;
    $('#rollBtn').textContent = cur.computer ? cur.name + ' is rolling…' : cur.name + ', roll dice';
  }
}

function showTab(name) {
  document.querySelectorAll('[role="tab"]').forEach((t) => {
    const on = t.dataset.tab === name;
    t.setAttribute('aria-selected', on);
    t.tabIndex = on ? 0 : -1;
  });
  document.querySelectorAll('.view').forEach((v) => { v.hidden = v.id !== 'view-' + name; });
  if (name === 'boards') loadBoardList();
  if (name === 'wins') loadRecords();
  if (name === 'design') refreshAnalysis();
}

/* ---------- playing ---------- */
function maybeComputerTurn() {
  clearTimeout(computerTimer);
  if (S && S.active && !busy && S.players[S.current].computer) {
    computerTimer = setTimeout(takeTurn, 900);
  }
}

async function takeTurn() {
  if (busy || !S.active) return;
  busy = true;
  $('#hintText').textContent = '';
  render();
  const r = await api('ROLL');
  if (!r.ok || !r.event) {
    busy = false;
    apply(r);
    render();
    return;
  }
  const ev = r.event;
  const name = S.players[ev.player].name;
  await rollDieAnimation(ev.roll);
  setStatus(name + ' rolled a ' + ev.roll + '.');

  if (ev.kind === 'bounce') {
    sound.error();
    tokenEls[ev.player].classList.add('shake');
    setStatus(name + ' rolled a ' + ev.roll + ' but needs an exact roll to land on 100. Staying on ' + ev.from + '.');
    await sleep(500);
    tokenEls[ev.player].classList.remove('shake');
  } else {
    for (let sq = ev.from + 1; sq <= ev.landed; sq++) {
      placeToken(ev.player, sq, false);
      sound.step();
      await sleep(170);
    }
    if (ev.kind === 'ladder' || ev.kind === 'snake') {
      const up = ev.kind === 'ladder';
      setStatus((up ? 'Ladder! ' : 'Snake! ') + name + (up ? ' climbed from ' : ' slid from ') + ev.landed + ' to ' + ev.final + '.', up ? 'ok' : 'err');
      (up ? sound.ladder : sound.snake)();
      await sleep(150);
      placeToken(ev.player, ev.final, true);
      await sleep(900);
    }
  }

  disp[ev.player] = ev.final;
  S = r;
  busy = false;
  render();

  if (r.winner >= 0) {
    const w = r.players[r.winner];
    sound.win();
    setStatus(w.name + ' wins in ' + w.throws + ' throws.', 'ok');
    $('#winTitle').textContent = w.name + ' wins';
    $('#winText').textContent = 'Reached square 100 in ' + w.throws + ' throws. The result is saved in Wins.';
    $('#winDlg').showModal();
    return;
  }
  if (ev.extra) setStatus(name + ' rolled a six and goes again.', 'ok');
  maybeComputerTurn();
}

async function startGame() {
  const parts = [];
  for (let i = 0; i < setupCount; i++) {
    const clean = setupPlayers[i].name.replace(/[^A-Za-z0-9 ]/g, '').trim().slice(0, 12).replace(/ +/g, '_');
    parts.push(setupPlayers[i].type + ':' + clean);
  }
  const r = await api('NEWGAME ' + setupCount + ' ' + parts.join(' '));
  if (!r.ok) { apply(r); return; }
  S = r;
  buildTokens();
  setDie(0);
  pendingFrom = null;
  $('#hintText').textContent = '';
  render();
  setStatus(S.players[0].name + ' starts on square 1.');
  maybeComputerTurn();
}

async function endGame() {
  clearTimeout(computerTimer);
  const r = await api('ENDGAME');
  busy = false;
  apply(r);
  buildTokens();
  render();
}

/* ---------- editor ---------- */
async function cellClick(sq) {
  if (!S || S.active || busy) return;
  if (tool === 'remove') {
    const j = S.jumps.find((p) => p[0] === sq || p[1] === sq);
    if (!j) { setStatus('Square ' + sq + ' has no snake or ladder.', 'err'); sound.error(); return; }
    apply(await api('REMOVE ' + j[0]));
    render();
    refreshAnalysis();
    return;
  }
  if (pendingFrom === null) {
    pendingFrom = sq;
    setStatus('Starting on ' + sq + '. Click the square where it ends, or press Escape to cancel.');
    render();
    return;
  }
  if (sq === pendingFrom) {
    pendingFrom = null;
    setStatus('Cancelled.');
    render();
    return;
  }
  const from = pendingFrom;
  pendingFrom = null;
  apply(await api('ADD ' + from + ' ' + sq));
  render();
  refreshAnalysis();
}

/* ---------- boards and records ---------- */
async function loadBoardList() {
  const ul = $('#boardList');
  let names = [];
  try { names = await (await fetch('/api/boards')).json(); } catch (e) { /* offline */ }
  ul.textContent = '';
  if (!names.length) {
    const li = document.createElement('li');
    li.className = 'empty';
    li.textContent = 'No saved boards yet. Design one, then save it here.';
    ul.appendChild(li);
    return;
  }
  names.forEach((n) => {
    const li = document.createElement('li');
    const span = document.createElement('span');
    span.className = 'nm';
    span.textContent = n;
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'ghost';
    b.textContent = 'Load';
    b.disabled = S.active;
    b.addEventListener('click', async () => {
      apply(await api('LOAD ' + n));
      render();
      refreshAnalysis();
    });
    li.append(span, b);
    ul.appendChild(li);
  });
}

async function loadRecords() {
  const r = await api('RECORDS');
  const ol = $('#recordList');
  ol.textContent = '';
  if (!r.records || !r.records.length) {
    const li = document.createElement('li');
    li.className = 'empty';
    li.textContent = 'No wins yet. Finish a game and it shows up here.';
    ol.appendChild(li);
    return;
  }
  r.records.forEach((t) => {
    const li = document.createElement('li');
    li.textContent = t;
    ol.appendChild(li);
  });
}

/* ---------- setup form ---------- */
function buildSetupRows() {
  const box = $('#playerRows');
  box.textContent = '';
  for (let i = 0; i < setupCount; i++) {
    const row = document.createElement('div');
    row.className = 'prow';
    row.innerHTML = '<span class="chip" style="background:' + COLORS[i] + ';color:' + TEXT_ON[i] + '">' + (i + 1) + '</span>' +
      '<input type="text" maxlength="12" autocomplete="off" aria-label="Name for player ' + (i + 1) + '" placeholder="' +
      (setupPlayers[i].type === 'c' ? 'Computer ' : 'Player ') + (i + 1) + '">' +
      '<select aria-label="Type of player ' + (i + 1) + '"><option value="h">Human</option><option value="c">Computer</option></select>';
    const input = row.querySelector('input'), select = row.querySelector('select');
    input.value = setupPlayers[i].name;
    select.value = setupPlayers[i].type;
    input.addEventListener('input', () => { setupPlayers[i].name = input.value; });
    select.addEventListener('change', () => {
      setupPlayers[i].type = select.value;
      input.placeholder = (select.value === 'c' ? 'Computer ' : 'Player ') + (i + 1);
    });
    box.appendChild(row);
  }
}

async function changeRules() {
  const r = await api('RULES ' + ($('#ruleExact').checked ? 1 : 0) + ' ' + ($('#ruleExtra').checked ? 1 : 0));
  apply(r);
  render();
  refreshAnalysis();
}

/* ---------- wiring ---------- */
function wire() {
  document.querySelectorAll('[role="tab"]').forEach((t) => {
    t.addEventListener('click', () => showTab(t.dataset.tab));
    t.addEventListener('keydown', (e) => {
      const tabs = [...document.querySelectorAll('[role="tab"]')];
      const i = tabs.indexOf(t);
      let n = null;
      if (e.key === 'ArrowRight') n = tabs[(i + 1) % tabs.length];
      if (e.key === 'ArrowLeft') n = tabs[(i + tabs.length - 1) % tabs.length];
      if (n) { e.preventDefault(); n.focus(); showTab(n.dataset.tab); }
    });
  });

  $('#countSeg').querySelectorAll('button').forEach((b) => {
    b.addEventListener('click', () => {
      setupCount = Number(b.dataset.n);
      $('#countSeg').querySelectorAll('button').forEach((x) => x.setAttribute('aria-pressed', x === b));
      buildSetupRows();
    });
  });
  $('#toolSeg').querySelectorAll('button').forEach((b) => {
    b.addEventListener('click', () => {
      tool = b.dataset.tool;
      pendingFrom = null;
      $('#toolSeg').querySelectorAll('button').forEach((x) => x.setAttribute('aria-pressed', x === b));
      render();
    });
  });

  $('#startBtn').addEventListener('click', startGame);
  $('#rollBtn').addEventListener('click', takeTurn);
  $('#endBtn').addEventListener('click', endGame);
  $('#hintBtn').addEventListener('click', async () => {
    const r = await api('HINT');
    if (r.ok) {
      $('#hintText').textContent = 'Fastest possible win from square ' + S.players[S.current].pos + ': ' + r.hint + ' throws.';
    }
  });
  $('#ruleExact').addEventListener('change', changeRules);
  $('#ruleExtra').addEventListener('change', changeRules);
  $('#defaultBtn').addEventListener('click', async () => { apply(await api('DEFAULT')); render(); refreshAnalysis(); });
  $('#clearBtn').addEventListener('click', async () => { apply(await api('CLEAR')); render(); refreshAnalysis(); });

  $('#saveForm').addEventListener('submit', async (e) => {
    e.preventDefault();
    const name = $('#saveName').value.trim();
    if (!name) { setStatus('Type a name for the board first.', 'err'); return; }
    apply(await api('SAVE ' + name));
    loadBoardList();
  });

  $('#againBtn').addEventListener('click', () => { $('#winDlg').close(); startGame(); });
  $('#closeWinBtn').addEventListener('click', () => { $('#winDlg').close(); });

  $('#soundBtn').addEventListener('click', () => {
    muted = !muted;
    $('#soundBtn').textContent = muted ? 'Sound off' : 'Sound on';
    $('#soundBtn').setAttribute('aria-pressed', muted);
    try { localStorage.setItem('snakeforge-muted', muted ? '1' : '0'); } catch (e) { /* ignore */ }
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && pendingFrom !== null) {
      pendingFrom = null;
      setStatus('Cancelled.');
      render();
    }
  });
}

/* ---------- start ---------- */
(async function init() {
  try { muted = localStorage.getItem('snakeforge-muted') === '1'; } catch (e) { /* ignore */ }
  $('#soundBtn').textContent = muted ? 'Sound off' : 'Sound on';
  $('#soundBtn').setAttribute('aria-pressed', muted);
  buildBoard();
  buildSetupRows();
  wire();
  const r = await api('STATE');
  if (!r.jumps) return;
  S = r;
  renderJumps();
  buildTokens();
  render();
  setStatus('Pick your players and start, or open Design to build your own board.');
})();
