#ifndef GAME_3_H
#define GAME_3_H

#include "Menu.h"

/**
 * @brief Run Game 3: Tron Light Cycles.
 *
 * The menu system calls this when Game 3 is selected. The game owns its loop
 * and returns MENU_STATE_HOME when the player exits or the match ends.
 */
MenuState Game3_Run(void);

#endif // GAME_3_H
