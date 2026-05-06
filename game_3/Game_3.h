#ifndef GAME_3_H
#define GAME_3_H

/*
 * Game_3.h - Battleship: Hot-Seat Naval Combat
 *
 * Public API kept compatible with the existing menu. Internals live in
 * Game_3.c only so the upgrade stays inside the Game 3 boundary.
 */

#include "Menu.h"
#include <stdint.h>

MenuState Game3_Run(void);

/* Compatibility setters retained for Menu.c. Battleship ignores these because
 * the current version is always two-player hot-seat. */
void    Game3_SetMode(uint8_t two_player);
void    Game3_SetWrap(uint8_t on);

uint8_t Game3_LastScoreP1(void);
uint8_t Game3_LastScoreP2(void);

#endif /* GAME_3_H */
