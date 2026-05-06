/*
 * Game_1.c - Two-Player Chess
 *
 * Controls:
 *   Joystick = move cursor / select / confirm move
 *   BT2      = cancel selection / back to menu
 *
 * Board encoding: int8_t[8][8], +ve = White, -ve = Black, 0 = Empty.
 */

#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t   buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t     joystick_data;
extern ADC_HandleTypeDef hadc1;

/* Second joystick for Player 2 (shared pins with Tron) */
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
static uint8_t    joystick2_ready = 0;   /* init once across menu re-entries */

/* Board layout (240x320 portrait) */
#define BOARD_X    20
#define BOARD_Y    20
#define SQ_SIZE    25

/* Palette indices (PALETTE_DEFAULT) */
#define COL_BG       0
#define COL_LIGHT    1    /* light square (cream) */
#define COL_DARK    12    /* dark square (brown)  */
#define COL_CURSOR   6    /* yellow */
#define COL_SEL     14
#define COL_VALID    3    /* green */
#define COL_WHITE    1
#define COL_CHECK    2    /* red */
#define COL_PIECE_W  1
#define COL_PIECE_B  0

/* Piece codes */
#define EMPTY    0
#define PAWN     1
#define KNIGHT   2
#define BISHOP   3
#define ROOK     4
#define QUEEN    5
#define KING     6

/* Move-direction tables (file scope to keep stack usage low) */
static const int8_t KNIGHT_MOVES[8][2] = {
    {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}
};
static const int8_t KING_DIRS[8][2] = {
    {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1}
};
static const int8_t DIAG_DIRS[4][2] = { {-1,-1},{-1,1},{1,-1},{1,1} };
static const int8_t AXIS_DIRS[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };

/* Game state (all static, no heap) */
static int8_t  board[8][8];
static uint8_t cursor_row, cursor_col;
static int8_t  sel_row, sel_col;          /* -1 = nothing selected */
static uint8_t current_player;            /* 0 = White, 1 = Black */
static uint8_t game_over;
static int8_t  winner;                    /* 1 = white, -1 = black */
static uint8_t white_in_check, black_in_check;
static uint8_t valid_moves[8][8];
static uint8_t temp_moves[8][8];          /* scratch buffer used by is_in_check */
static Direction last_joy1_dir, last_joy2_dir;

#define CHESS_FRAME_MS  50    /* ~20 FPS */

static int8_t  piece_type (int8_t p) { return (int8_t)(p < 0 ? -p : p); }
static int8_t  piece_color(int8_t p) { return p > 0 ? 1 : p < 0 ? -1 : 0; }
static uint8_t on_board   (int r, int c) {
    return (r >= 0 && r < 8 && c >= 0 && c < 8) ? 1u : 0u;
}

/* Set up the standard starting position. */
static void chess_init_board(void) {
    static const int8_t BACK_RANK[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };
    int r, c;

    for (c = 0; c < 8; c++) board[0][c] = (int8_t)(-(BACK_RANK[c]));
    for (c = 0; c < 8; c++) board[1][c] = -PAWN;
    for (r = 2; r < 6; r++)
        for (c = 0; c < 8; c++) board[r][c] = EMPTY;
    for (c = 0; c < 8; c++) board[6][c] = PAWN;
    for (c = 0; c < 8; c++) board[7][c] = BACK_RANK[c];
}

/* Walk one direction marking valid squares; stop at edge,
 * own piece, or after marking an enemy piece (capture). */
static void slide(int8_t r, int8_t c, int8_t dr, int8_t dc, int8_t color) {
    int step;
    for (step = 1; step < 8; step++) {
        int8_t nr = (int8_t)(r + dr * step);
        int8_t nc = (int8_t)(c + dc * step);
        if (!on_board(nr, nc)) break;
        if (piece_color(board[nr][nc]) == color) break;
        valid_moves[nr][nc] = 1;
        if (board[nr][nc] != EMPTY) break;
    }
}

/* Fill valid_moves[][] with pseudo-legal destinations for the
 * piece at (row, col). */
static void generate_moves(int8_t row, int8_t col) {
    int i;
    int8_t piece = board[row][col];
    int8_t color = piece_color(piece);
    int8_t type  = piece_type(piece);

    memset(valid_moves, 0, sizeof(valid_moves));
    if (piece == EMPTY) return;

    switch (type) {

        case PAWN: {
            int8_t dir     = (color == 1) ? -1 : 1;   /* white moves up the array */
            int8_t start_r = (color == 1) ?  6 : 1;
            int8_t nr      = (int8_t)(row + dir);

            if (on_board(nr, col) && board[nr][col] == EMPTY) {
                valid_moves[nr][col] = 1;
                if (row == start_r && board[row + 2*dir][col] == EMPTY)
                    valid_moves[row + 2*dir][col] = 1;
            }
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

/* Return 1 if the given side's king is attacked.
 * Saves and restores valid_moves so the caller's list survives. */
static uint8_t is_in_check(int8_t color) {
    int r, c;
    int8_t king_r = -1, king_c = -1;
    uint8_t in_check = 0;

    for (r = 0; r < 8 && king_r < 0; r++)
        for (c = 0; c < 8 && king_r < 0; c++)
            if (board[r][c] == (int8_t)(color * KING)) { king_r = (int8_t)r; king_c = (int8_t)c; }

    if (king_r < 0) return 0;   /* king already captured */

    memcpy(temp_moves, valid_moves, sizeof(valid_moves));

    for (r = 0; r < 8 && !in_check; r++) {
        for (c = 0; c < 8 && !in_check; c++) {
            if (piece_color(board[r][c]) == -color) {
                generate_moves((int8_t)r, (int8_t)c);
                if (valid_moves[king_r][king_c]) in_check = 1;
            }
        }
    }

    memcpy(valid_moves, temp_moves, sizeof(valid_moves));
    return in_check;
}

/* 1 = white wins, -1 = black wins, 0 = game still going. */
static int8_t check_winner(void) {
    int r, c;
    uint8_t wk = 0, bk = 0;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) {
            if (board[r][c] ==  KING) wk = 1;
            if (board[r][c] == -KING) bk = 1;
        }
    if (!bk) return  1;
    if (!wk) return -1;
    return 0;
}

/* Auto-promote any pawn that reached the back rank to a Queen. */
static void check_promotion(void) {
    int c;
    for (c = 0; c < 8; c++) {
        if (board[0][c] ==  PAWN) board[0][c] =  QUEEN;
        if (board[7][c] == -PAWN) board[7][c] = -QUEEN;
    }
}

/* Move the selected piece to (to_row, to_col). */
static void execute_move(int8_t to_row, int8_t to_col) {
    board[to_row][to_col] = board[sel_row][sel_col];
    board[sel_row][sel_col] = EMPTY;
    check_promotion();
}

/* Draw a piece icon centred in its square.
 * Each piece is a filled circle plus a small detail to identify it. */
static void draw_piece(int16_t px, int16_t py, int8_t piece) {
    int16_t cx = (int16_t)(px + SQ_SIZE / 2);
    int16_t cy = (int16_t)(py + SQ_SIZE / 2);

    uint8_t is_white   = (piece > 0);
    uint8_t body_col   = is_white ? COL_PIECE_W : COL_PIECE_B;
    uint8_t detail_col = is_white ? COL_PIECE_B : COL_PIECE_W;
    int8_t  type       = (int8_t)(is_white ? piece : -piece);

    int16_t body_r;
    switch (type) {
        case PAWN:                  body_r = 5;  break;
        case KNIGHT: case BISHOP:
        case ROOK:                  body_r = 7;  break;
        case QUEEN:  case KING:     body_r = 8;  break;
        default:                    body_r = 7;  break;
    }

    LCD_Draw_Circle(cx, cy, (uint16_t)body_r,       body_col,   1);
    LCD_Draw_Circle(cx, cy, (uint16_t)(body_r + 1), detail_col, 0);

    switch (type) {
        case PAWN:
            break;

        case ROOK: {
            /* 4 dots = turrets */
            int16_t d = (int16_t)(body_r - 1);
            LCD_Draw_Circle((int16_t)(cx),     (int16_t)(cy - d), 1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx),     (int16_t)(cy + d), 1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx - d), (int16_t)(cy),     1, detail_col, 1);
            LCD_Draw_Circle((int16_t)(cx + d), (int16_t)(cy),     1, detail_col, 1);
            break;
        }

        case BISHOP:
            LCD_Draw_Circle(cx, cy, 3, detail_col, 1);
            break;

        case KNIGHT:
            /* L-shape: vertical neck + horizontal muzzle */
            LCD_Draw_Rect((int16_t)(cx - 2), (int16_t)(cy - 4), 3, 7, detail_col, 1);
            LCD_Draw_Rect((int16_t)(cx - 2), (int16_t)(cy - 4), 5, 3, detail_col, 1);
            break;

        case QUEEN:
            /* Orb on top */
            LCD_Draw_Circle(cx, (int16_t)(cy - body_r - 1), 2, body_col, 1);
            LCD_Draw_Circle(cx, (int16_t)(cy - body_r - 1), 2, detail_col, 0);
            break;

        case KING:
            /* Cross inside the body */
            LCD_Draw_Rect((int16_t)(cx - 3), (int16_t)(cy - 1), 7, 3, detail_col, 1);
            LCD_Draw_Rect((int16_t)(cx - 1), (int16_t)(cy - 3), 3, 7, detail_col, 1);
            break;

        default:
            break;
    }
}

/* Draw the 8x8 board, all pieces, valid-move markers,
 * and the cursor/selection highlights on top. */
static void render_board(void) {
    int r, c;

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            int16_t px = (int16_t)(BOARD_X + c * SQ_SIZE);
            int16_t py = (int16_t)(BOARD_Y + r * SQ_SIZE);
            int8_t  piece = board[r][c];

            uint8_t sq_col = ((r + c) % 2 == 0) ? COL_LIGHT : COL_DARK;
            LCD_Draw_Rect(px, py, SQ_SIZE, SQ_SIZE, sq_col, 1);

            if (piece != EMPTY) {
                draw_piece(px, py, piece);
            }

            /* Valid-move marker: dot for empty, ring around an enemy piece */
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

            /* Selection highlight (2-px border) */
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

            /* Cursor (drawn last so it sits on top) */
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

/* Top status bar (whose turn / check / win) and bottom controls. */
static void render_status(void) {
    if (!game_over) {
        LCD_printString(
            (current_player == 0) ? "WHITE (JOY1)" : "BLACK (JOY2)",
            20, 5, COL_WHITE, 1);

        if ((current_player == 0 && white_in_check) ||
            (current_player == 1 && black_in_check)) {
            LCD_printString("CHECK!", 155, 5, COL_CHECK, 1);
        }

        LCD_printString("Joy: select/move", 20, 224, COL_WHITE, 1);
        LCD_printString("BT2: cancel/menu", 20, 232, COL_WHITE, 1);

    } else {
        LCD_printString(
            (winner == 1) ? " WHITE WINS!" : " BLACK WINS!",
            15, 5, COL_WHITE, 2);
        LCD_printString("Press BT2 for menu", 28, 230, COL_WHITE, 1);
    }
}

/* Game entry point. Owns its own loop; returns to menu on BT2. */
MenuState Game1_Run(void) {

    /* Init second joystick on first entry */
    if (!joystick2_ready) {
        Joystick_Init(&joystick2_cfg);
        Joystick_Calibrate(&joystick2_cfg);
        joystick2_ready = 1;
    }

    /* Reset game state */
    chess_init_board();
    cursor_row     = 6;     /* start on white's side */
    cursor_col     = 4;
    sel_row        = -1;
    sel_col        = -1;
    current_player = 0;
    game_over      = 0;
    winner         = 0;
    white_in_check = 0;
    black_in_check = 0;
    last_joy1_dir  = CENTRE;
    last_joy2_dir  = CENTRE;
    memset(valid_moves, 0, sizeof(valid_moves));

    /* Startup fanfare */
    buzzer_tone(&buzzer_cfg, 523, 80);  HAL_Delay(90);
    buzzer_tone(&buzzer_cfg, 659, 80);  HAL_Delay(90);
    buzzer_tone(&buzzer_cfg, 784, 120); HAL_Delay(130);
    buzzer_off(&buzzer_cfg);

    while (1) {
        uint32_t frame_start = HAL_GetTick();

        /* --- Input --- */
        Input_Read();

        Joystick_Read(&joystick_cfg,  &joystick_data);
        Joystick_Read(&joystick2_cfg, &joystick2_data);

        /* Active player picks the matching joystick */
        Direction dir = (current_player == 0)
                        ? Joystick_GetInput(&joystick_data).direction
                        : Joystick_GetInput(&joystick2_data).direction;

        Direction *last_dir = (current_player == 0) ? &last_joy1_dir : &last_joy2_dir;

        /* BT2: cancel selection first, then exit to menu */
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

            /* Cursor moves on direction change (no auto-repeat) */
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

            /* Joystick press = select / confirm move */
            if (current_input.btn2_pressed) {

                if (sel_row < 0) {
                    /* Nothing selected yet: pick up own piece if any */
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

                    if ((int8_t)cursor_row == sel_row && (int8_t)cursor_col == sel_col) {
                        /* Pressed on selected piece again -> deselect */
                        sel_row = -1;
                        sel_col = -1;
                        memset(valid_moves, 0, sizeof(valid_moves));

                    } else if (valid_moves[cursor_row][cursor_col]) {
                        /* Confirm move */
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
                            current_player ^= 1u;
                            white_in_check = is_in_check( 1);
                            black_in_check = is_in_check(-1);
                            /* Move cursor to the new player's side */
                            cursor_row    = (current_player == 0) ? 7u : 0u;
                            cursor_col    = 4u;
                            last_joy1_dir = CENTRE;
                            last_joy2_dir = CENTRE;
                            /* Move sound */
                            buzzer_tone(&buzzer_cfg, 440, 40);
                            HAL_Delay(45);
                            buzzer_off(&buzzer_cfg);
                            /* Low warning beep if new player is in check */
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
                        /* Pressed on a different own piece -> switch selection */
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
            }
        }

        /* --- Render --- */
        LCD_Fill_Buffer(COL_BG);
        render_board();
        render_status();
        LCD_Refresh(&cfg0);

        /* Frame cap */
        uint32_t elapsed = HAL_GetTick() - frame_start;
        if (elapsed < CHESS_FRAME_MS) HAL_Delay(CHESS_FRAME_MS - elapsed);
    }
}
