#include "Menu.h"
#include "LCD.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;  // LCD configuration from main.c
extern Joystick_cfg_t joystick_cfg;  // Joystick configuration
extern Joystick_t joystick_data;     // Current joystick readings

// Menu options
static const char* menu_options[] = {
    "Chess",
    "Rock-It Ralph",
    "Tron"
};
#define NUM_MENU_OPTIONS 3

// Frame rate for menu (in milliseconds)
#define MENU_FRAME_TIME_MS 30  // ~33 FPS

/**
 * @brief Render the home menu screen
 */
static void render_home_menu(MenuSystem* menu) {
    LCD_Fill_Buffer(0);
    
    // Title
    LCD_printString("MAIN MENU", 50, 10, 1, 3);
    
    // Menu options with selection highlight
    for (int i = 0; i < NUM_MENU_OPTIONS; i++) {
        uint16_t y_pos = 70 + (i * 40);
        uint8_t text_size = 2;
        
        if (i == menu->selected_option) {
            // Highlight selected option with inverted colors
            // Draw a rectangle around selected option
            // We'll use simple marker instead
            LCD_printString(">", 40, y_pos, 1, text_size);  // Arrow pointing to selection
        }
        
        LCD_printString((char*)menu_options[i], 70, y_pos, 1, text_size);
    }
    
    // Instructions
    LCD_printString("Press joystick to select", 14, 230, 1, 1);
    
    LCD_Refresh(&cfg0);
}

// ==============================================
// PUBLIC API IMPLEMENTATION
// ==============================================

void Menu_Init(MenuSystem* menu) {
    menu->selected_option = 0;
}

MenuState Menu_Run(MenuSystem* menu) {
    static Direction last_direction = CENTRE;  // Track last direction for debouncing
    MenuState selected_game = MENU_STATE_HOME;  // Which game was selected
    
    // Menu's own loop - runs until game is selected
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // Read input
        Input_Read();
        
        // Read current joystick position
        Joystick_Read(&joystick_cfg, &joystick_data);
        
        // Handle joystick navigation (up/down to select option)
        Direction current_direction = joystick_data.direction;
        
        if (current_direction == S && last_direction != S) {  // Joystick pushed DOWN
            // Move selection down
            menu->selected_option++;
            if (menu->selected_option >= NUM_MENU_OPTIONS) {
                menu->selected_option = 0;  // Wrap around
            }
        } 
        else if (current_direction == N && last_direction != N) {  // Joystick pushed UP
            // Move selection up
            if (menu->selected_option == 0) {
                menu->selected_option = NUM_MENU_OPTIONS - 1;  // Wrap around
            } else {
                menu->selected_option--;
            }
        }
        
        last_direction = current_direction;
        
        // Handle button press to select current option
        if (current_input.btn3_pressed) {
            // User pressed button - select the highlighted option
            if (menu->selected_option == 0) {
                selected_game = MENU_STATE_GAME_1;
            } else if (menu->selected_option == 1) {
                selected_game = MENU_STATE_GAME_2;
            } else if (menu->selected_option == 2) {
                selected_game = MENU_STATE_GAME_3;
            }
            break;  // Exit menu loop - game selected!
        }
        
        // Render menu
        render_home_menu(menu);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < MENU_FRAME_TIME_MS) {
            HAL_Delay(MENU_FRAME_TIME_MS - frame_time);
        }
    }
    
    return selected_game;  // Return which game was selected
}
