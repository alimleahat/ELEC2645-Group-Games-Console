#include "Menu.h"
#include "LCD.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

#define MENU_FRAME_TIME_MS 30

#define COL_BG          1   // white
#define COL_TITLE       9   // navy
#define COL_TITLE_SHAD 10   // gold
#define COL_NAME_TEXT   9   // navy
#define COL_HINT_TEXT   0   // black
#define COL_ARROW       9   // navy
#define COL_DOT_ON      9   // navy
#define COL_DOT_OFF    13   // grey
#define COL_TILE_SHAD  13   // grey

#define TILE_X      30
#define TILE_Y      46
#define TILE_W     180
#define TILE_H     140
#define TILE_R      14

#define SPRITE_DIM  32
#define EXPL_DIM    16

#define CHAR_W_S1    6
#define CHAR_W_S2   12
#define CHAR_W_S3   18

// Sprite encoding: X = filled, _ = transparent.
// Multi-colour sprites use the LCD palette index directly (see explosion).
#define _ 255,
#define X   1,

static const uint8_t sprite_king[SPRITE_DIM * SPRITE_DIM] = {
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ X X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X _ _ _ _ _ _ _
_ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X X _ _ _ _ _ _ _
_ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
};

static const uint8_t sprite_ralph[SPRITE_DIM * SPRITE_DIM] = {
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X X X X _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ X X X X X X X X X X X X X X _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ X X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
_ _ _ X X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
_ _ X X X X X X X X X X X X X X X X X X X X X X X X X X X X _ _
_ X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _
_ X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _
_ X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _
_ X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _
_ _ X X X X X X X X X X X X X X X X X X X X X X X X X X X X _ _
_ _ _ X X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X X X _ _ _ _
_ _ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
_ _ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
_ _ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
_ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ X X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ X X X X X X _ _ _ _ _ _ _ _ X X X X X X _ _ _ _ _ _
_ _ _ _ _ X X X X X X X X _ _ _ _ _ _ X X X X X X X X _ _ _ _ _
_ _ _ _ X X X X X X X X X X _ _ _ _ X X X X X X X X X X _ _ _ _
_ _ _ _ X X X X X X X X X X _ _ _ _ X X X X X X X X X X _ _ _ _
_ _ _ X X X X X X X X X X X X _ _ X X X X X X X X X X X X _ _ _
_ _ _ X X X X X X X X X X X X _ _ X X X X X X X X X X X X _ _ _
};

static const uint8_t sprite_destroyer[SPRITE_DIM * SPRITE_DIM] = {
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ X X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ X X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ X X X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ X X _ _ _ X X X X X X X X X _ _ X X _ _ _ _ _ _ _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ X X X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _
X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _ _
X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _
X X X X X X X X X X X X X X X X X X X X X X X X X X X X X X _ _
_ X X X X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _
_ _ X X X X X X X X X X X X X X X X X X X X X X X X X X _ _ _ _
_ _ _ _ X X X X X X X X X X X X X X X X X X X X X X _ _ _ _ _ _
_ _ _ _ _ _ _ _ X X X X X X X X X X X X X X X X _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ X X X X X X X X _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
};

#undef X

// Explosion palette: O = orange, R = red, Y = yellow.
#define O 5,
#define R 2,
#define Y 6,

static const uint8_t sprite_explosion[EXPL_DIM * EXPL_DIM] = {
_ _ _ _ _ _ O _ _ O _ _ _ _ _ _
_ O _ _ _ O O O O O O _ _ _ O _
_ _ O _ O O O R R O O O _ O _ _
_ _ _ O O R R R R R R O O _ _ _
_ _ O O R R Y Y Y Y R R O O _ _
_ O O R R Y Y Y Y Y Y R R O O _
O O R R Y Y Y R R Y Y Y R R O O
O O R R Y Y R R R R Y Y R R O O
O O R R Y Y R R R R Y Y R R O O
O O R R Y Y Y R R Y Y Y R R O O
_ O O R R Y Y Y Y Y Y R R O O _
_ _ O O R R Y Y Y Y R R O O _ _
_ _ _ O O R R R R R R O O _ _ _
_ _ O _ O O O R R O O O _ O _ _
_ O _ _ _ O O O O O O _ _ _ O _
_ _ _ _ _ _ O _ _ O _ _ _ _ _ _
};

#undef O
#undef R
#undef Y
#undef _

typedef void (*IconDrawFn)(int cx, int cy);

static void draw_icon_chess(int cx, int cy);
static void draw_icon_ralph(int cx, int cy);
static void draw_icon_ship(int cx, int cy);

typedef struct {
    const char* name;
    IconDrawFn  draw_icon;
    uint8_t     tile_bg;
    MenuState   state;
} GameTile;

static const GameTile games[] = {
    { "Chess",         draw_icon_chess,  7, MENU_STATE_GAME_1 }, // pink
    { "Rock-It Ralph", draw_icon_ralph,  6, MENU_STATE_GAME_2 }, // yellow
    { "Battleship",    draw_icon_ship,  14, MENU_STATE_GAME_3 }, // cyan
};
#define NUM_MENU_OPTIONS ((int)(sizeof(games) / sizeof(games[0])))

// Filled rect with rounded corners (mask each corner, then redraw with a circle).
static void draw_rounded_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              uint16_t r, uint8_t color, uint8_t bg_color) {
    LCD_Draw_Rect(x, y, w, h, color, 1);
    LCD_Draw_Rect(x,         y,         r, r, bg_color, 1);
    LCD_Draw_Rect(x + w - r, y,         r, r, bg_color, 1);
    LCD_Draw_Rect(x,         y + h - r, r, r, bg_color, 1);
    LCD_Draw_Rect(x + w - r, y + h - r, r, r, bg_color, 1);
    LCD_Draw_Circle(x + r,         y + r,         r, color, 1);
    LCD_Draw_Circle(x + w - r - 1, y + r,         r, color, 1);
    LCD_Draw_Circle(x + r,         y + h - r - 1, r, color, 1);
    LCD_Draw_Circle(x + w - r - 1, y + h - r - 1, r, color, 1);
}

// Drop shadow + thickened main text for a chunky bubbly look.
static void draw_bubbly_text(const char* s, int x, int y,
                             uint8_t main_col, uint8_t shadow_col, uint8_t scale) {
    LCD_printString((char*)s, x + 2, y + 2, shadow_col, scale);
    LCD_printString((char*)s, x + 1, y,     main_col,   scale);
    LCD_printString((char*)s, x,     y,     main_col,   scale);
}

// Mini chessboard with a gold king on top.
static void draw_icon_chess(int cx, int cy) {
    const int cell = 24;
    const int half = 2 * cell;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            uint8_t col = ((r + c) & 1) ? 1 : 9; // white / navy
            LCD_Draw_Rect(cx - half + c * cell, cy - half + r * cell,
                          cell, cell, col, 1);
        }
    }
    LCD_Draw_Sprite_Colour_Scaled(cx - 48, cy - 48,
                                  SPRITE_DIM, SPRITE_DIM,
                                  sprite_king, 10, 3);
}

// Red Hulk silhouette.
static void draw_icon_ralph(int cx, int cy) {
    LCD_Draw_Sprite_Colour_Scaled(cx - 64, cy - 64,
                                  SPRITE_DIM, SPRITE_DIM,
                                  sprite_ralph, 2, 4);
}

// Navy destroyer with an explosion above the deck.
static void draw_icon_ship(int cx, int cy) {
    LCD_Draw_Sprite_Colour_Scaled(cx - 64, cy - 64,
                                  SPRITE_DIM, SPRITE_DIM,
                                  sprite_destroyer, 9, 4);
    LCD_Draw_Sprite_Scaled(cx + 14, cy - 56,
                           EXPL_DIM, EXPL_DIM,
                           sprite_explosion, 3);
}

// Draw one frame of the menu for the currently-selected game.
static void render_home_menu(MenuSystem* menu) {
    LCD_Fill_Buffer(COL_BG);

    const char* title = "GAMES CONSOLE";
    int title_x = (240 - (int)strlen(title) * CHAR_W_S3) / 2;
    draw_bubbly_text(title, title_x, 10, COL_TITLE, COL_TITLE_SHAD, 3);

    const GameTile* g = &games[menu->selected_option];

    draw_rounded_rect(TILE_X + 3, TILE_Y + 3, TILE_W, TILE_H,
                      TILE_R, COL_TILE_SHAD, COL_BG);
    draw_rounded_rect(TILE_X, TILE_Y, TILE_W, TILE_H,
                      TILE_R, g->tile_bg, COL_BG);

    g->draw_icon(TILE_X + TILE_W / 2, TILE_Y + TILE_H / 2);

    LCD_printString("<", 8,                   TILE_Y + TILE_H / 2 - 10, COL_ARROW, 3);
    LCD_printString(">", 240 - 8 - CHAR_W_S3, TILE_Y + TILE_H / 2 - 10, COL_ARROW, 3);

    int name_x = (240 - (int)strlen(g->name) * CHAR_W_S2) / 2;
    LCD_printString((char*)g->name, name_x, 195, COL_NAME_TEXT, 2);

    int dot_spacing = 12;
    int dot_x0 = (240 - (NUM_MENU_OPTIONS - 1) * dot_spacing) / 2;
    for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
        uint8_t c = (i == menu->selected_option) ? COL_DOT_ON : COL_DOT_OFF;
        LCD_Draw_Circle(dot_x0 + i * dot_spacing, 218, 3, c, 1);
    }

    const char* hint = "Press BT2 to play";
    int hint_x = (240 - (int)strlen(hint) * CHAR_W_S1) / 2;
    LCD_printString((char*)hint, hint_x, 230, COL_HINT_TEXT, 1);

    LCD_Refresh(&cfg0);
}

void Menu_Init(MenuSystem* menu) {
    menu->selected_option = 0;
}

uint8_t Menu_SoundEnabled(void) {
    return 1;
}

// Run the menu loop until the user selects a game with BT2.
MenuState Menu_Run(MenuSystem* menu) {
    static Direction last_direction = CENTRE;
    MenuState selected_game = MENU_STATE_HOME;

    while (1) {
        uint32_t frame_start = HAL_GetTick();

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        Direction current_direction = joystick_data.direction;

        if (current_direction == E && last_direction != E) {
            menu->selected_option = (menu->selected_option + 1) % NUM_MENU_OPTIONS;
        } else if (current_direction == W && last_direction != W) {
            menu->selected_option = (menu->selected_option == 0)
                                  ? (NUM_MENU_OPTIONS - 1)
                                  : (menu->selected_option - 1);
        }

        last_direction = current_direction;

        if (current_input.btn2_pressed) {
            selected_game = games[menu->selected_option].state;
            break;
        }

        render_home_menu(menu);

        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < MENU_FRAME_TIME_MS) {
            HAL_Delay(MENU_FRAME_TIME_MS - frame_time);
        }
    }

    return selected_game;
}
