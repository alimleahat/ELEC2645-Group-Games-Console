/* ============================================================
 * Game_1.h  -  Two-Player Chess
 * ELEC2645 Embedded Systems Project - Unit 4
 * Platform : STM32 Nucleo-L476RG
 * ============================================================ */

#ifndef GAME_1_H
#define GAME_1_H

#include "Menu.h"

/**
 * @brief  Run the two-player chess game.
 *
 * Called by the main state machine when the user selects Game 1
 * from the menu.  The function contains its own game loop and
 * returns MENU_STATE_HOME when the player presses BT3.
 *
 * Controls:
 *   Joystick        - move cursor one square at a time
 *   BT2             - select piece / confirm move
 *   BT3 (1st press) - cancel current selection
 *   BT3 (2nd press) - return to main menu
 *
 * @return MenuState  Next state for the main state machine.
 */
MenuState Game1_Run(void);

#endif /* GAME_1_H */
