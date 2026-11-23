/*
 * SnakeForge bridge server (Node.js, no extra packages needed)
 *
 *   browser  <--HTTP-->  server.js  <--stdin/stdout-->  engine (C program)
 *
 * This file does three small jobs:
 *   1. builds engine.c with GCC and starts it
 *   2. forwards commands from the page (POST /api) to the C engine
 *   3. serves the files in the public/ folder
 *
 * It contains NO game rules. All rules are in engine.c.
 */
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { spawn, execFileSync } = require('child_process');

const PORT = Number(process.env.PORT) || 3000;
const ROOT = __dirname;
const PUBLIC = path.join(ROOT, 'public');
const BOARDS = path.join(ROOT, 'boards');
const ENGINE = path.join(ROOT, process.platform === 'win32' ? 'engine.exe' : 'engine');
const ALLOWED = ['STATE', 'NEWGAME', 'ENDGAME', 'ROLL', 'HINT', 'ADD', 'REMOVE',
  'CLEAR', 'DEFAULT', 'RULES', 'ANALYZE', 'SAVE', 'LOAD', 'RECORDS'];

fs.mkdirSync(BOARDS, { recursive: true });

/* ---------- 1. Build and start the C engine ---------- */
try {
  execFileSync('gcc', ['-O2', '-o', ENGINE, path.join(ROOT, 'engine.c')], { stdio: 'inherit' });
  console.log('Built the C engine (engine.c).');
} catch (err) {
  if (!fs.existsSync(ENGINE)) {
    console.error('\nCould not build engine.c. Install GCC (MinGW on Windows) and make sure');
    console.error('"gcc --version" works in this terminal, or compile it yourself:');
    console.error('  gcc engine.c -o engine');
    process.exit(1);
  }
  console.warn('GCC not found, using the engine that was built earlier.');
}

const engine = spawn(ENGINE, [], { cwd: ROOT, stdio: ['pipe', 'pipe', 'inherit'] });
const waiting = [];            /* requests waiting for an answer, in order */
let shuttingDown = false;

readline.createInterface({ input: engine.stdout }).on('line', (line) => {
  const next = waiting.shift();
  if (next) next.resolve(line);
});

engine.on('exit', () => {
  if (shuttingDown) return;
  console.error('The C engine stopped unexpectedly.');
  while (waiting.length) waiting.shift().reject(new Error('Engine stopped'));
  process.exit(1);
});

function ask(command) {
  return new Promise((resolve, reject) => {
    waiting.push({ resolve, reject });
    engine.stdin.write(command + '\n');
  });
}

/* ---------- 2. HTTP server ---------- */
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function sendJson(res, status, text) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(text);
}

function handleApi(req, res) {
  if (!String(req.headers['content-type'] || '').startsWith('application/json')) {
    sendJson(res, 415, '{"ok":0,"msg":"Send JSON."}');
    return;
  }
  let body = '';
  req.on('data', (chunk) => {
    body += chunk;
    if (body.length > 2000) req.destroy();
  });
  req.on('end', async () => {
    let command = '';
    try {
      command = String(JSON.parse(body).cmd || '');
    } catch (e) {
      sendJson(res, 400, '{"ok":0,"msg":"Bad request."}');
      return;
    }
    command = command.replace(/[^A-Za-z0-9_: .\-]/g, '').trim().slice(0, 150);
    if (!ALLOWED.includes(command.split(' ')[0])) {
      sendJson(res, 400, '{"ok":0,"msg":"Unknown command."}');
      return;
    }
    try {
      sendJson(res, 200, await ask(command));
    } catch (e) {
      sendJson(res, 500, '{"ok":0,"msg":"The C engine is not running."}');
    }
  });
}

function listBoards() {
  return fs.readdirSync(BOARDS)
    .filter((f) => /^[A-Za-z0-9_-]{1,30}\.txt$/.test(f))
    .map((f) => f.slice(0, -4))
    .sort();
}

function serveFile(req, res, pathname) {
  let rel;
  try {
    rel = decodeURIComponent(pathname);
  } catch (e) {
    res.writeHead(400); res.end('Bad request'); return;
  }
  if (rel === '/') rel = '/index.html';
  const file = path.normalize(path.join(PUBLIC, rel));
  if (!file.startsWith(PUBLIC + path.sep)) {
    res.writeHead(403); res.end('Forbidden'); return;
  }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('Not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    res.end(data);
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (req.method === 'POST' && url.pathname === '/api') {
    handleApi(req, res);
  } else if (req.method === 'GET' && url.pathname === '/api/boards') {
    sendJson(res, 200, JSON.stringify(listBoards()));
  } else if (req.method === 'GET') {
    serveFile(req, res, url.pathname);
  } else {
    res.writeHead(405); res.end('Method not allowed');
  }
});

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`Port ${PORT} is already in use. Close the other program or run: PORT=3001 node server.js`);
  } else {
    console.error(err.message);
  }
  engine.kill();
  process.exit(1);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`SnakeForge is running: http://localhost:${PORT}`);
  console.log('Press Ctrl+C to stop.');
});

process.on('SIGINT', () => {
  shuttingDown = true;
  engine.kill();
  process.exit(0);
});
