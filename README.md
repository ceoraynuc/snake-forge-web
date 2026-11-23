# SnakeForge (web version)

Snakes and Ladders where you design your own board, then play it against friends or the computer.

- **Backend: C** (`engine.c`): board, dice, turns, validation, BFS solver, save/load, win records
- **Front end: HTML + CSS + JavaScript** (`public/`): draws the board, dice, tokens, editor
- **Bridge: Node.js** (`server.js`): a small file that connects the page to the C program

## How to run

You need **GCC** (MinGW on Windows) and **Node.js** installed.

```
node server.js
```

Then open http://localhost:3000 in your browser. `server.js` compiles `engine.c` for you on start.
If you prefer to compile by hand: `gcc engine.c -o engine`.

## How the parts talk

```
Browser (app.js)  --HTTP-->  server.js  --one line of text-->  engine (C)
                  <--JSON--              <--one line of JSON--
```

The page never decides a rule. For example, when you click two squares in the editor, the page sends
`ADD 30 60`, and the C engine checks it and answers with the new board or an error message.

Commands the C engine understands: `STATE`, `NEWGAME`, `ENDGAME`, `ROLL`, `HINT`, `ADD`, `REMOVE`,
`CLEAR`, `DEFAULT`, `RULES`, `ANALYZE`, `SAVE`, `LOAD`, `RECORDS`.

## Features

- Board editor with validation (snake head above tail, no chained jumps, board must stay finishable)
- Live difficulty rating and fastest possible win (BFS) shown while you edit
- Save and load boards (text files in `boards/`)
- 2 to 4 players, humans and computers
- Hint button: fewest throws needed from your square
- Optional rules: exact roll to finish, roll again after a six
- Animated dice and token movement, sound effects, win records (`records.txt`)

## C concepts used in `engine.c`

Arrays (`jump[101]` is the whole board), loops, functions, if/else, file handling (`fopen`, `fprintf`, `fscanf`).
Ladder or snake is decided by one comparison: `jump[sq] > sq` is a ladder, smaller is a snake.

## Files

```
engine.c        C backend
server.js       Node bridge + web server
public/         index.html, style.css, app.js
boards/         saved boards
snakeforge.c    (optional) the earlier console-only version
```
