#include "Game_3.h"
#include "InputHandler.h"
#include "LCD.h"
#include "Joystick.h"
#include "PWM.h"
#include "Buzzer.h"
#include "adc.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;

#define CELL_PX             3
#define SCORE_BAR_H         24
#define PLAY_X              0
#define PLAY_Y              SCORE_BAR_H
#define GRID_COLS           (ST7789V2_WIDTH / CELL_PX)
#define GRID_ROWS           ((ST7789V2_HEIGHT - SCORE_BAR_H) / CELL_PX)

#define CELL_EMPTY          0u
#define CELL_P1             1u
#define CELL_P2             2u
#define CELL_WALL           3u

#define FRAME_MS            30u
#define MOVE_MS             105u
#define COUNTDOWN_MS        1000u
#define ROUND_END_MS        1800u
#define GAME_OVER_MS        3000u
#define WINS_TO_WIN         3u
#define INPUT_THRESHOLD     0.25f

#define COL_BG              0u
#define COL_TEXT            1u
#define COL_P1              14u
#define COL_P2              15u
#define COL_WALL            13u
#define COL_WARN            6u

typedef enum {
    TRON_COUNTDOWN = 0,
    TRON_PLAYING,
    TRON_ROUND_END,
    TRON_PAUSED,
    TRON_GAME_OVER
} TronState;

typedef struct {
    int16_t x;
    int16_t y;
    Direction dir;
    uint8_t alive;
    uint8_t score;
    uint8_t cell_value;
    uint8_t colour;
} TronBike;

static uint8_t grid[GRID_ROWS][GRID_COLS];
static TronBike p1;
static TronBike p2;
static TronState tron_state;
static TronState paused_resume_state;
static uint32_t state_tick;
static uint32_t move_tick;
static uint32_t pause_tick;
static uint32_t tone_until_tick;
static uint8_t countdown_n;
static uint8_t p2_joystick_ready;

/* Player 2 uses A0/A1, matching ADC1 channels PA0/PA1 from the CubeMX ADC setup. */
static Joystick_cfg_t joystick2_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_5,
    .y_channel = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};
static Joystick_t joystick2_data;

static void play_tone(uint32_t freq_hz, uint16_t duration_ms, uint8_t volume)
{
    buzzer_tone(&buzzer_cfg, freq_hz, volume);
    tone_until_tick = HAL_GetTick() + duration_ms;
}

static void service_tone(void)
{
    if (tone_until_tick != 0u &&
        (int32_t)(HAL_GetTick() - tone_until_tick) >= 0) {
        buzzer_off(&buzzer_cfg);
        tone_until_tick = 0u;
    }
}

static uint8_t is_vertical(Direction dir)
{
    return (dir == N || dir == S) ? 1u : 0u;
}

static Direction prevent_reverse(Direction next, Direction current)
{
    if ((next == N && current == S) ||
        (next == S && current == N) ||
        (next == E && current == W) ||
        (next == W && current == E)) {
        return current;
    }

    return next;
}

static Direction cardinal_from_input(UserInput input, Direction current)
{
    if (input.magnitude < INPUT_THRESHOLD) {
        return current;
    }

    switch (input.direction) {
        case N:
        case E:
        case S:
        case W:
            return prevent_reverse(input.direction, current);

        case NE:
            return prevent_reverse(is_vertical(current) ? E : N, current);

        case SE:
            return prevent_reverse(is_vertical(current) ? E : S, current);

        case SW:
            return prevent_reverse(is_vertical(current) ? W : S, current);

        case NW:
            return prevent_reverse(is_vertical(current) ? W : N, current);

        case CENTRE:
        default:
            return current;
    }
}

static void reset_grid(void)
{
    memset(grid, CELL_EMPTY, sizeof(grid));

    for (uint8_t c = 0; c < GRID_COLS; c++) {
        grid[0][c] = CELL_WALL;
        grid[GRID_ROWS - 1u][c] = CELL_WALL;
    }

    for (uint8_t r = 0; r < GRID_ROWS; r++) {
        grid[r][0] = CELL_WALL;
        grid[r][GRID_COLS - 1u] = CELL_WALL;
    }
}

static void reset_round(void)
{
    reset_grid();

    p1.x = 10;
    p1.y = 12;
    p1.dir = E;
    p1.alive = 1u;
    p1.cell_value = CELL_P1;
    p1.colour = COL_P1;

    p2.x = GRID_COLS - 11;
    p2.y = GRID_ROWS - 13;
    p2.dir = W;
    p2.alive = 1u;
    p2.cell_value = CELL_P2;
    p2.colour = COL_P2;

    countdown_n = 3u;
    tron_state = TRON_COUNTDOWN;
    state_tick = HAL_GetTick();
    move_tick = state_tick;
}

static void reset_game(void)
{
    p1.score = 0u;
    p2.score = 0u;
    reset_round();
}

static void next_position(const TronBike *bike, int16_t *x, int16_t *y)
{
    *x = bike->x;
    *y = bike->y;

    switch (bike->dir) {
        case N:
            (*y)--;
            break;
        case S:
            (*y)++;
            break;
        case E:
            (*x)++;
            break;
        case W:
            (*x)--;
            break;
        default:
            break;
    }
}

static uint8_t blocked_cell(int16_t x, int16_t y)
{
    if (x < 0 || x >= GRID_COLS || y < 0 || y >= GRID_ROWS) {
        return 1u;
    }

    return (grid[y][x] != CELL_EMPTY) ? 1u : 0u;
}

static void update_directions(void)
{
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);

    p1.dir = cardinal_from_input(Joystick_GetInput(&joystick_data), p1.dir);
    p2.dir = cardinal_from_input(Joystick_GetInput(&joystick2_data), p2.dir);
}

static void finish_round(uint8_t p1_crashed, uint8_t p2_crashed)
{
    p1.alive = p1_crashed ? 0u : 1u;
    p2.alive = p2_crashed ? 0u : 1u;

    if (p1_crashed && !p2_crashed) {
        p2.score++;
        play_tone(NOTE_A4, 180u, 35u);
    } else if (p2_crashed && !p1_crashed) {
        p1.score++;
        play_tone(NOTE_C5, 180u, 35u);
    } else {
        play_tone(NOTE_D4, 220u, 35u);
    }

    tron_state = TRON_ROUND_END;
    state_tick = HAL_GetTick();
}

static void advance_bikes(void)
{
    int16_t p1_next_x;
    int16_t p1_next_y;
    int16_t p2_next_x;
    int16_t p2_next_y;

    grid[p1.y][p1.x] = p1.cell_value;
    grid[p2.y][p2.x] = p2.cell_value;

    next_position(&p1, &p1_next_x, &p1_next_y);
    next_position(&p2, &p2_next_x, &p2_next_y);

    uint8_t p1_crashed = blocked_cell(p1_next_x, p1_next_y);
    uint8_t p2_crashed = blocked_cell(p2_next_x, p2_next_y);

    if (p1_next_x == p2_next_x && p1_next_y == p2_next_y) {
        p1_crashed = 1u;
        p2_crashed = 1u;
    }

    if (p1_crashed || p2_crashed) {
        finish_round(p1_crashed, p2_crashed);
        return;
    }

    p1.x = p1_next_x;
    p1.y = p1_next_y;
    p2.x = p2_next_x;
    p2.y = p2_next_y;
}

static void update_game(void)
{
    uint32_t now = HAL_GetTick();

    switch (tron_state) {
        case TRON_COUNTDOWN:
            if ((now - state_tick) >= COUNTDOWN_MS) {
                state_tick = now;
                if (countdown_n > 0u) {
                    countdown_n--;
                }
                if (countdown_n == 0u) {
                    tron_state = TRON_PLAYING;
                    move_tick = now;
                    play_tone(NOTE_A5, 90u, 30u);
                }
            }
            break;

        case TRON_PLAYING:
            update_directions();
            if ((now - move_tick) >= MOVE_MS) {
                move_tick = now;
                advance_bikes();
            }
            break;

        case TRON_ROUND_END:
            if ((now - state_tick) >= ROUND_END_MS) {
                if (p1.score >= WINS_TO_WIN || p2.score >= WINS_TO_WIN) {
                    tron_state = TRON_GAME_OVER;
                    state_tick = now;
                    play_tone(NOTE_C6, 450u, 35u);
                } else {
                    reset_round();
                }
            }
            break;

        case TRON_PAUSED:
            break;

        case TRON_GAME_OVER:
        default:
            break;
    }
}

static void draw_cell(uint8_t col, uint8_t row, uint8_t colour)
{
    LCD_Draw_Rect((uint16_t)(PLAY_X + (col * CELL_PX)),
                  (uint16_t)(PLAY_Y + (row * CELL_PX)),
                  CELL_PX,
                  CELL_PX,
                  colour,
                  1u);
}

static void draw_score_bar(void)
{
    char text[16];

    LCD_Draw_Rect(0, 0, ST7789V2_WIDTH, SCORE_BAR_H, COL_BG, 1u);

    LCD_printString("P1", 3, 3, COL_P1, 1);
    snprintf(text, sizeof(text), "%u", (unsigned int)p1.score);
    LCD_printString(text, 22, 3, COL_P1, 2);

    LCD_printString("TRON", 91, 3, COL_TEXT, 2);

    LCD_printString("P2", 178, 3, COL_P2, 1);
    snprintf(text, sizeof(text), "%u", (unsigned int)p2.score);
    LCD_printString(text, 198, 3, COL_P2, 2);
}

static void draw_grid(void)
{
    for (uint8_t row = 0; row < GRID_ROWS; row++) {
        for (uint8_t col = 0; col < GRID_COLS; col++) {
            switch (grid[row][col]) {
                case CELL_P1:
                    draw_cell(col, row, COL_P1);
                    break;
                case CELL_P2:
                    draw_cell(col, row, COL_P2);
                    break;
                case CELL_WALL:
                    draw_cell(col, row, COL_WALL);
                    break;
                default:
                    break;
            }
        }
    }

    if (p1.alive) {
        draw_cell((uint8_t)p1.x, (uint8_t)p1.y, COL_TEXT);
    }
    if (p2.alive) {
        draw_cell((uint8_t)p2.x, (uint8_t)p2.y, COL_TEXT);
    }
}

static void draw_center_text(const char *top, const char *bottom, uint8_t colour)
{
    if (top != NULL) {
        LCD_printString(top, 42, 92, colour, 3);
    }
    if (bottom != NULL) {
        LCD_printString(bottom, 54, 126, colour, 2);
    }
}

static void render_game(void)
{
    LCD_Fill_Buffer(COL_BG);
    draw_score_bar();
    draw_grid();

    if (tron_state == TRON_COUNTDOWN && countdown_n > 0u) {
        char text[4];
        snprintf(text, sizeof(text), "%u", (unsigned int)countdown_n);
        LCD_printString("READY", 66, 80, COL_TEXT, 3);
        LCD_printString(text, 108, 120, COL_WARN, 5);
    } else if (tron_state == TRON_ROUND_END) {
        if (!p1.alive && !p2.alive) {
            draw_center_text("DRAW", "No point", COL_TEXT);
        } else if (!p1.alive) {
            draw_center_text("P2 SCORES", "Next round", COL_P2);
        } else {
            draw_center_text("P1 SCORES", "Next round", COL_P1);
        }
    } else if (tron_state == TRON_PAUSED) {
        draw_center_text("PAUSED", "BT2 resumes", COL_WARN);
    } else if (tron_state == TRON_GAME_OVER) {
        if (p1.score >= WINS_TO_WIN) {
            draw_center_text("PLAYER 1", "WINS", COL_P1);
        } else {
            draw_center_text("PLAYER 2", "WINS", COL_P2);
        }
        LCD_printString("Returning to menu", 36, 176, COL_TEXT, 1);
    }

    LCD_Refresh(&cfg0);
}

static void toggle_pause(void)
{
    uint32_t now = HAL_GetTick();

    if (tron_state == TRON_PAUSED) {
        uint32_t paused_ms = now - pause_tick;
        state_tick += paused_ms;
        move_tick += paused_ms;
        tron_state = paused_resume_state;
        play_tone(NOTE_E5, 80u, 25u);
    } else if (tron_state == TRON_COUNTDOWN || tron_state == TRON_PLAYING) {
        paused_resume_state = tron_state;
        pause_tick = now;
        tron_state = TRON_PAUSED;
        buzzer_off(&buzzer_cfg);
        tone_until_tick = 0u;
    }
}

MenuState Game3_Run(void)
{
    if (!p2_joystick_ready) {
        Joystick_Init(&joystick2_cfg);
        Joystick_Calibrate(&joystick2_cfg);
        p2_joystick_ready = 1u;
    }

    LCD_Set_Palette(PALETTE_DEFAULT);
    PWM_SetDuty(&pwm_cfg, 35u);
    reset_game();
    play_tone(NOTE_C5, 120u, 30u);

    while (1) {
        uint32_t frame_start = HAL_GetTick();

        Input_Read();
        service_tone();

        if (current_input.btn3_pressed) {
            break;
        }

        if (current_input.btn2_pressed) {
            toggle_pause();
        }

        update_game();
        render_game();
        service_tone();

        if (tron_state == TRON_GAME_OVER &&
            (HAL_GetTick() - state_tick) >= GAME_OVER_MS) {
            break;
        }

        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < FRAME_MS) {
            HAL_Delay(FRAME_MS - frame_time);
        }
    }

    buzzer_off(&buzzer_cfg);
    tone_until_tick = 0u;
    PWM_SetDuty(&pwm_cfg, 50u);

    return MENU_STATE_HOME;
}
