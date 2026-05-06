// Battleship: hot-seat naval combat for the ELEC2645 console.

#include "Game_3.h"
#include "InputHandler.h"
#include "LCD.h"
#include "Joystick.h"
#include "Buzzer.h"
#include "PWM.h"
#include "stm32l4xx_hal.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t   cfg0;
extern Joystick_cfg_t   joystick_cfg;
extern Joystick_t       joystick_data;
extern InputState       current_input;
extern Buzzer_cfg_t     buzzer_cfg;
extern PWM_cfg_t        pwm_cfg;
extern ADC_HandleTypeDef hadc1;

// P2 joystick uses the spare ADC channels, matching Game 1.
static Joystick_cfg_t joystick2_cfg = {
    .adc           = &hadc1,
    .x_channel     = ADC_CHANNEL_5,
    .y_channel     = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x      = JOYSTICK_DEFAULT_CENTER_X,
    .center_y      = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone      = JOYSTICK_DEADZONE,
    .setup_done    = 0
};
static Joystick_t joystick2_data;
static uint8_t    joystick2_ready = 0;

#define BS_GRID_SIZE        9u
#define BS_NUM_PLAYERS      2u
#define BS_NUM_SHIPS        4u
#define BS_TOTAL_SHIP_CELLS 12u

#define BS_EMPTY    0u
#define BS_SHIP     1u
#define BS_HIT      2u
#define BS_MISS     3u
#define BS_SUNK     4u

#define BS_LCD_W            240u
#define BS_LCD_H            240u
#define BS_GRID_X           10u
#define BS_GRID_Y           34u
#define BS_CELL_PITCH       18u
#define BS_CELL_SIZE        16u
#define BS_GRID_PIXEL       (BS_GRID_SIZE * BS_CELL_PITCH)
#define BS_SIDEBAR_X        176u
#define BS_FRAME_MS         35u
#define BS_MOVE_MS          150u
#define BS_RESULT_MS        1300u
#define BS_HANDOFF_MS       300u
#define BS_NUKE_HOLD_MS     1000u
#define BS_NUKE_CHARGE_MS   1800u
#define BS_BACK_HOLD_MS     1000u
#define BS_STICK_THRESHOLD  0.40f

#define COL_BG     0    // black
#define COL_TEXT   1    // white
#define COL_HIT    2    // red
#define COL_OK     3    // green
#define COL_WATER  4    // blue
#define COL_USED   5    // orange
#define COL_MISS   6    // yellow
#define COL_NUKE   8    // purple
#define COL_P1     9    // navy
#define COL_LABEL 10    // gold
#define COL_SUNK  13    // grey
#define COL_SHIP  14    // cyan
#define COL_P2    15    // magenta

typedef enum {
    BS_STATE_SETUP_P1 = 0,
    BS_STATE_HANDOFF_TO_P2_SETUP,
    BS_STATE_SETUP_P2,
    BS_STATE_HANDOFF_TO_P1_START,
    BS_STATE_TURN_P1,
    BS_STATE_RESULT_P1,
    BS_STATE_HANDOFF_TO_P2_TURN,
    BS_STATE_TURN_P2,
    BS_STATE_RESULT_P2,
    BS_STATE_HANDOFF_TO_P1_TURN,
    BS_STATE_EXIT_CONFIRM,
    BS_STATE_GAME_OVER
} BS_State;

typedef struct {
    int8_t  dx;
    int8_t  dy;
    uint8_t fire_tap;
    uint8_t nuke_start;
    uint8_t nuke_charging;
    uint8_t nuke_release;
    uint8_t ext_tap;
    uint8_t back_request;
} BS_Input;

typedef struct {
    uint8_t  was_down;
    uint8_t  long_consumed;
    uint32_t down_tick;
} BtnTrack;

typedef struct {
    const char *name;
    uint8_t size;
    uint8_t x;
    uint8_t y;
    uint8_t horizontal;
    uint8_t placed;
    uint8_t sunk;
} BS_Ship;

typedef struct {
    uint16_t shots_fired;
    uint16_t hits;
    uint8_t  ships_sunk;
    uint8_t  best_streak;
    uint8_t  current_streak;
    uint8_t  ability_used;
} BS_Stats;

static uint8_t  bs_fleet[BS_NUM_PLAYERS][BS_GRID_SIZE][BS_GRID_SIZE];
static BS_Ship  bs_ships[BS_NUM_PLAYERS][BS_NUM_SHIPS];
static BS_Stats bs_stats[BS_NUM_PLAYERS];
static uint8_t  bs_cursor_x[BS_NUM_PLAYERS];
static uint8_t  bs_cursor_y[BS_NUM_PLAYERS];
static uint32_t bs_last_move_tick[BS_NUM_PLAYERS];
static uint8_t  bs_show_fleet[BS_NUM_PLAYERS];

static BtnTrack bs_jbtn[BS_NUM_PLAYERS];
static BtnTrack bs_extbtn;

static BS_State bs_state;
static BS_State bs_exit_return_state;
static uint8_t  bs_setup_player;
static uint8_t  bs_setup_ship_index;
static uint8_t  bs_setup_x;
static uint8_t  bs_setup_y;
static uint8_t  bs_setup_horizontal;
static uint8_t  bs_winner;
static uint8_t  bs_match_score[BS_NUM_PLAYERS];
static uint8_t  bs_last_score_p1;
static uint8_t  bs_last_score_p2;
static uint32_t bs_state_tick;
static char     bs_status[40];
static char     bs_last_shot[20];
static char     bs_player_last_shot[BS_NUM_PLAYERS][20];
static uint8_t  bs_quit_requested;
static uint8_t  bs_ability_charging[BS_NUM_PLAYERS];
static uint32_t bs_ability_start_tick[BS_NUM_PLAYERS];
static uint32_t bs_last_charge_sfx_tick[BS_NUM_PLAYERS];

static const uint8_t bs_ship_sizes[BS_NUM_SHIPS] = {4u, 3u, 3u, 2u};
static const char *bs_ship_names[BS_NUM_SHIPS] = {
    "BATTLESHIP", "CRUISER", "SUB", "DESTROYER"
};

static void bs_set_state(BS_State next);

void Game3_SetMode(uint8_t two_player) { (void)two_player; }
void Game3_SetWrap(uint8_t on)         { (void)on; }
uint8_t Game3_LastScoreP1(void)        { return bs_last_score_p1; }
uint8_t Game3_LastScoreP2(void)        { return bs_last_score_p2; }

static void bs_copy_text(char *dst, uint32_t dst_size, const char *src) {
    if (dst_size != 0u) {
        (void)snprintf(dst, dst_size, "%s", src);
    }
}

static void bs_print_center(const char *text, uint16_t y, uint8_t colour, uint8_t size) {
    uint16_t width = (uint16_t)(strlen(text) * 6u * size);
    uint16_t x = (width >= BS_LCD_W) ? 0u : (uint16_t)((BS_LCD_W - width) / 2u);
    LCD_printString(text, x, y, colour, size);
}

static void bs_led_set(uint8_t duty) {
    if (duty > 100u) duty = 100u;
    PWM_SetDuty(&pwm_cfg, duty);
}

static void bs_led_off(void) { PWM_SetDuty(&pwm_cfg, 0u); }
static void bs_sfx_off(void) { buzzer_off(&buzzer_cfg); }

static void bs_sfx_tone(uint16_t hz, uint16_t ms, uint8_t volume) {
    if (Menu_SoundEnabled()) buzzer_tone(&buzzer_cfg, hz, volume);
    HAL_Delay(ms);
    buzzer_off(&buzzer_cfg);
}

static void bs_sfx_blip(uint16_t hz, uint8_t volume) {
    if (Menu_SoundEnabled()) {
        buzzer_tone(&buzzer_cfg, hz, volume);
        HAL_Delay(12u);
        buzzer_off(&buzzer_cfg);
    }
}

static void bs_sfx_move(void)    { bs_sfx_blip(900u, 12u); }
static void bs_sfx_invalid(void) { bs_led_set(20u); bs_sfx_tone(180u, 120u, 30u); bs_led_off(); }
static void bs_sfx_miss(void)    { bs_led_set(25u); bs_sfx_tone(260u, 120u, 35u); bs_led_off(); }
static void bs_sfx_hit(void)     { bs_led_set(100u); bs_sfx_tone(1050u, 190u, 45u); bs_led_off(); }

static void bs_sfx_sunk(void) {
    static const uint16_t notes[5] = {500u, 650u, 800u, 950u, 700u};
    static const uint16_t lens[5]  = {300u, 300u, 300u, 300u, 500u};
    uint8_t i;
    for (i = 0u; i < 5u; i++) {
        bs_led_set((uint8_t)(30u + i * 17u));
        bs_sfx_tone(notes[i], lens[i], 45u);
        if (i < 4u) { bs_led_set(5u); HAL_Delay(100u); }
    }
    bs_led_off();
}

static void bs_sfx_win(void) {
    static const uint16_t notes[5] = {600u, 800u, 1000u, 1200u, 1500u};
    uint8_t i;
    for (i = 0u; i < 5u; i++) {
        bs_led_set((i & 1u) ? 35u : 100u);
        bs_sfx_tone(notes[i], 220u, 45u);
        HAL_Delay(70u);
    }
    bs_led_off();
}

static void bs_sfx_charge(uint16_t freq) {
    if (Menu_SoundEnabled()) buzzer_tone(&buzzer_cfg, freq, 18u);
}

static void bs_set_state(BS_State next) {
    bs_state = next;
    bs_state_tick = HAL_GetTick();

    if (next == BS_STATE_TURN_P1 || next == BS_STATE_TURN_P2) {
        uint8_t p = (next == BS_STATE_TURN_P1) ? 0u : 1u;
        bs_show_fleet[p] = 0u;
        bs_copy_text(bs_status, sizeof(bs_status), "AIM AND FIRE");
    } else if (next == BS_STATE_SETUP_P1) {
        bs_setup_player = 0u;
        bs_setup_ship_index = 0u;
        bs_setup_x = 0u; bs_setup_y = 0u;
        bs_setup_horizontal = 1u;
    } else if (next == BS_STATE_SETUP_P2) {
        bs_setup_player = 1u;
        bs_setup_ship_index = 0u;
        bs_setup_x = 0u; bs_setup_y = 0u;
        bs_setup_horizontal = 1u;
    }
}

static void bs_init_ships(uint8_t player) {
    uint8_t i;
    for (i = 0u; i < BS_NUM_SHIPS; i++) {
        bs_ships[player][i].name = bs_ship_names[i];
        bs_ships[player][i].size = bs_ship_sizes[i];
        bs_ships[player][i].x = 0u;
        bs_ships[player][i].y = 0u;
        bs_ships[player][i].horizontal = 1u;
        bs_ships[player][i].placed = 0u;
        bs_ships[player][i].sunk = 0u;
    }
}

static void bs_reset_game(void) {
    uint8_t p;
    memset(bs_fleet, 0, sizeof(bs_fleet));
    memset(bs_stats, 0, sizeof(bs_stats));
    memset(bs_ability_charging, 0, sizeof(bs_ability_charging));
    memset(bs_ability_start_tick, 0, sizeof(bs_ability_start_tick));
    memset(bs_last_charge_sfx_tick, 0, sizeof(bs_last_charge_sfx_tick));
    memset(bs_show_fleet, 0, sizeof(bs_show_fleet));
    memset(bs_jbtn, 0, sizeof(bs_jbtn));
    memset(&bs_extbtn, 0, sizeof(bs_extbtn));
    memset(bs_player_last_shot, 0, sizeof(bs_player_last_shot));

    for (p = 0u; p < BS_NUM_PLAYERS; p++) {
        bs_init_ships(p);
        bs_cursor_x[p] = 4u;
        bs_cursor_y[p] = 4u;
        bs_last_move_tick[p] = 0u;
    }

    bs_winner = 0u;
    bs_quit_requested = 0u;
    bs_exit_return_state = BS_STATE_SETUP_P1;
    bs_match_score[0] = 0u;
    bs_match_score[1] = 0u;
    bs_copy_text(bs_status, sizeof(bs_status), "READY");
    bs_copy_text(bs_last_shot, sizeof(bs_last_shot), "");
}

static void bs_start_new_match(void) {
    bs_reset_game();
    bs_set_state(BS_STATE_SETUP_P1);
}

static uint8_t bs_can_place(uint8_t player, uint8_t ship_index,
                            uint8_t x, uint8_t y, uint8_t horizontal) {
    uint8_t i;
    uint8_t size = bs_ships[player][ship_index].size;

    if (horizontal) {
        if ((uint16_t)x + size > BS_GRID_SIZE) return 0u;
    } else {
        if ((uint16_t)y + size > BS_GRID_SIZE) return 0u;
    }

    for (i = 0u; i < size; i++) {
        uint8_t cx = (uint8_t)(x + (horizontal ? i : 0u));
        uint8_t cy = (uint8_t)(y + (horizontal ? 0u : i));
        if (bs_fleet[player][cy][cx] != BS_EMPTY) return 0u;
    }
    return 1u;
}

static void bs_place_ship(uint8_t player, uint8_t ship_index,
                          uint8_t x, uint8_t y, uint8_t horizontal) {
    uint8_t i;
    uint8_t size = bs_ships[player][ship_index].size;
    for (i = 0u; i < size; i++) {
        uint8_t cx = (uint8_t)(x + (horizontal ? i : 0u));
        uint8_t cy = (uint8_t)(y + (horizontal ? 0u : i));
        bs_fleet[player][cy][cx] = BS_SHIP;
    }
    bs_ships[player][ship_index].x = x;
    bs_ships[player][ship_index].y = y;
    bs_ships[player][ship_index].horizontal = horizontal;
    bs_ships[player][ship_index].placed = 1u;
}

static uint8_t bs_all_ships_placed(uint8_t player) {
    uint8_t i;
    for (i = 0u; i < BS_NUM_SHIPS; i++) {
        if (!bs_ships[player][i].placed) return 0u;
    }
    return 1u;
}

static int8_t bs_ship_at(uint8_t player, uint8_t x, uint8_t y) {
    uint8_t i, j;
    for (i = 0u; i < BS_NUM_SHIPS; i++) {
        BS_Ship *s = &bs_ships[player][i];
        if (!s->placed) continue;
        for (j = 0u; j < s->size; j++) {
            uint8_t cx = (uint8_t)(s->x + (s->horizontal ? j : 0u));
            uint8_t cy = (uint8_t)(s->y + (s->horizontal ? 0u : j));
            if (cx == x && cy == y) return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t bs_is_ship_sunk(uint8_t player, uint8_t ship_index) {
    uint8_t i;
    BS_Ship *s = &bs_ships[player][ship_index];
    for (i = 0u; i < s->size; i++) {
        uint8_t x = (uint8_t)(s->x + (s->horizontal ? i : 0u));
        uint8_t y = (uint8_t)(s->y + (s->horizontal ? 0u : i));
        if (bs_fleet[player][y][x] != BS_HIT && bs_fleet[player][y][x] != BS_SUNK) return 0u;
    }
    return 1u;
}

static void bs_mark_ship_sunk(uint8_t player, uint8_t ship_index) {
    uint8_t i;
    BS_Ship *s = &bs_ships[player][ship_index];
    for (i = 0u; i < s->size; i++) {
        uint8_t x = (uint8_t)(s->x + (s->horizontal ? i : 0u));
        uint8_t y = (uint8_t)(s->y + (s->horizontal ? 0u : i));
        bs_fleet[player][y][x] = BS_SUNK;
    }
    s->sunk = 1u;
}

static uint8_t bs_cells_destroyed(uint8_t player) {
    uint8_t x, y, count = 0u;
    for (y = 0u; y < BS_GRID_SIZE; y++) {
        for (x = 0u; x < BS_GRID_SIZE; x++) {
            if (bs_fleet[player][y][x] == BS_HIT || bs_fleet[player][y][x] == BS_SUNK) count++;
        }
    }
    return count;
}

// Returns 0=invalid, 1=miss, 2=hit, 3=sunk.
static uint8_t bs_fire_cell(uint8_t shooter, uint8_t x, uint8_t y, char *message, uint32_t msg_size) {
    uint8_t target = (uint8_t)(1u - shooter);
    uint8_t cell;
    int8_t  ship_index;

    if (x >= BS_GRID_SIZE || y >= BS_GRID_SIZE) {
        bs_copy_text(message, msg_size, "OUT OF RANGE");
        return 0u;
    }

    cell = bs_fleet[target][y][x];
    if (cell == BS_MISS || cell == BS_HIT || cell == BS_SUNK) {
        bs_copy_text(message, msg_size, "ALREADY HIT");
        return 0u;
    }

    bs_stats[shooter].shots_fired++;

    if (cell == BS_SHIP) {
        bs_fleet[target][y][x] = BS_HIT;
        bs_stats[shooter].hits++;
        bs_stats[shooter].current_streak++;
        if (bs_stats[shooter].current_streak > bs_stats[shooter].best_streak) {
            bs_stats[shooter].best_streak = bs_stats[shooter].current_streak;
        }
        ship_index = bs_ship_at(target, x, y);
        if (ship_index >= 0 && bs_is_ship_sunk(target, (uint8_t)ship_index)) {
            bs_mark_ship_sunk(target, (uint8_t)ship_index);
            bs_stats[shooter].ships_sunk++;
            (void)snprintf(message, msg_size, "%s SUNK", bs_ships[target][ship_index].name);
            return 3u;
        }
        bs_copy_text(message, msg_size, "HIT!");
        return 2u;
    }

    bs_fleet[target][y][x] = BS_MISS;
    bs_stats[shooter].current_streak = 0u;
    bs_copy_text(message, msg_size, "MISS");
    return 1u;
}

static void bs_record_last_shot(uint8_t shooter, uint8_t x, uint8_t y, uint8_t code) {
    const char *what = (code == 3u) ? "SUNK" : (code == 2u) ? "HIT" : "MISS";
    (void)snprintf(bs_last_shot, sizeof(bs_last_shot), "%c%u %s",
                   (char)('A' + x), (unsigned)(y + 1u), what);
    bs_copy_text(bs_player_last_shot[shooter], sizeof(bs_player_last_shot[shooter]), bs_last_shot);
}

static void bs_after_shot_result(uint8_t shooter, uint8_t code) {
    if (bs_cells_destroyed((uint8_t)(1u - shooter)) >= BS_TOTAL_SHIP_CELLS) {
        bs_winner = shooter;
        bs_match_score[shooter] = 1u;
        bs_match_score[(uint8_t)(1u - shooter)] = 0u;
        bs_sfx_win();
        bs_set_state(BS_STATE_GAME_OVER);
        return;
    }
    if      (code == 3u) bs_sfx_sunk();
    else if (code == 2u) bs_sfx_hit();
    else if (code == 1u) bs_sfx_miss();

    bs_set_state(shooter == 0u ? BS_STATE_RESULT_P1 : BS_STATE_RESULT_P2);
}

// Edge-detects tap, long-hold start, held state, and long release from one pin.
static void bs_track_button(BtnTrack *t, uint8_t pin_low, uint32_t long_ms,
                            uint8_t *tap, uint8_t *long_pressed,
                            uint8_t *held_long, uint8_t *long_released) {
    uint32_t now = HAL_GetTick();
    *tap = 0u;
    *long_pressed = 0u;
    *held_long = 0u;
    *long_released = 0u;

    if (pin_low) {
        if (!t->was_down) {
            t->was_down = 1u;
            t->down_tick = now;
            t->long_consumed = 0u;
        }
        if (!t->long_consumed && (now - t->down_tick) >= long_ms) {
            *long_pressed = 1u;
            t->long_consumed = 1u;
        }
        if (t->long_consumed) *held_long = 1u;
    } else {
        if (t->was_down) {
            uint32_t held = now - t->down_tick;
            if (held < long_ms) *tap = 1u;
            else if (t->long_consumed) *long_released = 1u;
            t->was_down = 0u;
            t->long_consumed = 0u;
        }
    }
}

static void bs_read_stick(uint8_t player, BS_Input *out) {
    UserInput joy;
    if (player == 0u) {
        Joystick_Read(&joystick_cfg, &joystick_data);
        joy = Joystick_GetInput(&joystick_data);
    } else {
        Joystick_Read(&joystick2_cfg, &joystick2_data);
        joy = Joystick_GetInput(&joystick2_data);
    }
    if (joy.magnitude > BS_STICK_THRESHOLD) {
        if      (joy.direction == N || joy.direction == NE || joy.direction == NW) out->dy = -1;
        else if (joy.direction == S || joy.direction == SE || joy.direction == SW) out->dy = 1;
        if      (joy.direction == E || joy.direction == NE || joy.direction == SE) out->dx = 1;
        else if (joy.direction == W || joy.direction == NW || joy.direction == SW) out->dx = -1;
    }
}

static void bs_read_input(uint8_t player, BS_Input *out) {
    uint16_t jbtn_pin = (player == 0u) ? BTN3_Pin : BTN8_Pin;
    GPIO_TypeDef *jbtn_port = (player == 0u) ? BTN3_GPIO_Port : BTN8_GPIO_Port;
    uint8_t jbtn_low = (HAL_GPIO_ReadPin(jbtn_port, jbtn_pin) == GPIO_PIN_RESET) ? 1u : 0u;
    uint8_t ext_low  = (HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin) == GPIO_PIN_RESET) ? 1u : 0u;
    uint8_t tmp_held, tmp_release;

    memset(out, 0, sizeof(*out));
    bs_read_stick(player, out);

    bs_track_button(&bs_jbtn[player], jbtn_low, BS_NUKE_HOLD_MS,
                    &out->fire_tap, &out->nuke_start,
                    &out->nuke_charging, &out->nuke_release);

    bs_track_button(&bs_extbtn, ext_low, BS_BACK_HOLD_MS,
                    &out->ext_tap, &out->back_request, &tmp_held, &tmp_release);
    (void)tmp_held;
    (void)tmp_release;
}

static void bs_apply_cursor_move(uint8_t player, int8_t dx, int8_t dy) {
    uint32_t now = HAL_GetTick();
    int8_t nx, ny;
    if (dx == 0 && dy == 0) return;
    if (now - bs_last_move_tick[player] < BS_MOVE_MS) return;
    bs_last_move_tick[player] = now;

    nx = (int8_t)bs_cursor_x[player] + dx;
    ny = (int8_t)bs_cursor_y[player] + dy;
    if (nx < 0) nx = 0; else if (nx >= (int8_t)BS_GRID_SIZE) nx = (int8_t)BS_GRID_SIZE - 1;
    if (ny < 0) ny = 0; else if (ny >= (int8_t)BS_GRID_SIZE) ny = (int8_t)BS_GRID_SIZE - 1;
    if ((uint8_t)nx != bs_cursor_x[player] || (uint8_t)ny != bs_cursor_y[player]) {
        bs_cursor_x[player] = (uint8_t)nx;
        bs_cursor_y[player] = (uint8_t)ny;
        bs_sfx_move();
    }
}

static void bs_apply_setup_move(BS_Input *input) {
    uint32_t now = HAL_GetTick();
    int8_t nx, ny;
    if (input->dx == 0 && input->dy == 0) return;
    if (now - bs_last_move_tick[bs_setup_player] < BS_MOVE_MS) return;
    bs_last_move_tick[bs_setup_player] = now;

    nx = (int8_t)bs_setup_x + input->dx;
    ny = (int8_t)bs_setup_y + input->dy;
    if (nx < 0) nx = 0; else if (nx >= (int8_t)BS_GRID_SIZE) nx = (int8_t)BS_GRID_SIZE - 1;
    if (ny < 0) ny = 0; else if (ny >= (int8_t)BS_GRID_SIZE) ny = (int8_t)BS_GRID_SIZE - 1;
    bs_setup_x = (uint8_t)nx;
    bs_setup_y = (uint8_t)ny;
    bs_sfx_move();
}

static uint8_t bs_player_colour(uint8_t player) {
    return (player == 0u) ? COL_P1 : COL_P2;
}

static uint8_t bs_cell_colour(uint8_t cell, uint8_t own_grid) {
    switch (cell) {
        case BS_SHIP: return own_grid ? COL_SHIP : COL_WATER;
        case BS_HIT:  return COL_HIT;
        case BS_MISS: return COL_MISS;
        case BS_SUNK: return COL_SUNK;
        default:      return COL_WATER;
    }
}

static void bs_draw_grid(uint16_t x0, uint16_t y0, uint8_t owner, uint8_t own_grid,
                         uint8_t cursor_x, uint8_t cursor_y, uint8_t show_cursor,
                         uint8_t cursor_col) {
    uint8_t x, y;
    char label[4];

    for (y = 0u; y < BS_GRID_SIZE; y++) {
        for (x = 0u; x < BS_GRID_SIZE; x++) {
            uint8_t cell = bs_fleet[owner][y][x];
            uint8_t draw = (own_grid || cell != BS_SHIP) ? cell : BS_EMPTY;
            LCD_Draw_Rect((uint16_t)(x0 + x * BS_CELL_PITCH),
                          (uint16_t)(y0 + y * BS_CELL_PITCH),
                          BS_CELL_SIZE, BS_CELL_SIZE,
                          bs_cell_colour(draw, own_grid), 1u);
        }
    }

    for (x = 0u; x <= BS_GRID_SIZE; x++) {
        LCD_Draw_Rect((uint16_t)(x0 + x * BS_CELL_PITCH), y0, 1u, BS_GRID_PIXEL, COL_TEXT, 1u);
        LCD_Draw_Rect(x0, (uint16_t)(y0 + x * BS_CELL_PITCH), BS_GRID_PIXEL, 1u, COL_TEXT, 1u);
    }

    for (x = 0u; x < BS_GRID_SIZE; x++) {
        label[0] = (char)('A' + x);
        label[1] = '\0';
        LCD_printString(label, (uint16_t)(x0 + x * BS_CELL_PITCH + 5u),
                        (uint16_t)(y0 - 10u), COL_LABEL, 1u);
    }
    for (y = 0u; y < BS_GRID_SIZE; y++) {
        (void)snprintf(label, sizeof(label), "%u", (unsigned)(y + 1u));
        LCD_printString(label, (uint16_t)(x0 - 8u),
                        (uint16_t)(y0 + y * BS_CELL_PITCH + 5u), COL_LABEL, 1u);
    }

    if (show_cursor) {
        uint16_t cx = (uint16_t)(x0 + cursor_x * BS_CELL_PITCH);
        uint16_t cy = (uint16_t)(y0 + cursor_y * BS_CELL_PITCH);
        LCD_Draw_Rect(cx, cy, BS_CELL_SIZE, BS_CELL_SIZE, cursor_col, 0u);
        LCD_Draw_Rect((uint16_t)(cx + 1u), (uint16_t)(cy + 1u),
                      BS_CELL_SIZE - 2u, BS_CELL_SIZE - 2u, cursor_col, 0u);
    }
}

static void bs_draw_setup_preview(uint8_t valid) {
    uint8_t i;
    const BS_Ship *ship = &bs_ships[bs_setup_player][bs_setup_ship_index];
    uint8_t colour = valid ? COL_OK : COL_USED;
    for (i = 0u; i < ship->size; i++) {
        uint8_t x = (uint8_t)(bs_setup_x + (bs_setup_horizontal ? i : 0u));
        uint8_t y = (uint8_t)(bs_setup_y + (bs_setup_horizontal ? 0u : i));
        if (x < BS_GRID_SIZE && y < BS_GRID_SIZE) {
            LCD_Draw_Rect((uint16_t)(BS_GRID_X + x * BS_CELL_PITCH),
                          (uint16_t)(BS_GRID_Y + y * BS_CELL_PITCH),
                          BS_CELL_SIZE, BS_CELL_SIZE, colour, 1u);
        }
    }
}

static void bs_draw_top_bar(uint8_t player) {
    char buf[24];
    uint8_t opp = (uint8_t)(1u - player);
    LCD_Draw_Rect(0u, 0u, BS_LCD_W, 22u, bs_player_colour(player), 1u);
    (void)snprintf(buf, sizeof(buf), "PLAYER %d", (int)player + 1);
    LCD_printString(buf, 6u, 7u, COL_TEXT, 2u);
    LCD_printString("HIT", 150u, 7u, COL_TEXT, 1u);
    (void)snprintf(buf, sizeof(buf), "%u/%u",
                   (unsigned)bs_cells_destroyed(opp), (unsigned)BS_TOTAL_SHIP_CELLS);
    LCD_printString(buf, 176u, 5u, COL_TEXT, 2u);
}

static void bs_draw_sidebar(uint8_t player) {
    char buf[18];
    uint8_t used = bs_stats[player].ability_used;

    LCD_printString("STATS", BS_SIDEBAR_X, 28u, COL_LABEL, 1u);
    (void)snprintf(buf, sizeof(buf), "SHOT %u", (unsigned)bs_stats[player].shots_fired);
    LCD_printString(buf, BS_SIDEBAR_X, 42u, COL_TEXT, 1u);
    (void)snprintf(buf, sizeof(buf), "HIT  %u", (unsigned)bs_stats[player].hits);
    LCD_printString(buf, BS_SIDEBAR_X, 54u, COL_TEXT, 1u);
    (void)snprintf(buf, sizeof(buf), "SUNK %u/%u",
                   (unsigned)bs_stats[player].ships_sunk, (unsigned)BS_NUM_SHIPS);
    LCD_printString(buf, BS_SIDEBAR_X, 66u, COL_TEXT, 1u);

    LCD_printString("NUKE", BS_SIDEBAR_X, 84u, COL_LABEL, 1u);
    LCD_printString(used ? "USED" : "READY",
                    BS_SIDEBAR_X, 96u, used ? COL_USED : COL_OK, 1u);

    LCD_printString("SHOT MAP", BS_SIDEBAR_X, 116u, COL_LABEL, 1u);
    LCD_printString("ALL SHOTS", BS_SIDEBAR_X, 130u, COL_TEXT, 1u);
    LCD_printString("STAY ON", BS_SIDEBAR_X, 142u, COL_TEXT, 1u);
    LCD_printString("GRID", BS_SIDEBAR_X, 154u, COL_TEXT, 1u);

    LCD_printString("LEGEND", BS_SIDEBAR_X, 176u, COL_LABEL, 1u);
    LCD_Draw_Rect(BS_SIDEBAR_X,        190u, 8u, 8u, COL_HIT,   1u);
    LCD_printString("HIT",  (uint16_t)(BS_SIDEBAR_X + 12u), 191u, COL_TEXT, 1u);
    LCD_Draw_Rect(BS_SIDEBAR_X,        202u, 8u, 8u, COL_MISS,  1u);
    LCD_printString("MISS", (uint16_t)(BS_SIDEBAR_X + 12u), 203u, COL_TEXT, 1u);
    LCD_Draw_Rect(BS_SIDEBAR_X,        214u, 8u, 8u, COL_SUNK,  1u);
    LCD_printString("SUNK", (uint16_t)(BS_SIDEBAR_X + 12u), 215u, COL_TEXT, 1u);
    LCD_Draw_Rect(BS_SIDEBAR_X,        226u, 8u, 8u, COL_WATER, 1u);
    LCD_printString("SEA",  (uint16_t)(BS_SIDEBAR_X + 12u), 227u, COL_TEXT, 1u);
}

static void bs_render_setup_screen(uint8_t player) {
    char buf[28];
    uint8_t valid;

    LCD_Fill_Buffer(COL_BG);
    bs_draw_top_bar(player);

    if (bs_setup_ship_index < BS_NUM_SHIPS) {
        (void)snprintf(buf, sizeof(buf), "%s (%u)",
                       bs_ships[player][bs_setup_ship_index].name,
                       (unsigned)bs_ships[player][bs_setup_ship_index].size);
        LCD_printString(buf, BS_SIDEBAR_X, 28u, COL_LABEL, 1u);
    }

    bs_draw_grid(BS_GRID_X, BS_GRID_Y, player, 1u,
                 bs_setup_x, bs_setup_y, 0u, COL_TEXT);
    valid = bs_can_place(player, bs_setup_ship_index, bs_setup_x, bs_setup_y, bs_setup_horizontal);
    bs_draw_setup_preview(valid);

    LCD_printString(bs_setup_horizontal ? "ROT: H" : "ROT: V",
                    BS_SIDEBAR_X, 64u, valid ? COL_OK : COL_USED, 1u);
    LCD_printString("CONTROLS", BS_SIDEBAR_X, 90u, COL_LABEL, 1u);
    LCD_printString("STK MOVE",  BS_SIDEBAR_X, 104u, COL_TEXT, 1u);
    LCD_printString("J  PLACE",  BS_SIDEBAR_X, 116u, COL_TEXT, 1u);
    LCD_printString("EXT ROT",   BS_SIDEBAR_X, 128u, COL_TEXT, 1u);
    LCD_printString("HOLD EXT",  BS_SIDEBAR_X, 152u, COL_USED, 1u);
    LCD_printString("= MENU",    BS_SIDEBAR_X, 164u, COL_USED, 1u);

    LCD_printString(valid ? "OK TO PLACE" : "BLOCKED - MOVE",
                    BS_GRID_X, 200u, valid ? COL_OK : COL_USED, 1u);
    LCD_Refresh(&cfg0);
}

static void bs_render_handoff_screen(uint8_t next_player) {
    LCD_Fill_Buffer(COL_BG);
    bs_print_center("SCREEN HIDDEN", 50u, COL_TEXT, 2u);
    if (next_player == 0u) {
        bs_print_center("PASS TO PLAYER 1", 96u, COL_P1, 2u);
    } else {
        bs_print_center("PASS TO PLAYER 2", 96u, COL_P2, 2u);
    }
    bs_print_center(next_player == 0u ? "P1 PRESS J1 BUTTON"
                                      : "P2 PRESS J2 BUTTON",
                    150u, COL_LABEL, 1u);
    bs_print_center("HOLD EXT TO QUIT", 180u, COL_USED, 1u);
    LCD_Refresh(&cfg0);
}

static void bs_render_turn_screen(uint8_t player) {
    char buf[28];
    uint8_t opp = (uint8_t)(1u - player);
    uint8_t fleet_view = bs_show_fleet[player];

    LCD_Fill_Buffer(COL_BG);
    bs_draw_top_bar(player);

    if (fleet_view) {
        LCD_printString("MY FLEET", BS_GRID_X, 12u, COL_TEXT, 1u);
        bs_draw_grid(BS_GRID_X, BS_GRID_Y, player, 1u, 0u, 0u, 0u, COL_TEXT);
    } else {
        bs_draw_grid(BS_GRID_X, BS_GRID_Y, opp, 0u,
                     bs_cursor_x[player], bs_cursor_y[player], 1u, COL_TEXT);
    }

    bs_draw_sidebar(player);

    (void)snprintf(buf, sizeof(buf), "AIM %c%u",
                   (char)('A' + bs_cursor_x[player]),
                   (unsigned)bs_cursor_y[player] + 1u);
    LCD_printString(buf, BS_GRID_X, 196u, COL_LABEL, 1u);
    if (bs_player_last_shot[player][0] != '\0') {
        (void)snprintf(buf, sizeof(buf), "LAST %s", bs_player_last_shot[player]);
        LCD_printString(buf, 80u, 196u, COL_TEXT, 1u);
    } else if (bs_status[0] != '\0') {
        LCD_printString(bs_status, 80u, 196u, COL_TEXT, 1u);
    }

    LCD_printString(fleet_view ? "EXT BACK TO AIM"
                               : "J TAP FIRE / HOLD NUKE",
                    BS_GRID_X, 212u, COL_TEXT, 1u);
    LCD_printString(fleet_view ? "HOLD EXT MENU" : "EXT FLEET / HOLD MENU",
                    BS_GRID_X, 224u, COL_TEXT, 1u);

    if (bs_ability_charging[player]) {
        uint32_t held = HAL_GetTick() - bs_ability_start_tick[player];
        uint8_t pct = (held >= BS_NUKE_CHARGE_MS) ? 100u
                      : (uint8_t)((held * 100u) / BS_NUKE_CHARGE_MS);
        LCD_Draw_Rect(BS_SIDEBAR_X + 50u, 196u, 12u, 40u, COL_SUNK, 0u);
        LCD_Draw_Rect(BS_SIDEBAR_X + 51u,
                      (uint16_t)(235u - (pct * 38u) / 100u),
                      10u, (uint16_t)((pct * 38u) / 100u), COL_NUKE, 1u);
    }

    LCD_Refresh(&cfg0);
}

static void bs_render_result_screen(uint8_t player) {
    uint8_t col = COL_TEXT;
    if      (strstr(bs_last_shot, "SUNK")) col = COL_OK;
    else if (strstr(bs_last_shot, "HIT"))  col = COL_HIT;
    else if (strstr(bs_last_shot, "MISS")) col = COL_MISS;

    LCD_Fill_Buffer(COL_BG);
    bs_draw_top_bar(player);
    bs_print_center(bs_last_shot, 90u, col, 3u);
    bs_print_center("HANDING OFF...", 160u, COL_SUNK, 1u);
    LCD_Refresh(&cfg0);
}

static void bs_render_game_over(uint8_t winner) {
    char buf[28];
    uint8_t p;

    LCD_Fill_Buffer(COL_BG);
    LCD_Draw_Rect(0u, 0u, BS_LCD_W, 30u, bs_player_colour(winner), 1u);
    (void)snprintf(buf, sizeof(buf), "PLAYER %d WINS", (int)winner + 1);
    bs_print_center(buf, 8u, COL_TEXT, 2u);

    for (p = 0u; p < BS_NUM_PLAYERS; p++) {
        uint16_t y = (uint16_t)(48u + p * 70u);
        uint16_t acc = (bs_stats[p].shots_fired == 0u)
                       ? 0u
                       : (uint16_t)((bs_stats[p].hits * 100u) / bs_stats[p].shots_fired);
        (void)snprintf(buf, sizeof(buf), "P%d", (int)p + 1);
        LCD_printString(buf, 12u, y, bs_player_colour(p), 2u);
        (void)snprintf(buf, sizeof(buf), "SHOTS %u  HITS %u",
                       (unsigned)bs_stats[p].shots_fired, (unsigned)bs_stats[p].hits);
        LCD_printString(buf, 50u, y, COL_TEXT, 1u);
        (void)snprintf(buf, sizeof(buf), "ACC %u%%  STREAK %u",
                       (unsigned)acc, (unsigned)bs_stats[p].best_streak);
        LCD_printString(buf, 50u, (uint16_t)(y + 14u), COL_TEXT, 1u);
        LCD_printString(bs_stats[p].ability_used ? "NUKE USED" : "NUKE SAVED",
                        50u, (uint16_t)(y + 28u), COL_SUNK, 1u);
    }

    bs_print_center("J REMATCH", 198u, COL_OK, 1u);
    bs_print_center("EXT MENU", 216u, COL_LABEL, 1u);
    LCD_Refresh(&cfg0);
}

static void bs_render_exit_confirm(void) {
    LCD_Fill_Buffer(COL_BG);
    bs_print_center("EXIT GAME?", 58u, COL_USED, 3u);
    bs_print_center("PROGRESS LOST", 108u, COL_HIT, 2u);
    bs_print_center("J = YES", 156u, COL_OK, 1u);
    bs_print_center("EXT = NO", 178u, COL_LABEL, 1u);
    LCD_Refresh(&cfg0);
}

static void bs_handle_setup(uint8_t player, BS_Input *input) {
    bs_apply_setup_move(input);

    if (input->ext_tap) {
        bs_setup_horizontal ^= 1u;
        bs_sfx_move();
    }

    if (input->fire_tap) {
        if (bs_can_place(player, bs_setup_ship_index, bs_setup_x, bs_setup_y, bs_setup_horizontal)) {
            bs_place_ship(player, bs_setup_ship_index, bs_setup_x, bs_setup_y, bs_setup_horizontal);
            bs_sfx_hit();
            bs_setup_ship_index++;
            bs_setup_x = 0u; bs_setup_y = 0u;
            bs_setup_horizontal = 1u;
            if (bs_all_ships_placed(player)) {
                bs_set_state(player == 0u ? BS_STATE_HANDOFF_TO_P2_SETUP
                                          : BS_STATE_HANDOFF_TO_P1_START);
            }
        } else {
            bs_sfx_invalid();
        }
    }
}

static void bs_update_charge_feedback(uint8_t player) {
    uint32_t now = HAL_GetTick();
    uint32_t held = now - bs_ability_start_tick[player];
    uint8_t pct = (held >= BS_NUKE_CHARGE_MS) ? 100u
                  : (uint8_t)((held * 100u) / BS_NUKE_CHARGE_MS);
    uint16_t freq = (uint16_t)(300u + (pct * 700u) / 100u);
    bs_led_set(pct);
    if (now - bs_last_charge_sfx_tick[player] >= 150u) {
        bs_last_charge_sfx_tick[player] = now;
        bs_sfx_charge(freq);
    }
}

// Nuke fires the full row and column through the cursor.
static uint8_t bs_fire_nuke(uint8_t player) {
    uint8_t i;
    uint8_t best = 0u;
    uint8_t cx = bs_cursor_x[player];
    uint8_t cy = bs_cursor_y[player];
    char tmp[32];

    for (i = 0u; i < BS_GRID_SIZE; i++) {
        uint8_t code = bs_fire_cell(player, i, cy, tmp, sizeof(tmp));
        if (code > best) best = code;
    }
    for (i = 0u; i < BS_GRID_SIZE; i++) {
        if (i == cy) continue;
        uint8_t code = bs_fire_cell(player, cx, i, tmp, sizeof(tmp));
        if (code > best) best = code;
    }
    if (best == 0u) {
        bs_copy_text(bs_last_shot, sizeof(bs_last_shot), "NUKE: NO TARGET");
        bs_copy_text(bs_player_last_shot[player], sizeof(bs_player_last_shot[player]), bs_last_shot);
    } else {
        bs_record_last_shot(player, cx, cy, best);
    }
    return best;
}

static void bs_handle_turn(uint8_t player, BS_Input *input) {
    uint32_t now = HAL_GetTick();
    uint8_t code;

    if (input->ext_tap && !bs_ability_charging[player]) {
        bs_show_fleet[player] ^= 1u;
        bs_sfx_move();
        return;
    }

    if (bs_show_fleet[player]) return;

    bs_apply_cursor_move(player, input->dx, input->dy);

    if (input->nuke_start && bs_stats[player].ability_used) {
        bs_copy_text(bs_status, sizeof(bs_status), "NUKE ALREADY USED");
        bs_sfx_invalid();
        return;
    }

    if (input->nuke_start && !bs_ability_charging[player]) {
        bs_ability_charging[player] = 1u;
        bs_ability_start_tick[player] = now - BS_NUKE_HOLD_MS;
        bs_last_charge_sfx_tick[player] = 0u;
        bs_copy_text(bs_status, sizeof(bs_status), "NUKE CHARGING");
    }
    if (bs_ability_charging[player]) {
        bs_update_charge_feedback(player);
        if (input->nuke_release) {
            bs_ability_charging[player] = 0u;
            bs_sfx_off(); bs_led_off();
            if (bs_stats[player].ability_used) {
                bs_copy_text(bs_last_shot, sizeof(bs_last_shot), "NUKE USED");
            } else {
                bs_stats[player].ability_used = 1u;
                code = bs_fire_nuke(player);
                bs_sfx_tone(500u, 60u, 35u);
                bs_sfx_tone(800u, 60u, 35u);
                bs_sfx_tone(1100u, 80u, 35u);
                bs_after_shot_result(player, code);
                return;
            }
        }
        return;
    }

    if (input->fire_tap) {
        char msg[32];
        code = bs_fire_cell(player, bs_cursor_x[player], bs_cursor_y[player],
                            msg, sizeof(msg));
        if (code == 0u) {
            bs_copy_text(bs_status, sizeof(bs_status), msg);
            bs_sfx_invalid();
        } else {
            bs_record_last_shot(player, bs_cursor_x[player], bs_cursor_y[player], code);
            bs_after_shot_result(player, code);
        }
    }
}

static uint8_t bs_handoff_acked(BS_Input *input) {
    return (HAL_GetTick() - bs_state_tick > BS_HANDOFF_MS) && input->fire_tap;
}

static void bs_run_state_logic(BS_Input *p1, BS_Input *p2) {
    switch (bs_state) {
        case BS_STATE_SETUP_P1:
            bs_handle_setup(0u, p1);
            break;
        case BS_STATE_HANDOFF_TO_P2_SETUP:
            if (bs_handoff_acked(p2)) bs_set_state(BS_STATE_SETUP_P2);
            break;
        case BS_STATE_SETUP_P2:
            bs_handle_setup(1u, p2);
            break;
        case BS_STATE_HANDOFF_TO_P1_START:
            if (bs_handoff_acked(p1)) bs_set_state(BS_STATE_TURN_P1);
            break;
        case BS_STATE_TURN_P1:
            bs_handle_turn(0u, p1);
            break;
        case BS_STATE_RESULT_P1:
            if (HAL_GetTick() - bs_state_tick >= BS_RESULT_MS) {
                bs_set_state(BS_STATE_HANDOFF_TO_P2_TURN);
            }
            break;
        case BS_STATE_HANDOFF_TO_P2_TURN:
            if (bs_handoff_acked(p2)) bs_set_state(BS_STATE_TURN_P2);
            break;
        case BS_STATE_TURN_P2:
            bs_handle_turn(1u, p2);
            break;
        case BS_STATE_RESULT_P2:
            if (HAL_GetTick() - bs_state_tick >= BS_RESULT_MS) {
                bs_set_state(BS_STATE_HANDOFF_TO_P1_TURN);
            }
            break;
        case BS_STATE_HANDOFF_TO_P1_TURN:
            if (bs_handoff_acked(p1)) bs_set_state(BS_STATE_TURN_P1);
            break;
        case BS_STATE_EXIT_CONFIRM:
            if (p1->fire_tap || p2->fire_tap) {
                bs_quit_requested = 1u;
            } else if (p1->ext_tap || p2->ext_tap) {
                bs_set_state(bs_exit_return_state);
            }
            break;
        case BS_STATE_GAME_OVER:
            if (p1->fire_tap || p2->fire_tap) {
                bs_start_new_match();
            } else if (p1->ext_tap || p2->ext_tap || p1->back_request || p2->back_request) {
                bs_quit_requested = 1u;
            }
            break;
        default:
            break;
    }
}

static void bs_render_current_state(void) {
    switch (bs_state) {
        case BS_STATE_SETUP_P1:               bs_render_setup_screen(0u);   break;
        case BS_STATE_HANDOFF_TO_P2_SETUP:    bs_render_handoff_screen(1u); break;
        case BS_STATE_SETUP_P2:               bs_render_setup_screen(1u);   break;
        case BS_STATE_HANDOFF_TO_P1_START:    bs_render_handoff_screen(0u); break;
        case BS_STATE_TURN_P1:                bs_render_turn_screen(0u);    break;
        case BS_STATE_RESULT_P1:              bs_render_result_screen(0u);  break;
        case BS_STATE_HANDOFF_TO_P2_TURN:     bs_render_handoff_screen(1u); break;
        case BS_STATE_TURN_P2:                bs_render_turn_screen(1u);    break;
        case BS_STATE_RESULT_P2:              bs_render_result_screen(1u);  break;
        case BS_STATE_HANDOFF_TO_P1_TURN:     bs_render_handoff_screen(0u); break;
        case BS_STATE_EXIT_CONFIRM:           bs_render_exit_confirm();     break;
        case BS_STATE_GAME_OVER:              bs_render_game_over(bs_winner); break;
        default: break;
    }
}

MenuState Game3_Run(void) {
    BS_Input p1_in, p2_in;
    uint8_t  return_home = 0u;

    if (!joystick2_ready) {
        Joystick_Init(&joystick2_cfg);
        Joystick_Calibrate(&joystick2_cfg);
        joystick2_ready = 1u;
    }

    bs_start_new_match();

    while (!return_home) {
        uint32_t frame_start = HAL_GetTick();

        Input_Read();
        bs_read_input(0u, &p1_in);
        bs_read_input(1u, &p2_in);

        if (p1_in.ext_tap || p2_in.ext_tap) {
            p1_in.ext_tap = 1u;
            p2_in.ext_tap = 1u;
        }
        if (p1_in.back_request || p2_in.back_request) {
            p1_in.back_request = 1u;
            p2_in.back_request = 1u;
        }

        if ((p1_in.back_request || p2_in.back_request) &&
            bs_state != BS_STATE_EXIT_CONFIRM &&
            bs_state != BS_STATE_GAME_OVER) {
            bs_exit_return_state = bs_state;
            memset(bs_ability_charging, 0, sizeof(bs_ability_charging));
            bs_led_off();
            bs_sfx_off();
            bs_set_state(BS_STATE_EXIT_CONFIRM);
        }

        bs_run_state_logic(&p1_in, &p2_in);

        if (bs_quit_requested) {
            return_home = 1u;
        }

        bs_render_current_state();

        {
            uint32_t dt = HAL_GetTick() - frame_start;
            if (dt < BS_FRAME_MS) HAL_Delay(BS_FRAME_MS - dt);
        }
    }

    bs_last_score_p1 = bs_match_score[0];
    bs_last_score_p2 = bs_match_score[1];
    bs_led_off();
    bs_sfx_off();
    return MENU_STATE_HOME;
}
