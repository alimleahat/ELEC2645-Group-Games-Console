#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// ==============================================
// MENU STATE MACHINE
// ==============================================

typedef enum {
    MENU_STATE_HOME = 0,        // Main menu - select game
    MENU_STATE_GAME_1,          // Running Game 1
    MENU_STATE_GAME_2,          // Running Game 2
    MENU_STATE_GAME_3,          // Running Game 3
} MenuState;

// Menu system structure
typedef struct {
    uint8_t selected_option;    // Which menu option is highlighted (0-2)
} MenuSystem;

// ==============================================
// INITIALIZATION AND STATE MANAGEMENT
// ==============================================

/**
 * @brief Initialize the menu system
 */
void Menu_Init(MenuSystem* menu);

/**
 * @brief Run the menu - displays menu and waits for selection
 * 
 * Runs its own loop and returns the selected game state.
 * 
 * @return MenuState - The game that was selected (GAME_1, GAME_2, or GAME_3)
 */
MenuState Menu_Run(MenuSystem* menu);

// Sound gate consulted by games before playing tones. Returns 1 = on.
uint8_t Menu_SoundEnabled(void);

#endif // MENU_H
