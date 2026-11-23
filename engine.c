/*
 * SnakeForge ENGINE - the C backend of the web version
 * Programming Fundamentals Project
 *
 * The browser (HTML/CSS/JS) never touches the game rules. It sends ONE text
 * command per line to this program (through the small Node.js bridge) and
 * gets back ONE line of JSON. Board, dice, players, validation, solver,
 * files: everything lives here in C.
 *
 * Commands:
 *   STATE                       send the current state
 *   NEWGAME n h:Name c:Name ... start a game (h = human, c = computer)
 *   ENDGAME                     stop the current game
 *   ROLL                        current player rolls the dice and moves
 *   HINT                        fewest throws needed from the current square
 *   ADD from to                 add a ladder (to > from) or a snake (to < from)
 *   REMOVE square               remove the snake/ladder that starts there
 *   CLEAR / DEFAULT             empty board / classic board
 *   RULES exact extra           1 = on, 0 = off
 *   ANALYZE                     snakes, ladders, fastest win, difficulty
 *   SAVE name / LOAD name       board files in the boards/ folder
 *   RECORDS                     list of past winners
 *
 * Simple concepts only: arrays, loops, functions, if/else, file handling.
 *
 * Board: jump[square] = where that square sends you (0 = normal square).
 *        jump[sq] > sq is a LADDER, jump[sq] < sq is a SNAKE.
 *
 * Compile: gcc engine.c -o engine
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define BOARD_SIZE 100
#define MAX_PLAYERS 4
#define MAX_RECORDS 100
#define LOCKED_MSG "Finish or end the current game before changing the board."

/* ---------- Global data ---------- */
int jump[BOARD_SIZE + 1];
int exactFinish = 1;
int extraTurnOnSix = 1;

int numPlayers = 0;
int position[MAX_PLAYERS];
int throwsCount[MAX_PLAYERS];
int isComputer[MAX_PLAYERS];
char names[MAX_PLAYERS][20];
int current = 0;
int gameActive = 0;
int winner = -1;

/* What happened on the last ROLL (the browser uses it to animate) */
int evPlayer = -1, evRoll = 0, evFrom = 0, evLanded = 0, evFinal = 0, evExtra = 0;
char evKind[10] = "none";    /* none, ladder, snake or bounce */

/* ====================================================== */
/*                    BOARD FUNCTIONS                     */
/* ====================================================== */

void clearBoard(void)
{
    int i;
    for (i = 0; i <= BOARD_SIZE; i++)
    {
        jump[i] = 0;
    }
}

void loadDefaultBoard(void)
{
    int ladderFrom[] = {2, 7, 8, 15, 21, 28, 36, 51, 71, 78, 87};
    int ladderTo[]   = {38, 14, 31, 26, 42, 84, 44, 67, 91, 98, 94};
    int snakeFrom[] = {16, 46, 49, 62, 64, 74, 89, 92, 95, 99};
    int snakeTo[]   = {6, 25, 11, 19, 60, 53, 68, 88, 75, 80};
    int i;

    clearBoard();
    for (i = 0; i < 11; i++)
    {
        jump[ladderFrom[i]] = ladderTo[i];
    }
    for (i = 0; i < 10; i++)
    {
        jump[snakeFrom[i]] = snakeTo[i];
    }
}

/* ====================================================== */
/*          SOLVER (Breadth-First Search on the board)    */
/* ====================================================== */

/* dist[x] = fewest throws to reach square x from 'start' (-1 = unreachable) */
void bfs(int start, int dist[])
{
    int queue[BOARD_SIZE + 1];
    int head = 0, tail = 0;
    int i, cur, roll, next;

    for (i = 0; i <= BOARD_SIZE; i++)
    {
        dist[i] = -1;
    }
    dist[start] = 0;
    queue[tail++] = start;

    while (head < tail)
    {
        cur = queue[head++];
        for (roll = 1; roll <= 6; roll++)
        {
            next = cur + roll;
            if (next > BOARD_SIZE)
            {
                if (exactFinish) continue;
                next = BOARD_SIZE;
            }
            if (jump[next] != 0)
            {
                next = jump[next];
            }
            if (dist[next] == -1)
            {
                dist[next] = dist[cur] + 1;
                queue[tail++] = next;
            }
        }
    }
}

int minThrowsFrom(int start)
{
    int dist[BOARD_SIZE + 1];
    bfs(start, dist);
    return dist[BOARD_SIZE];
}

/* Playable = from every square a player can stand on, 100 is still reachable */
int isPlayable(void)
{
    int fromStart[BOARD_SIZE + 1];
    int s;

    bfs(1, fromStart);
    for (s = 1; s <= BOARD_SIZE; s++)
    {
        if (fromStart[s] != -1 && minThrowsFrom(s) == -1)
        {
            return 0;
        }
    }
    return fromStart[BOARD_SIZE] != -1;
}

double averageThrows(void)
{
    int games, pos, throws, roll, next;
    long total = 0;
    int numGames = 500;

    for (games = 0; games < numGames; games++)
    {
        pos = 1;
        throws = 0;
        while (pos != BOARD_SIZE && throws < 5000)
        {
            roll = rand() % 6 + 1;
            throws++;
            next = pos + roll;
            if (next > BOARD_SIZE)
            {
                if (exactFinish) continue;
                next = BOARD_SIZE;
            }
            if (jump[next] != 0) next = jump[next];
            pos = next;
        }
        total += throws;
    }
    return (double)total / numGames;
}

/* ====================================================== */
/*                  REPLY (JSON OUTPUT)                   */
/* ====================================================== */

void printState(void)
{
    int i, first = 1;

    printf("\"jumps\":[");
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] != 0)
        {
            if (!first) printf(",");
            printf("[%d,%d]", i, jump[i]);
            first = 0;
        }
    }
    printf("],\"players\":[");
    for (i = 0; i < numPlayers; i++)
    {
        if (i > 0) printf(",");
        printf("{\"name\":\"%s\",\"pos\":%d,\"computer\":%d,\"throws\":%d}",
               names[i], position[i], isComputer[i], throwsCount[i]);
    }
    printf("],\"current\":%d,\"active\":%d,\"winner\":%d,\"exact\":%d,\"extra\":%d",
           current, gameActive, winner, exactFinish, extraTurnOnSix);
    if (evPlayer >= 0)
    {
        printf(",\"event\":{\"player\":%d,\"roll\":%d,\"from\":%d,\"landed\":%d,"
               "\"final\":%d,\"kind\":\"%s\",\"extra\":%d}",
               evPlayer, evRoll, evFrom, evLanded, evFinal, evKind, evExtra);
    }
}

/* Every command ends with exactly one reply line */
void reply(int ok, const char msg[], const char extra[])
{
    printf("{\"ok\":%d,\"msg\":\"%s\",", ok, msg);
    printState();
    if (extra[0] != '\0')
    {
        printf(",%s", extra);
    }
    printf("}\n");
    fflush(stdout);
}

/* ====================================================== */
/*                    SMALL HELPERS                       */
/* ====================================================== */

int parseNumber(const char text[], int *value)
{
    char *end;
    long v;

    if (text == NULL) return 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0') return 0;
    if (v < -1000000 || v > 1000000) return 0;
    *value = (int)v;
    return 1;
}

int validName(const char name[])
{
    int i, len = (int)strlen(name);

    if (len < 1 || len > 30) return 0;
    for (i = 0; i < len; i++)
    {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-')
        {
            return 0;
        }
    }
    return 1;
}

/* ====================================================== */
/*                     BOARD EDITING                      */
/* ====================================================== */

/* Returns NULL when added, otherwise the reason it was rejected. */
const char *addJump(int from, int to)
{
    int i;

    if (from < 2 || from > 99 || to < 1 || to > 99)
    {
        return "Squares must be 2 to 99 (a snake tail can be square 1).";
    }
    if (from == to)
    {
        return "Start and end must be different squares.";
    }
    if (jump[from] != 0)
    {
        return "That square already has a snake or a ladder.";
    }
    if (jump[to] != 0)
    {
        return "The end square is the start of another snake or ladder.";
    }
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] == from)
        {
            return "The start square is the end of another snake or ladder.";
        }
    }

    jump[from] = to;
    if (!isPlayable())
    {
        jump[from] = 0;
        return "This would make the board impossible to finish.";
    }
    return NULL;
}

/* ====================================================== */
/*                     FILE HANDLING                      */
/* ====================================================== */

const char *saveBoardFile(const char name[])
{
    char path[80];
    FILE *file;
    int i;

    if (!validName(name))
    {
        return "Board names can use letters, numbers, - and _ (up to 30).";
    }
    sprintf(path, "boards/%s.txt", name);
    file = fopen(path, "w");
    if (file == NULL)
    {
        return "Could not save the file. Is there a boards folder?";
    }
    fprintf(file, "%d %d\n", exactFinish, extraTurnOnSix);
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] != 0)
        {
            fprintf(file, "%d %d\n", i, jump[i]);
        }
    }
    fclose(file);
    return NULL;
}

const char *loadBoardFile(const char name[])
{
    char path[80];
    FILE *file;
    int backup[BOARD_SIZE + 1];
    int oldExact = exactFinish, oldExtra = extraTurnOnSix;
    int from, to, i, e, x;

    if (!validName(name))
    {
        return "Board names can use letters, numbers, - and _ (up to 30).";
    }
    sprintf(path, "boards/%s.txt", name);
    file = fopen(path, "r");
    if (file == NULL)
    {
        return "Board file not found.";
    }
    if (fscanf(file, "%d %d", &e, &x) != 2)
    {
        fclose(file);
        return "That is not a valid board file.";
    }

    for (i = 0; i <= BOARD_SIZE; i++) backup[i] = jump[i];
    exactFinish = e ? 1 : 0;
    extraTurnOnSix = x ? 1 : 0;
    clearBoard();
    while (fscanf(file, "%d %d", &from, &to) == 2)
    {
        if (from >= 2 && from <= 99 && to >= 1 && to <= 99 && from != to)
        {
            jump[from] = to;
        }
    }
    fclose(file);

    if (!isPlayable())
    {
        for (i = 0; i <= BOARD_SIZE; i++) jump[i] = backup[i];
        exactFinish = oldExact;
        extraTurnOnSix = oldExtra;
        return "That board cannot be finished, so it was not loaded.";
    }
    return NULL;
}

void saveRecord(const char winnerName[], int throws)
{
    FILE *file = fopen("records.txt", "a");
    if (file == NULL) return;
    fprintf(file, "%s won in %d throws\n", winnerName, throws);
    fclose(file);
}

void cmdRecords(void)
{
    FILE *file = fopen("records.txt", "r");
    char lines[MAX_RECORDS][100];
    char line[100];
    int count = 0, i, j;
    unsigned char ch;

    if (file != NULL)
    {
        while (fgets(line, sizeof(line), file) != NULL)
        {
            line[strcspn(line, "\r\n")] = '\0';
            if (count == MAX_RECORDS)
            {
                for (i = 1; i < MAX_RECORDS; i++) strcpy(lines[i - 1], lines[i]);
                count--;
            }
            strcpy(lines[count], line);
            count++;
        }
        fclose(file);
    }

    printf("{\"ok\":1,\"msg\":\"\",\"records\":[");
    for (i = count - 1; i >= 0; i--)    /* newest first */
    {
        printf("\"");
        for (j = 0; lines[i][j] != '\0'; j++)
        {
            ch = (unsigned char)lines[i][j];
            if (ch >= 32 && ch < 127 && ch != '"' && ch != '\\') putchar(ch);
        }
        printf("\"");
        if (i > 0) printf(",");
    }
    printf("]}\n");
    fflush(stdout);
}

/* ====================================================== */
/*                       THE GAME                         */
/* ====================================================== */

void cmdNewGame(void)
{
    char *token;
    int n, i, j, k, letters;
    char kinds[MAX_PLAYERS];
    char newNames[MAX_PLAYERS][20];
    char ch;

    token = strtok(NULL, " ");
    if (!parseNumber(token, &n) || n < 2 || n > MAX_PLAYERS)
    {
        reply(0, "Choose 2 to 4 players.", "");
        return;
    }
    for (i = 0; i < n; i++)
    {
        token = strtok(NULL, " ");
        if (token == NULL || token[1] != ':' || (token[0] != 'h' && token[0] != 'c'))
        {
            reply(0, "Each player must look like h:Name or c:Name.", "");
            return;
        }
        kinds[i] = token[0];
        k = 0;
        letters = 0;
        for (j = 2; token[j] != '\0' && k < 19; j++)
        {
            ch = token[j];
            if (isalnum((unsigned char)ch))
            {
                newNames[i][k++] = ch;
                letters++;
            }
            else if (ch == '_')
            {
                newNames[i][k++] = ' ';
            }
        }
        newNames[i][k] = '\0';
        if (letters == 0)
        {
            sprintf(newNames[i], kinds[i] == 'c' ? "Computer %d" : "Player %d", i + 1);
        }
    }

    numPlayers = n;
    for (i = 0; i < n; i++)
    {
        strcpy(names[i], newNames[i]);
        isComputer[i] = (kinds[i] == 'c');
        position[i] = 1;
        throwsCount[i] = 0;
    }
    current = 0;
    winner = -1;
    gameActive = 1;
    reply(1, "Game started.", "");
}

void cmdEndGame(void)
{
    gameActive = 0;
    winner = -1;
    numPlayers = 0;
    current = 0;
    reply(1, "Game ended.", "");
}

void doRoll(void)
{
    int roll, from, next;

    if (!gameActive)
    {
        reply(0, "No game is running. Start a new game first.", "");
        return;
    }

    roll = rand() % 6 + 1;
    throwsCount[current]++;
    from = position[current];
    next = from + roll;
    strcpy(evKind, "none");

    if (next > BOARD_SIZE)
    {
        if (exactFinish)
        {
            strcpy(evKind, "bounce");     /* needs an exact roll: stay */
            next = from;
        }
        else
        {
            next = BOARD_SIZE;
        }
        evLanded = next;
    }
    else
    {
        evLanded = next;
        if (jump[next] != 0)
        {
            strcpy(evKind, jump[next] > next ? "ladder" : "snake");
            next = jump[next];
        }
    }

    position[current] = next;
    evPlayer = current;
    evRoll = roll;
    evFrom = from;
    evFinal = next;
    evExtra = 0;

    if (next == BOARD_SIZE)
    {
        winner = current;
        gameActive = 0;
        saveRecord(names[current], throwsCount[current]);
        reply(1, "We have a winner.", "");
        return;
    }
    if (roll == 6 && extraTurnOnSix)
    {
        evExtra = 1;                      /* same player rolls again */
    }
    else
    {
        current = (current + 1) % numPlayers;
    }
    reply(1, "", "");
}

void cmdHint(void)
{
    char extra[40];

    if (!gameActive)
    {
        reply(0, "No game is running.", "");
        return;
    }
    sprintf(extra, "\"hint\":%d", minThrowsFrom(position[current]));
    reply(1, "", extra);
}

void cmdAnalyze(void)
{
    int i, snakes = 0, ladders = 0;
    double average;
    const char *level;
    char extra[200];

    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] > i) ladders++;
        else if (jump[i] != 0) snakes++;
    }
    average = averageThrows();
    if (average < 30) level = "EASY";
    else if (average < 45) level = "MEDIUM";
    else level = "HARD";

    sprintf(extra,
            "\"analysis\":{\"snakes\":%d,\"ladders\":%d,\"fastest\":%d,"
            "\"average\":%.1f,\"difficulty\":\"%s\"}",
            snakes, ladders, minThrowsFrom(1), average, level);
    reply(1, "", extra);
}

void cmdRules(void)
{
    char *a = strtok(NULL, " ");
    char *b = strtok(NULL, " ");
    int e, x;
    int oldExact = exactFinish, oldExtra = extraTurnOnSix;

    if (gameActive)
    {
        reply(0, "Rules cannot change during a game.", "");
        return;
    }
    if (!parseNumber(a, &e) || !parseNumber(b, &x))
    {
        reply(0, "RULES needs two values: 0 or 1.", "");
        return;
    }
    exactFinish = e ? 1 : 0;
    extraTurnOnSix = x ? 1 : 0;
    if (!isPlayable())
    {
        exactFinish = oldExact;
        extraTurnOnSix = oldExtra;
        reply(0, "This board cannot be finished with that rule.", "");
        return;
    }
    reply(1, "Rules updated.", "");
}

/* ====================================================== */
/*                         MAIN                           */
/* ====================================================== */

int main(void)
{
    char line[200];
    char *cmd, *a, *b;
    int x, y, old;
    const char *err;

    srand((unsigned)time(NULL));
    loadDefaultBoard();

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        evPlayer = -1;
        strcpy(evKind, "none");

        cmd = strtok(line, " ");
        if (cmd == NULL)
        {
            reply(0, "Empty command.", "");
        }
        else if (strcmp(cmd, "STATE") == 0)
        {
            reply(1, "", "");
        }
        else if (strcmp(cmd, "NEWGAME") == 0)
        {
            cmdNewGame();
        }
        else if (strcmp(cmd, "ENDGAME") == 0)
        {
            cmdEndGame();
        }
        else if (strcmp(cmd, "ROLL") == 0)
        {
            doRoll();
        }
        else if (strcmp(cmd, "HINT") == 0)
        {
            cmdHint();
        }
        else if (strcmp(cmd, "ANALYZE") == 0)
        {
            cmdAnalyze();
        }
        else if (strcmp(cmd, "RULES") == 0)
        {
            cmdRules();
        }
        else if (strcmp(cmd, "RECORDS") == 0)
        {
            cmdRecords();
        }
        else if (strcmp(cmd, "ADD") == 0)
        {
            a = strtok(NULL, " ");
            b = strtok(NULL, " ");
            if (gameActive)
            {
                reply(0, LOCKED_MSG, "");
            }
            else if (!parseNumber(a, &x) || !parseNumber(b, &y))
            {
                reply(0, "ADD needs two square numbers.", "");
            }
            else
            {
                err = addJump(x, y);
                if (err != NULL) reply(0, err, "");
                else reply(1, y > x ? "Ladder added." : "Snake added.", "");
            }
        }
        else if (strcmp(cmd, "REMOVE") == 0)
        {
            a = strtok(NULL, " ");
            if (gameActive)
            {
                reply(0, LOCKED_MSG, "");
            }
            else if (!parseNumber(a, &x) || x < 1 || x > BOARD_SIZE)
            {
                reply(0, "REMOVE needs a square from 1 to 100.", "");
            }
            else if (jump[x] == 0)
            {
                reply(0, "There is no snake or ladder starting on that square.", "");
            }
            else
            {
                old = jump[x];
                jump[x] = 0;
                if (!isPlayable())
                {
                    jump[x] = old;
                    reply(0, "Cannot remove it: the board would become impossible to finish.", "");
                }
                else
                {
                    reply(1, "Removed.", "");
                }
            }
        }
        else if (strcmp(cmd, "CLEAR") == 0)
        {
            if (gameActive) { reply(0, LOCKED_MSG, ""); }
            else { clearBoard(); reply(1, "Board cleared.", ""); }
        }
        else if (strcmp(cmd, "DEFAULT") == 0)
        {
            if (gameActive) { reply(0, LOCKED_MSG, ""); }
            else { loadDefaultBoard(); reply(1, "Classic board loaded.", ""); }
        }
        else if (strcmp(cmd, "SAVE") == 0)
        {
            a = strtok(NULL, " ");
            if (a == NULL) { reply(0, "SAVE needs a board name.", ""); }
            else
            {
                err = saveBoardFile(a);
                if (err != NULL) reply(0, err, "");
                else reply(1, "Board saved.", "");
            }
        }
        else if (strcmp(cmd, "LOAD") == 0)
        {
            a = strtok(NULL, " ");
            if (gameActive) { reply(0, LOCKED_MSG, ""); }
            else if (a == NULL) { reply(0, "LOAD needs a board name.", ""); }
            else
            {
                err = loadBoardFile(a);
                if (err != NULL) reply(0, err, "");
                else reply(1, "Board loaded.", "");
            }
        }
        else
        {
            reply(0, "Unknown command.", "");
        }
    }
    return 0;
}
