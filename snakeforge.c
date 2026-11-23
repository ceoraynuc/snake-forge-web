/*
 * SnakeForge - Snakes and Ladders with a board editor
 * Programming Fundamentals Project
 *
 * Simple concepts used: variables, arrays, loops, functions,
 * if/else, switch, and file handling. No structs or pointers
 * tricks, so it is easy to read and explain.
 *
 * How the board works:
 *   jump[square] = where that square sends you
 *   jump[square] = 0  -> normal square (nothing happens)
 *   if jump[sq] > sq  -> it is a LADDER (goes up)
 *   if jump[sq] < sq  -> it is a SNAKE  (goes down)
 *
 * Compile:  gcc snakeforge.c -o snakeforge
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOARD_SIZE 100
#define MAX_PLAYERS 4

/* ---------- Global data ---------- */
int jump[BOARD_SIZE + 1];     /* the board (index 1..100) */
int exactFinish = 1;          /* rule: need exact roll to land on 100 */
int extraTurnOnSix = 1;       /* rule: rolling a six gives another turn */

int numPlayers;
int position[MAX_PLAYERS];
int throwsCount[MAX_PLAYERS];
int isComputer[MAX_PLAYERS];
char names[MAX_PLAYERS][20];

/* ====================================================== */
/*                  INPUT HELPER FUNCTIONS                */
/* ====================================================== */

/* Reads one line of text safely. Returns 0 if input ended. */
int readLine(char text[], int size)
{
    if (fgets(text, size, stdin) == NULL)
    {
        text[0] = '\0';
        return 0;
    }
    text[strcspn(text, "\r\n")] = '\0';
    return 1;
}

/* Keeps asking until the user types a number between min and max. */
int readInt(const char prompt[], int min, int max)
{
    char line[50];
    int value;

    while (1)
    {
        printf("%s", prompt);
        if (!readLine(line, sizeof(line)))
        {
            printf("\nInput ended. Goodbye!\n");
            exit(0);
        }
        if (sscanf(line, "%d", &value) == 1 && value >= min && value <= max)
        {
            return value;
        }
        printf("Please enter a number from %d to %d.\n", min, max);
    }
}

int rollDice(void)
{
    return rand() % 6 + 1;
}

/* ====================================================== */
/*                BOARD SETUP AND DISPLAY                 */
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
    /* ladders: bottom -> top */
    int ladderFrom[] = {2, 7, 8, 15, 21, 28, 36, 51, 71, 78, 87};
    int ladderTo[]   = {38, 14, 31, 26, 42, 84, 44, 67, 91, 98, 94};
    /* snakes: head -> tail */
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

/* Row 0 is the bottom row. Rows go left-to-right, then right-to-left. */
int squareNumber(int row, int col)
{
    if (row % 2 == 0)
    {
        return row * 10 + col + 1;
    }
    return row * 10 + (9 - col) + 1;
}

/* Draws the 10x10 board. Each cell shows its number and, if it has a
   ladder (L>top) or snake (S>tail). Second line shows player tokens. */
void printBoard(int showPlayers)
{
    int row, col, i, len, sq;
    char tag[10];
    char tokens[10];

    printf("\n");
    for (row = 9; row >= 0; row--)
    {
        for (col = 0; col < 10; col++)
        {
            sq = squareNumber(row, col);
            if (jump[sq] != 0)
            {
                sprintf(tag, "%c>%d", jump[sq] > sq ? 'L' : 'S', jump[sq]);
            }
            else
            {
                tag[0] = '\0';
            }
            printf("%3d %-4s|", sq, tag);
        }
        printf("\n");

        for (col = 0; col < 10; col++)
        {
            sq = squareNumber(row, col);
            tokens[0] = '\0';
            if (showPlayers)
            {
                for (i = 0; i < numPlayers; i++)
                {
                    if (position[i] == sq)
                    {
                        len = (int)strlen(tokens);
                        tokens[len] = (char)('1' + i);
                        tokens[len + 1] = '\0';
                    }
                }
            }
            printf("    %-4s|", tokens);
        }
        printf("\n");
        for (col = 0; col < 10; col++)
        {
            printf("--------+");
        }
        printf("\n");
    }
    printf("L>n = ladder up to n    S>n = snake down to n\n");
}

void listJumps(void)
{
    int i, ladders = 0, snakes = 0;

    printf("\nLadders: ");
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] > i)
        {
            printf("%d->%d  ", i, jump[i]);
            ladders++;
        }
    }
    if (ladders == 0) printf("none");

    printf("\nSnakes:  ");
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] != 0 && jump[i] < i)
        {
            printf("%d->%d  ", i, jump[i]);
            snakes++;
        }
    }
    if (snakes == 0) printf("none");
    printf("\n");
}

/* ====================================================== */
/*          SOLVER (Breadth-First Search on the board)    */
/* ====================================================== */

/* Fills dist[] with the minimum number of throws needed to reach every
   square from 'start'. dist[x] = -1 means square x cannot be reached. */
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
                if (exactFinish) continue;   /* roll not allowed */
                next = BOARD_SIZE;
            }
            if (jump[next] != 0)
            {
                next = jump[next];           /* snake or ladder */
            }
            if (dist[next] == -1)
            {
                dist[next] = dist[cur] + 1;
                queue[tail++] = next;
            }
        }
    }
}

/* Fewest throws needed to win from 'start' (-1 if impossible). */
int minThrowsFrom(int start)
{
    int dist[BOARD_SIZE + 1];
    bfs(start, dist);
    return dist[BOARD_SIZE];
}

/* A board is playable if from every square a player can really
   stand on, it is still possible to reach square 100. */
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

/* Plays 500 quick random games and returns the average throws needed. */
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
            roll = rollDice();
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

void analyzeBoard(void)
{
    int i, snakes = 0, ladders = 0;
    int fastest;
    double average;

    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] > i) ladders++;
        else if (jump[i] != 0) snakes++;
    }
    fastest = minThrowsFrom(1);
    average = averageThrows();

    printf("\n=== Board Analysis ===\n");
    printf("Snakes: %d   Ladders: %d\n", snakes, ladders);
    printf("Fastest possible win: %d throws\n", fastest);
    printf("Average game length : %.1f throws (500 test games)\n", average);
    printf("Difficulty: ");
    if (average < 30) printf("EASY\n");
    else if (average < 45) printf("MEDIUM\n");
    else printf("HARD\n");
}

/* ====================================================== */
/*                     BOARD EDITOR                       */
/* ====================================================== */

/* Tries to add a snake (isSnake = 1) or ladder (isSnake = 0).
   Returns 1 if added, 0 if rejected (reason is printed). */
int addJump(int from, int to, int isSnake)
{
    int i;

    if (from < 2 || from > 99 || to < 1 || to > 99)
    {
        printf("Invalid: squares must be 2-99 (a snake tail may be 1).\n");
        return 0;
    }
    if (isSnake && from <= to)
    {
        printf("Invalid: a snake head must be HIGHER than its tail.\n");
        return 0;
    }
    if (!isSnake && from >= to)
    {
        printf("Invalid: a ladder top must be HIGHER than its bottom.\n");
        return 0;
    }
    if (jump[from] != 0)
    {
        printf("Invalid: square %d already has a snake or ladder.\n", from);
        return 0;
    }
    if (jump[to] != 0)
    {
        printf("Invalid: square %d is the start of another snake/ladder.\n", to);
        return 0;
    }
    for (i = 1; i <= BOARD_SIZE; i++)
    {
        if (jump[i] == from)
        {
            printf("Invalid: square %d is the end of another snake/ladder.\n", from);
            return 0;
        }
    }

    jump[from] = to;
    if (!isPlayable())
    {
        jump[from] = 0;   /* undo */
        printf("Invalid: this would make the board impossible to finish.\n");
        return 0;
    }
    return 1;
}

void editorAddLadder(void)
{
    int bottom = readInt("Ladder bottom square (2-98): ", 2, 98);
    int top = readInt("Ladder top square (3-99): ", 3, 99);
    if (addJump(bottom, top, 0))
    {
        printf("Ladder added: %d -> %d\n", bottom, top);
    }
}

void editorAddSnake(void)
{
    int head = readInt("Snake head - higher square (2-99): ", 2, 99);
    int tail = readInt("Snake tail - lower square (1-98): ", 1, 98);
    if (addJump(head, tail, 1))
    {
        printf("Snake added: %d -> %d\n", head, tail);
    }
}

void editorRemove(void)
{
    int sq = readInt("Start square (ladder bottom / snake head): ", 1, BOARD_SIZE);
    int old = jump[sq];

    if (old == 0)
    {
        printf("There is no snake or ladder on square %d.\n", sq);
        return;
    }
    jump[sq] = 0;
    if (!isPlayable())
    {
        jump[sq] = old;   /* undo */
        printf("Cannot remove it: the board would become impossible to finish.\n");
        return;
    }
    printf("Removed the jump on square %d.\n", sq);
}

void boardEditor(void)
{
    int choice;

    do
    {
        printf("\n--- BOARD EDITOR ---\n");
        printf("1. Add ladder\n");
        printf("2. Add snake\n");
        printf("3. Remove a snake/ladder\n");
        printf("4. Show board\n");
        printf("5. List all snakes and ladders\n");
        printf("6. Clear board (empty)\n");
        printf("7. Load default board\n");
        printf("0. Back to main menu\n");
        choice = readInt("Choose: ", 0, 7);

        switch (choice)
        {
            case 1: editorAddLadder(); break;
            case 2: editorAddSnake(); break;
            case 3: editorRemove(); break;
            case 4: printBoard(0); break;
            case 5: listJumps(); break;
            case 6: clearBoard(); printf("Board cleared.\n"); break;
            case 7: loadDefaultBoard(); printf("Default board loaded.\n"); break;
        }
    } while (choice != 0);
}

/* ====================================================== */
/*                  FILE HANDLING                         */
/* ====================================================== */

void saveBoard(void)
{
    char filename[50];
    FILE *file;
    int i;

    printf("File name to save (Enter = board.txt): ");
    readLine(filename, sizeof(filename));
    if (filename[0] == '\0') strcpy(filename, "board.txt");

    file = fopen(filename, "w");
    if (file == NULL)
    {
        printf("Could not create the file.\n");
        return;
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
    printf("Board saved to %s\n", filename);
}

void loadBoard(void)
{
    char filename[50];
    FILE *file;
    int backup[BOARD_SIZE + 1];
    int oldExact = exactFinish, oldExtra = extraTurnOnSix;
    int from, to, i;

    printf("File name to load (Enter = board.txt): ");
    readLine(filename, sizeof(filename));
    if (filename[0] == '\0') strcpy(filename, "board.txt");

    file = fopen(filename, "r");
    if (file == NULL)
    {
        printf("File not found.\n");
        return;
    }

    for (i = 0; i <= BOARD_SIZE; i++) backup[i] = jump[i];

    if (fscanf(file, "%d %d", &exactFinish, &extraTurnOnSix) != 2)
    {
        printf("This is not a valid board file.\n");
        exactFinish = oldExact;
        extraTurnOnSix = oldExtra;
        fclose(file);
        return;
    }

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
        printf("That board cannot be finished, so it was not loaded.\n");
        for (i = 0; i <= BOARD_SIZE; i++) jump[i] = backup[i];
        exactFinish = oldExact;
        extraTurnOnSix = oldExtra;
        return;
    }
    printf("Board loaded from %s\n", filename);
}

void saveRecord(const char winner[], int throws)
{
    FILE *file = fopen("records.txt", "a");
    if (file == NULL) return;
    fprintf(file, "%s won in %d throws\n", winner, throws);
    fclose(file);
}

void viewRecords(void)
{
    FILE *file = fopen("records.txt", "r");
    char line[100];

    if (file == NULL)
    {
        printf("\nNo records yet. Play a game first!\n");
        return;
    }
    printf("\n=== WIN RECORDS ===\n");
    while (fgets(line, sizeof(line), file) != NULL)
    {
        printf("%s", line);
    }
    fclose(file);
}

/* ====================================================== */
/*                       THE GAME                         */
/* ====================================================== */

void setupPlayers(void)
{
    int i, type;
    char prompt[60];
    char line[100];

    numPlayers = readInt("How many players (2-4)? ", 2, 4);
    for (i = 0; i < numPlayers; i++)
    {
        sprintf(prompt, "Player %d type (1 = Human, 2 = Computer): ", i + 1);
        type = readInt(prompt, 1, 2);
        if (type == 2)
        {
            isComputer[i] = 1;
            sprintf(names[i], "Computer %d", i + 1);
        }
        else
        {
            isComputer[i] = 0;
            printf("Enter name for player %d: ", i + 1);
            readLine(line, sizeof(line));
            strncpy(names[i], line, 19);
            names[i][19] = '\0';
            if (names[i][0] == '\0') sprintf(names[i], "Player %d", i + 1);
        }
    }
}

void playGame(void)
{
    int current = 0, i, roll, next, gameOver = 0;
    char line[50];

    setupPlayers();
    for (i = 0; i < numPlayers; i++)
    {
        position[i] = 1;
        throwsCount[i] = 0;
    }

    printf("\nPlayers: ");
    for (i = 0; i < numPlayers; i++)
    {
        printf("[%d] = %s   ", i + 1, names[i]);
    }
    printf("\n");
    printBoard(1);

    while (!gameOver)
    {
        printf("\n--- %s's turn (on square %d) ---\n", names[current], position[current]);

        if (!isComputer[current])
        {
            while (1)
            {
                printf("Press Enter to roll (H = hint, B = board, Q = quit game): ");
                if (!readLine(line, sizeof(line))) return;

                if (line[0] == 'h' || line[0] == 'H')
                {
                    printf("Hint: you need at least %d throw(s) to reach 100 from here.\n",
                           minThrowsFrom(position[current]));
                }
                else if (line[0] == 'b' || line[0] == 'B')
                {
                    printBoard(1);
                }
                else if (line[0] == 'q' || line[0] == 'Q')
                {
                    printf("Game ended.\n");
                    return;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            printf("%s rolls the dice...\n", names[current]);
        }

        roll = rollDice();
        throwsCount[current]++;
        printf("%s rolled a %d.\n", names[current], roll);

        next = position[current] + roll;
        if (next > BOARD_SIZE)
        {
            if (exactFinish)
            {
                printf("An exact roll is needed to land on 100. Staying on square %d.\n",
                       position[current]);
                next = position[current];
            }
            else
            {
                next = BOARD_SIZE;
            }
        }
        else if (jump[next] != 0)
        {
            if (jump[next] > next)
            {
                printf("\aLadder! Climb from %d up to %d.\n", next, jump[next]);
            }
            else
            {
                printf("\aSnake! Slide from %d down to %d.\n", next, jump[next]);
            }
            next = jump[next];
        }

        position[current] = next;
        printf("%s is now on square %d.\n", names[current], next);

        if (next == BOARD_SIZE)
        {
            printf("\a\n*** %s WINS in %d throws! ***\n", names[current], throwsCount[current]);
            saveRecord(names[current], throwsCount[current]);
            gameOver = 1;
        }
        else if (roll == 6 && extraTurnOnSix)
        {
            printf("A six! %s gets an extra turn.\n", names[current]);
        }
        else
        {
            current = (current + 1) % numPlayers;
            if (current == 0)
            {
                printBoard(1);   /* show board after every full round */
            }
        }
    }
}

void changeRules(void)
{
    int choice;

    printf("\n--- RULES ---\n");
    printf("1. Exact roll needed to finish : %s\n", exactFinish ? "ON" : "OFF");
    printf("2. Extra turn when rolling a 6 : %s\n", extraTurnOnSix ? "ON" : "OFF");
    printf("0. Back\n");
    choice = readInt("Toggle which rule? ", 0, 2);

    if (choice == 1)
    {
        exactFinish = !exactFinish;
        if (!isPlayable())
        {
            exactFinish = !exactFinish;   /* undo */
            printf("Cannot change: this board could not be finished with that rule.\n");
        }
        else
        {
            printf("Exact finish is now %s.\n", exactFinish ? "ON" : "OFF");
        }
    }
    else if (choice == 2)
    {
        extraTurnOnSix = !extraTurnOnSix;
        printf("Extra turn on six is now %s.\n", extraTurnOnSix ? "ON" : "OFF");
    }
}

/* ====================================================== */
/*                         MAIN                           */
/* ====================================================== */

int main(void)
{
    int choice;

    srand((unsigned)time(NULL));
    loadDefaultBoard();

    printf("=====================================\n");
    printf("   SNAKEFORGE - Snakes and Ladders\n");
    printf("   Design your own board and play!\n");
    printf("=====================================\n");

    do
    {
        printf("\n--- MAIN MENU ---\n");
        printf("1. Play game\n");
        printf("2. Board editor\n");
        printf("3. Show board\n");
        printf("4. Save board to file\n");
        printf("5. Load board from file\n");
        printf("6. Analyze board (solver + difficulty)\n");
        printf("7. Change rules\n");
        printf("8. View win records\n");
        printf("0. Exit\n");
        choice = readInt("Choose: ", 0, 8);

        switch (choice)
        {
            case 1: playGame(); break;
            case 2: boardEditor(); break;
            case 3: printBoard(0); break;
            case 4: saveBoard(); break;
            case 5: loadBoard(); break;
            case 6: analyzeBoard(); break;
            case 7: changeRules(); break;
            case 8: viewRecords(); break;
        }
    } while (choice != 0);

    printf("Thanks for playing SnakeForge!\n");
    return 0;
}
