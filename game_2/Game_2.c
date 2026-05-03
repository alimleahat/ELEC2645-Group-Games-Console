/**
 * @file Game_2.c
 * @brief Rock-It Ralph — Game 2 wrapper for the ELEC2645 menu system
 *
 * This file integrates Ahmad Alhamadi's Rock-It Ralph game into the
 * shared menu template structure.
 *
 * Controls:
 *   Joystick LEFT/RIGHT : Move Ralph
 *   Joystick UP         : Jump
 *   Joystick DOWN       : Dash
 *   BT2 (tap)           : Punch
 *   BT2 (hold + release): Power smash (requires full stamina)
 *   BT3                 : Return to main menu
 */

#include "Game_2.h"
#include "PongEngine.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>

/* ===== Externs provided by main.c ===== */
extern ST7789V2_cfg_t    cfg0;
extern Buzzer_cfg_t      buzzer_cfg;
extern Joystick_cfg_t    joystick_cfg;
extern Joystick_t        joystick_data;

/* ===== Module-level state ===== */
static PongEngine_t pong_engine;

/* Frame rate (match Ahmad's original: 60 FPS) */
#define GAME2_FPS            60
#define GAME2_FRAME_TIME_MS  (1000 / GAME2_FPS)

/* HUD colour index (palette colour 1 = white) */
#define HUD_COLOUR 1

/**
 * @brief Read BT2 as a held/continuous state.
 *
 * PongEngine uses action_pressed every frame (for charge attacks),
 * so we poll the GPIO directly rather than using the edge-triggered
 * InputHandler flag.
 * BTN2 is active-low (GPIO_PIN_RESET = pressed).
 */
static uint8_t game2_read_action(void)
{
    return (HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

/**
 * @brief Rock-It Ralph game entry point.
 *
 * Called by the menu system when the player selects Game 2.
 * Owns its own loop; returns MENU_STATE_HOME when BT3 is pressed
 * or the game ends.
 *
 * @return MenuState  Always MENU_STATE_HOME to return to the menu.
 */
MenuState Game2_Run(void)
{
    /* ------------------------------------------------------------------
     * Initialise
     * ------------------------------------------------------------------ */
    LCD_Set_Palette(PALETTE_DEFAULT);

    /* Splash screen */
    LCD_Fill_Buffer(0);
    LCD_printString("ROCK-IT", 48, 60,  HUD_COLOUR, 4);
    LCD_printString("RALPH",   66, 100, HUD_COLOUR, 4);
    LCD_printString("Joy = Menu", 60, 170, HUD_COLOUR, 2);
    LCD_Refresh(&cfg0);
    HAL_Delay(800);

    /* Initialise engine — parameters are kept from Ahmad's original main.c */
    PongEngine_Init(&pong_engine,
                    10,    /* paddle_x   (unused internally) */
                    100,   /* paddle_y   (unused internally) */
                    4,     /* paddle_w   (unused internally) */
                    40,    /* paddle_h   (unused internally) */
                    6,     /* ball_size  (unused internally) */
                    8.0f); /* ball_speed (unused internally) */

    /* ------------------------------------------------------------------
     * Game loop
     * ------------------------------------------------------------------ */
    uint8_t alive = 1;

    while (alive)
    {
        uint32_t frame_start = HAL_GetTick();

        /* --- Input --- */
        Input_Read();   /* updates current_input (btn2/btn3 edge flags) */

        /* BT3 → return to menu */
        if (current_input.btn3_pressed)
        {
            break;
        }

        /* Joystick for movement */
        Joystick_Read(&joystick_cfg, &joystick_data);
        UserInput input = Joystick_GetInput(&joystick_data);

        /* BT2 polled directly for held/charge behaviour */
        uint8_t action_pressed = game2_read_action();

        /* --- Update --- */
        uint8_t health = PongEngine_Update(&pong_engine, input, action_pressed);
        if (health == 0)
        {
            alive = 0; /* game over — exit loop, show game over screen */
        }

        /* --- Render --- */
        {
            char info[32];

            LCD_Fill_Buffer(0);

            PongEngine_Draw(&pong_engine);

            /* HUD — top bar */
            sprintf(info, "SCR %d", PongEngine_GetScore(&pong_engine));
            LCD_printString(info, 6,   8, HUD_COLOUR, 1);

            sprintf(info, "LV %d",  PongEngine_GetBottomGames(&pong_engine));
            LCD_printString(info, 84,  8, HUD_COLOUR, 1);

            sprintf(info, "HP %d",  PongEngine_GetBottomPoints(&pong_engine));
            LCD_printString(info, 142, 8, HUD_COLOUR, 1);

            sprintf(info, "ST %d",  PongEngine_GetTopPoints(&pong_engine));
            LCD_printString(info, 194, 8, HUD_COLOUR, 1);

            /* HUD — bottom status */
            sprintf(info, "%s", PongEngine_GetLastShotText(&pong_engine));
            LCD_printString(info, 88, 212, HUD_COLOUR, 1);

            sprintf(info, "%s", PongEngine_GetLastEventText(&pong_engine));
            LCD_printString(info, 70, 224, HUD_COLOUR, 1);

            LCD_Refresh(&cfg0);
        }

        /* --- Frame cap --- */
        uint32_t elapsed = HAL_GetTick() - frame_start;
        if (elapsed < GAME2_FRAME_TIME_MS)
        {
            HAL_Delay(GAME2_FRAME_TIME_MS - elapsed);
        }
    }

    /* ------------------------------------------------------------------
     * Game-over screen (scrolling, matches Ahmad's original style)
     * ------------------------------------------------------------------ */
    if (!alive)
    {
        int16_t offset = 0;
        uint32_t game_over_start = HAL_GetTick();

        while ((HAL_GetTick() - game_over_start) < 5000)   /* show for 5 s */
        {
            Input_Read();
            if (current_input.btn3_pressed || current_input.btn2_pressed)
            {
                break;   /* early exit */
            }

            LCD_Fill_Buffer(0);

            LCD_printString("Game Over!",  20, 0  + offset, HUD_COLOUR, 3);

            {
                char score_str[32];
                sprintf(score_str, "Score %d", PongEngine_GetScore(&pong_engine));
                LCD_printString(score_str, 12, 28 + offset, HUD_COLOUR, 2);
            }

            LCD_printString("BT2 or Joy",  28, 80 + offset, HUD_COLOUR, 2);
            LCD_printString("to continue", 24, 98 + offset, HUD_COLOUR, 2);

            LCD_Refresh(&cfg0);
            HAL_Delay(500);

            offset += 10;
            if (offset > 120) offset = 0;
        }
    }

    return MENU_STATE_HOME;
}
