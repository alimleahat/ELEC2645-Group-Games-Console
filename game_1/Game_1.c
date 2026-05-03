/* ============================================================
 * Game_1.c  -  Two-Player Chess
 * ELEC2645 Embedded Systems Project - Unit 4
 * Platform : STM32 Nucleo-L476RG
 *
 * Controls
 *   Joystick  -> move cursor (8-directional, fires on transition)
 *   BT2       -> select piece / confirm move
 *   BT3       -> cancel selection (1st press) / exit to menu (2nd)
 *
 * Architecture
 *   Input -> Update (FSM: IDLE | SELECTED | GAME_OVER) -> Render
 *   Board: signed int8_t[8][8]  (+ve = White, -ve = Black, 0 = Empty)
 *   Move generation: pseudo-legal (all geometrically valid moves)
 *   Check detection: opponent move scan against king position
 *   Win condition: king captured
 *   Pawn promotion: auto-promote to Queen
 * ============================================================ */

#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ---- Peripherals declared in main.c ---- */
extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t   buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t     joystick_data;
extern ADC_HandleTypeDef hadc1;

/* ---- Second joystick for Player 2 (same pins as Tron Game 3) ---- */
static Joystick_cfg_t joystick2_cfg = {
    .adc            = &hadc1,
    .x_channel      = ADC_CHANNEL_5,
    .y_channel      = ADC_CHANNEL_6,
    .sampling_time  = ADC_SAMPLETIME_47CYCLES_5,
    .center_x       = JOYSTICK_DEFAULT_CENTER_X,
    .center_y       = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone       = JOYSTICK_DEADZONE,
    .setup_done     = 0
};
static Joystick_t joystick2_data;
static uint8_t    joystick2_ready = 0;  /* init once, persist across menu re-entries */

/* ============================================================
 * Display layout  (240 x 320 portrait LCD)
 *   y  0 - 19  : status bar  (turn / check / win message)
 *   y 20 - 219 : chess board (8 * 25 = 200 px square)
 *   y 220 - 239: instruction bar
 * ============================================================ */
#define BOARD_X    20     /* left edge of board (pixels)  */
#define BOARD_Y    20     /* top  edge of board (pixels)  */
#define SQ_SIZE    25     /* pixels per square            */

/* ---- 4-bit palette colour indices (PALETTE_DEFAULT) ----
 *  0 = BLACK     1 = WHITE      6 = YELLOW    12 = BROWN
 *  Cream/brown wood-board look using available palette. */
#define COL_BG       0    /* background fill              */
#define COL_LIGHT    1    /* light square (cream/white)   */
#define COL_DARK    12    /* dark  square (brown)         */
#define COL_CURSOR   6    /* cursor highlight (yellow)    */
#define COL_SEL     14    /* selected-piece highlight     */
#define COL_VALID    3    /* valid-move indicator (green) */
#define COL_WHITE    1    /* bright text                  */
#define COL_CHECK    2    /* check warning colour (red)   */
#define COL_PIECE_W  1    /* white piece body             */
#define COL_PIECE_B  0    /* black piece body             */

/* ============================================================
 * Piece encoding
 *   +PAWN .. +KING  = White pieces
 *   -PAWN .. -KING  = Black pieces
 *            EMPTY  = empty square
 * ============================================================ */
#define EMPTY    0
#define PAWN     1
#define KNIGHT   2
#define BISHOP   3
#define ROOK     4
#define QUEEN    5
#define KING     6

/* ============================================================
 * File-scope move tables
 *   Kept at file scope to avoid VLAs inside switch blocks and
 *   to keep stack usage minimal on the Nucleo.
 * ============================================================ */
static const int8_t KNIGHT_MOVES[8][2] = {
    {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}
};
static const int8_t KING_DIRS[8][2] = {
    {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
};
static const int8_t DIAG_DIRS[4][2] = { {-1,-1},{-1,1},{1,-1},{1,1} };
static const int8_t AXIS_DIRS[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };

/* ============================================================
 * Game state  (all static - no heap allocation)
 * ============================================================ */
static int8_t  board[8][8];         /* piece values              */
static uint8_t cursor_row;          /* joystick cursor row       */
static uint8_t cursor_col;          /* joystick cursor column    */
static int8_t  sel_row;             /* selected piece row (-1=none) */
static int8_t  sel_col;             /* selected piece col (-1=none) */
static uint8_t current_player;      /* 0 = White, 1 = Black      */
static uint8_t game_over;           /* 1 when a king is captured */
static int8_t  winner;              /* 1=white, -1=black         */
static uint8_t white_in_check;
static uint8_t black_in_check;
static uint8_t valid_moves[8][8];   /* pseudo-legal destinations */
static uint8_t temp_moves[8][8];    /* scratch buffer for check detection */
static Direction last_joy1_dir;     /* edge-trigger: white's joystick   */
static Direction last_joy2_dir;     /* edge-trigger: black's joystick   */

#define CHESS_FRAME_MS  50          /* ~20 FPS  */

/* ============================================================
 * Utility helpers
 * ============================================================ */
static int8_t  piece_type (int8_t p) { return (int8_t)(p < 0 ? -p : p); }
static int8_t  piece_color(int8_t p) { return p > 0 ? 1 : p < 0 ? -1 : 0; }
static uint8_t on_board   (int r, int c) {
    return (r >= 0 && r < 8 && c >= 0 && c < 8) ? 1u : 0u;
}

/* ============================================================
 * Board initialisation  (standard chess starting position)
 * ============================================================ */
static void chess_init_board(void) {
    static const int8_t BACK_RANK[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };
    int r, c;

    for (c = 0; c < 8; c++) board[0][c] = (int8_t)(-(BACK_RANK[c])); /* Black back rank */
    for (c = 0; c < 8; c++) board[1][c] = -PAWN;                      /* Black pawns     */
    for (r = 2; r < 6; r++)
        for (c = 0; c < 8; c++) board[r][c] = EMPTY;                  /* Empty rows      */
    for (c = 0; c < 8; c++) board[6][c] = PAWN;                       /* White pawns     */
    for (c = 0; c < 8; c++) board[7][c] = BACK_RANK[c];               /* White back rank */
}

/* ============================================================
 * Move generation  (populates valid_moves[][])
 *
 *   slide() iterates in one direction until it hits the edge,
 *   a friendly piece (stop without marking), or an enemy piece
 *   (mark as capturable, then stop).
 * ============================================================ */
static void slide(int8_t r, int8_t c, int8_t dr, int8_t dc, int8_t color) {
    int step;
    for (step = 1; step < 8; step++) {
        int8_t nr = (int8_t)(r + dr * step);
        int8_t nc = (int8_t)(c + dc * step);
        if (!on_board(nr, nc)) break;
        if (piece_color(board[nr][nc]) == color) break;  /* own piece - blocked */
        valid_moves[nr][nc] = 1;
        if (board[nr][nc] != EMPTY) break;               /* enemy piece - capture & stop */
    }
}

static void generate_moves(int8_t row, int8_t col) {
    int i;
    int8_t piece = board[row][col];
    int8_t color = piece_color(piece);
    int8_t type  = piece_type(piece);

    memset(valid_moves, 0, sizeof(valid_moves));
    if (piece == EMPTY) return;

    switch (type) {

        case PAWN: {
            int8_t dir     = (color == 1) ? -1 : 1;   /* white moves up (row 7->0) */
            int8_t start_r = (color == 1) ?  6 : 1;
            int8_t nr      = (int8_t)(row + dir);

            /* One step forward into empty square */
            if (on_board(nr, col) && board[nr][col] == EMPTY) {
                valid_moves[nr][col] = 1;
                /* Two steps from starting rank */
                if (row == start_r && board[row + 2*dir][col] == EMPTY)
                    valid_moves[row + 2*dir][col] = 1;
            }
            /* Diagonal captures */
            if (on_board(nr, col-1) && piece_color(board[nr][col-1]) == -color)
                valid_moves[nr][col-1] = 1;
            if (on_board(nr, col+1) && piece_color(board[nr][col+1]) == -color)
                valid_moves[nr][col+1] = 1;
            break;
        }

        case KNIGHT:
            for (i = 0; i < 8; i++) {
                int8_t nr = (int8_t)(row + KNIGHT_MOVES[i][0]);
                int8_t nc = (int8_t)(col + KNIGHT_MOVES[i][1]);
                if (on_board(nr, nc) && piece_color(board[nr][nc]) != color)
                    valid_moves[nr][nc] = 1;
            }
            break;

        case BISHOP:
            for (i = 0; i < 4; i++)
                slide(row, col, DIAG_DIRS[i][0], DIAG_DIRS[i][1], color);
            break;

        case ROOK:
            for (i = 0; i < 4; i++)
                slide(row, col, AXIS_DIRS[i][0], AXIS_DIRS[i][1], color);
            break;

        case QUEEN:
            for (i = 0; i < 4; i++) slide(row, col, DIAG_DIRS[i][0], DIAG_DIRS[i][1], color);
            for (i = 0; i < 4; i++) slide(row, col, AXIS_DIRS[i][0], AXIS_DIRS[i][1], color);
            break;

        case KING:
            for (i = 0; i < 8; i++) {
                int8_t nr = (int8_t)(row + KING_DIRS[i][0]);
                int8_t nc = (int8_t)(col + KING_DIRS[i][1]);
                if (on_board(nr, nc) && piece_color(board[nr][nc]) != color)
                    valid_moves[nr][nc] = 1;
            }
            break;

        default: break;
    }
}

/* ============================================================
 * Check detection
 *
 *   Saves and restores valid_moves[] around the internal call
 *   to generate_moves() so the caller's move list is preserved.
 * ============================================================ */
static uint8_t is_in_check(int8_t color) {
    int r, c;
    int8_t king_r = -1, king_c = -1;
    uint8_t in_check = 0;

    /* Locate the king */
    for (r = 0; r < 8 && king_r < 0; r++)
        for (c = 0; c < 8 && king_r < 0; c++)
            if (board[r][c] == (int8_t)(color * KING)) { king_r = (int8_t)r; king_c = (int8_t)c; }

    if (king_r < 0) return 0;  /* king already captured - handled by check_winner() */

    memcpy(temp_moves, valid_moves, sizeof(valid_moves));  /* save  */

    for (r = 0; r < 8 && !in_check; r++) {
        for (c = 0; c < 8 && !in_check; c++) {
            if (piece_color(board[r][c]) == -color) {
                generate_moves((int8_t)r, (int8_t)c);
                if (valid_moves[king_r][king_c]) in_check = 1;
            }
        }
    }

    memcpy(valid_moves, temp_moves, sizeof(valid_moves));  /* restore */
    return in_check;
}

/* ============================================================
 * Win detection  -  called after every move
 * ============================================================ */
static int8_t check_winner(void) {
    int r, c;
    uint8_t wk = 0, bk = 0;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) {
            if (board[r][c] ==  KING) wk = 1;
            if (board[r][c] == -KING) bk = 1;
        }
    if (!bk) return  1;   /* white wins */
    if (!wk) return -1;   /* black wins */
    return 0;
}

/* ============================================================
 * Pawn promotion  -  auto-promote to Queen
 * ============================================================ */
static void check_promotion(void) {
    int c;
    for (c = 0; c < 8; c++) {
        if (board[0][c] ==  PAWN) board[0][c] =  QUEEN;  /* white pawn reaches row 0 */
        if (board[7][c] == -PAWN) board[7][c] = -QUEEN;  /* black pawn reaches row 7 */
    }
}

/* ============================================================
 * Execute the move stored in (sel_row, sel_col) -> (to_row, to_col)
 * ============================================================ */
static void execute_move(int8_t to_row, int8_t to_col) {
    board[to_row][to_col] = board[sel_row][sel_col];
    board[sel_row][sel_col] = EMPTY;
    check_promotion();
}

/* ============================================================
 * Rendering
 * ============================================================ */

/* Draw a single piece icon centred in a square at (px, py).
 * Each piece is a filled circle (the body) plus a small detail
 * identifying it. White pieces are filled white with a dark
 * outline ring; black pieces are filled black with a light ring. */
static void draw_piece(int16_t px, int16_t py, int8_t piece) {
    int16_t cx = (int16_t)(px + SQ_SIZE / 2);
    int16_t cy = (int16_t)(py + SQ_SIZE / 2);

    uint8_t is_white   = (piece > 0);
    uint8_t body_col   = is_white ? COL_PIECE_W : COL_PIECE_B;
    uint8_t detail_col = is_white ? COL_PIECE_B : COL_PIECE_W;
    int8_t  type       = (int8_t)(is_white ? piece : -piece);

    /* Body radii: pawn smallest, queen/king largest */
    int16_t body_r;
    switch (type) {
        case PAWN:                  body_r = 5;  break;
        case KNIGHT: case BISHOP:
        case ROOK:                  body_r = 7;  break;
        case QUEEN:  case KING:     body_r = 8;  break;
        default:                    body_r = 7;  break;
    }

    /* Filled body + contrasting outline ring (1 px) */
    LCD_Draw_Circle(cx, cy, (uint16_t)body_r,       body_col,   1);
    LCD_Draw_Circle(cx, cy, (uint16_t)(body_r + 1), detail_col, 0);

    /* Identifying detail */
    switch (type) {
        case PAWN:
            /* Plain small circle — body only */
            break;

        case ROOK: {
            /* Four small dots at N/S/E/W edges (turrets) */
            int16_t d = (int16_t)(body_r - 1);
            LCD_Draw_Circle((int16_t)(cx),     (int16_t)(cy - d), 1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx),     (int16_t)(cy + d), 1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx - d), (int16_t)(cy),     1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx + d), (int16_t)(cy),     1, detail_col, 1);
            break;
        }

        case BISHOP:
            /* Smaller filled circle in the middle (mitre) */
            LCD_Draw_Circle(cx, cy, 3, detail_col, 1);
            break;

        case KNIGHT:
            /* Two perpendicular bars forming a horse-profile L:
             * vertical bar = neck, horizontal bar at top = muzzle */
            LCD_Draw_Rect((int16_t)(cx - 2), (int16_t)(cy - 4),
                          3, 7, detail_col, 1);   /* neck   */
            LCD_Draw_Rect((int16_t)(cx - 2), (int16_t)(cy - 4),
                          5, 3, detail_col, 1);   /* muzzle */
            break;

        case QUEEN:
            /* Small filled circle on top of body (orb) */
            LCD_Draw_Circle(cx, (int16_t)(cy - body_r - 1), 2, body_col, 1);
            LCD_Draw_Circle(cx, (int16_t)(cy - body_r - 1), 2, detail_col, 0);
            break;

        case KING:
            /* Bold + cross inside the body (7-px arms, 3-px thick) */
            LCD_Draw_Rect((int16_t)(cx - 3), (int16_t)(cy - 1),
                          7, 3, detail_col, 1);   /* horizontal arm */
            LCD_Draw_Rect((int16_t)(cx - 1), (int16_t)(cy - 3),
                          3, 7, detail_col, 1);   /* vertical arm   */
            break;

        default:
            break;
    }
}

static void render_board(void) {
    int r, c;

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            int16_t px = (int16_t)(BOARD_X + c * SQ_SIZE);
            int16_t py = (int16_t)(BOARD_Y + r * SQ_SIZE);
            int8_t  piece = board[r][c];

            /* --- Square base colour (always the cream/brown pattern) --- */
            uint8_t sq_col = ((r + c) % 2 == 0) ? COL_LIGHT : COL_DARK;
            LCD_Draw_Rect(px, py, SQ_SIZE, SQ_SIZE, sq_col, 1);

            /* --- Piece icon --- */
            if (piece != EMPTY) {
                draw_piece(px, py, piece);
            }

            /* --- Valid-move indicator (small dot for empty,
             *     ring around enemy piece to show capture) --- */
            if (sel_row >= 0 && valid_moves[r][c]) {
                int16_t cx = (int16_t)(px + SQ_SIZE / 2);
                int16_t cy = (int16_t)(py + SQ_SIZE / 2);
                if (piece == EMPTY) {
                    LCD_Draw_Circle(cx, cy, 3, COL_VALID, 1);
                } else {
                    LCD_Draw_Circle(cx, cy, 11, COL_VALID, 0);
                    LCD_Draw_Circle(cx, cy, 12, COL_VALID, 0);
                }
            }

            /* --- Selection highlight: 2-px coloured border --- */
            if (sel_row >= 0 && r == (int)sel_row && c == (int)sel_col) {
                LCD_Draw_Rect(px,                  py,                  SQ_SIZE, 1,       COL_SEL, 1);
                LCD_Draw_Rect(px,                  (int16_t)(py+SQ_SIZE-1), SQ_SIZE, 1,   COL_SEL, 1);
                LCD_Draw_Rect(px,                  py,                  1,       SQ_SIZE, COL_SEL, 1);
                LCD_Draw_Rect((int16_t)(px+SQ_SIZE-1), py,              1,       SQ_SIZE, COL_SEL, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+1),   (int16_t)(SQ_SIZE-2), 1, COL_SEL, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+SQ_SIZE-2), (int16_t)(SQ_SIZE-2), 1, COL_SEL, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+1),   1, (int16_t)(SQ_SIZE-2), COL_SEL, 1);
                LCD_Draw_Rect((int16_t)(px+SQ_SIZE-2), (int16_t)(py+1), 1, (int16_t)(SQ_SIZE-2), COL_SEL, 1);
            }

            /* --- Cursor: 2-px border in cursor colour (drawn last, on top) --- */
            if ((uint8_t)r == cursor_row && (uint8_t)c == cursor_col) {
                LCD_Draw_Rect(px,                  py,                  SQ_SIZE, 1,       COL_CURSOR, 1);
                LCD_Draw_Rect(px,                  (int16_t)(py+SQ_SIZE-1), SQ_SIZE, 1,   COL_CURSOR, 1);
                LCD_Draw_Rect(px,                  py,                  1,       SQ_SIZE, COL_CURSOR, 1);
                LCD_Draw_Rect((int16_t)(px+SQ_SIZE-1), py,              1,       SQ_SIZE, COL_CURSOR, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+1),   (int16_t)(SQ_SIZE-2), 1, COL_CURSOR, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+SQ_SIZE-2), (int16_t)(SQ_SIZE-2), 1, COL_CURSOR, 1);
                LCD_Draw_Rect((int16_t)(px+1),       (int16_t)(py+1),   1, (int16_t)(SQ_SIZE-2), COL_CURSOR, 1);
                LCD_Draw_Rect((int16_t)(px+SQ_SIZE-2), (int16_t)(py+1), 1, (int16_t)(SQ_SIZE-2), COL_CURSOR, 1);
            }
        }
    }
}

static void render_status(void) {
    if (!game_over) {
        /* Top bar: whose turn + which joystick they use */
        LCD_printString(
            (current_player == 0) ? "WHITE (JOY1)" : "BLACK (JOY2)",
            20, 5, COL_WHITE, 1);

        /* Check alert */
        if ((current_player == 0 && white_in_check) ||
            (current_player == 1 && black_in_check)) {
            LCD_printString("CHECK!", 155, 5, COL_CHECK, 1);
        }

        /* Bottom bar: controls (BT2 = act, joystick press = back) */
        LCD_printString("BT2: select/move", 20, 224, COL_WHITE, 1);
        LCD_printString("Joy: cancel/menu", 20, 232, COL_WHITE, 1);

    } else {
        /* Game-over screen */
        LCD_printString(
            (winner == 1) ? " WHITE WINS!" : " BLACK WINS!",
            15, 5, COL_WHITE, 2);
        LCD_printString("Press joystick for menu", 14, 230, COL_WHITE, 1);
    }
}

/* ============================================================
 * Game1_Run  -  entry point called by the main state machine
 * ============================================================ */
MenuState Game1_Run(void) {

    /* ---------- Initialise second joystick (once only) ---------- */
    if (!joystick2_ready) {
        Joystick_Init(&joystick2_cfg);
        Joystick_Calibrate(&joystick2_cfg);
        joystick2_ready = 1;
    }

    /* ---------- Initialise game state ---------- */
    chess_init_board();
    cursor_row     = 6;     /* start cursor on white's side */
    cursor_col     = 4;
    sel_row        = -1;
    sel_col        = -1;
    current_player = 0;     /* white moves first */
    game_over      = 0;
    winner         = 0;
    white_in_check = 0;
    black_in_check = 0;
    last_joy1_dir  = CENTRE;
    last_joy2_dir  = CENTRE;
    memset(valid_moves, 0, sizeof(valid_moves));

    /* Startup fanfare */
    buzzer_tone(&buzzer_cfg, 523, 80);  HAL_Delay(90);   /* C5 */
    buzzer_tone(&buzzer_cfg, 659, 80);  HAL_Delay(90);   /* E5 */
    buzzer_tone(&buzzer_cfg, 784, 120); HAL_Delay(130);  /* G5 */
    buzzer_off(&buzzer_cfg);

    /* ---------- Main loop ---------- */
    while (1) {
        uint32_t frame_start = HAL_GetTick();

        /* ===== INPUT ===== */
        Input_Read();

        /* Read both joysticks — P1 (White) on joy1, P2 (Black) on joy2 */
        Joystick_Read(&joystick_cfg,  &joystick_data);
        Joystick_Read(&joystick2_cfg, &joystick2_data);

        /* Active player picks their joystick */
        Direction dir = (current_player == 0)
                        ? Joystick_GetInput(&joystick_data).direction
                        : Joystick_GetInput(&joystick2_data).direction;

        Direction *last_dir = (current_player == 0) ? &last_joy1_dir : &last_joy2_dir;

        /* BT3: cancel selection first press, exit to menu second press */
        if (current_input.btn3_pressed) {
            if (sel_row >= 0 && !game_over) {
                sel_row = -1;
                sel_col = -1;
                memset(valid_moves, 0, sizeof(valid_moves));
            } else {
                return MENU_STATE_HOME;
            }
        }

        if (!game_over) {

            /* ===== CURSOR MOVEMENT (edge-triggered on direction change) ===== */
            if (dir != *last_dir && dir != CENTRE) {
                switch (dir) {
                    case N:  if (cursor_row > 0) cursor_row--; break;
                    case S:  if (cursor_row < 7) cursor_row++; break;
                    case W:  if (cursor_col > 0) cursor_col--; break;
                    case E:  if (cursor_col < 7) cursor_col++; break;
                    case NW: if (cursor_row > 0 && cursor_col > 0) { cursor_row--; cursor_col--; } break;
                    case NE: if (cursor_row > 0 && cursor_col < 7) { cursor_row--; cursor_col++; } break;
                    case SW: if (cursor_row < 7 && cursor_col > 0) { cursor_row++; cursor_col--; } break;
                    case SE: if (cursor_row < 7 && cursor_col < 7) { cursor_row++; cursor_col++; } break;
                    default: break;
                }
            }
            *last_dir = dir;

            /* ===== BT2: SELECT or MOVE ===== */
            if (current_input.btn2_pressed) {

                if (sel_row < 0) {
                    /* --- Nothing selected: try to select current player's piece --- */
                    int8_t p  = board[cursor_row][cursor_col];
                    int8_t pc = piece_color(p);
                    if ((current_player == 0 && pc ==  1) ||
                        (current_player == 1 && pc == -1)) {
                        sel_row = (int8_t)cursor_row;
                        sel_col = (int8_t)cursor_col;
                        generate_moves(sel_row, sel_col);
                        buzzer_tone(&buzzer_cfg, 600, 30);
                        HAL_Delay(35);
                        buzzer_off(&buzzer_cfg);
                    }

                } else {
                    /* --- Piece already selected --- */

                    if ((int8_t)cursor_row == sel_row && (int8_t)cursor_col == sel_col) {
                        /* Same square clicked again: deselect */
                        sel_row = -1;
                        sel_col = -1;
                        memset(valid_moves, 0, sizeof(valid_moves));

                    } else if (valid_moves[cursor_row][cursor_col]) {
                        /* Valid destination: execute the move */
                        execute_move((int8_t)cursor_row, (int8_t)cursor_col);

                        winner = check_winner();
                        if (winner != 0) {
                            game_over = 1;
                            /* Victory fanfare */
                            buzzer_tone(&buzzer_cfg, 784,  150); HAL_Delay(160);
                            buzzer_tone(&buzzer_cfg, 988,  150); HAL_Delay(160);
                            buzzer_tone(&buzzer_cfg, 1175, 350); HAL_Delay(360);
                            buzzer_off(&buzzer_cfg);
                        } else {
                            /* Switch turn and update check status */
                            current_player ^= 1u;
                            white_in_check = is_in_check( 1);
                            black_in_check = is_in_check(-1);
                            /* Move cursor to new player's side and clear edge state */
                            cursor_row    = (current_player == 0) ? 7u : 0u;
                            cursor_col    = 4u;
                            last_joy1_dir = CENTRE;
                            last_joy2_dir = CENTRE;
                            /* Move sound */
                            buzzer_tone(&buzzer_cfg, 440, 40);
                            HAL_Delay(45);
                            buzzer_off(&buzzer_cfg);
                            /* Extra warning beep if new player is in check */
                            if ((current_player == 0 && white_in_check) ||
                                (current_player == 1 && black_in_check)) {
                                HAL_Delay(80);
                                buzzer_tone(&buzzer_cfg, 220, 250);
                                HAL_Delay(260);
                                buzzer_off(&buzzer_cfg);
                            }
                        }

                        sel_row = -1;
                        sel_col = -1;
                        memset(valid_moves, 0, sizeof(valid_moves));

                    } else {
                        /* Invalid destination: try to re-select a different own piece */
                        int8_t p  = board[cursor_row][cursor_col];
                        int8_t pc = piece_color(p);
                        if ((current_player == 0 && pc ==  1) ||
                            (current_player == 1 && pc == -1)) {
                            sel_row = (int8_t)cursor_row;
                            sel_col = (int8_t)cursor_col;
                            generate_moves(sel_row, sel_col);
                        }
                    }
                }
            } /* end BT2 */
        } /* end !game_over */

        /* ===== RENDER ===== */
        LCD_Fill_Buffer(COL_BG);
        render_board();
        render_status();
        LCD_Refresh(&cfg0);

        /* Frame-rate cap */
        uint32_t elapsed = HAL_GetTick() - frame_start;
        if (elapsed < CHESS_FRAME_MS) HAL_Delay(CHESS_FRAME_MS - elapsed);

    } /* end while(1) */
}
