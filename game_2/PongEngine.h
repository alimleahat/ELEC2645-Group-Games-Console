/**
 * @file PongEngine.h
 * @brief Rock-It Ralph 
 */

#ifndef PONGENGINE_H
#define PONGENGINE_H

#include <stdint.h>
#include "Joystick.h"

#define PONG_MAX_ROCKS 18
#define PONG_MAX_ENEMIES 5
#define PONG_MAX_PICKUPS 4
#define PONG_MAX_PLAYER_BULLETS 6
#define PONG_MAX_ENEMY_BULLETS 8

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t hp;
    uint8_t active;
} RockBarrier_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t hp;
    int8_t speed;
    uint8_t active;
    uint8_t flying;
    uint8_t shot_timer;
} Enemy_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t type;
    uint8_t active;
} Pickup_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int8_t vx;
    int8_t vy;
    uint8_t active;
} Bullet_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t active;
    uint8_t hp;
    uint8_t max_hp;
    int8_t vx;
    uint8_t shot_timer;
} Boss_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t health;
    uint8_t stamina;
    uint8_t facing_right;
    uint8_t hurt_timer;
    uint8_t attack_timer;
    uint8_t charge_frames;
    uint8_t prev_action_pressed;
    uint8_t on_ground;
    int8_t vy;
    uint8_t weapon_type;
    uint8_t weapon_timer;
    uint8_t shot_cooldown;
    uint8_t dash_timer;
    uint8_t dash_cooldown;
} Ralph_t;

typedef struct {
    Ralph_t player;
    RockBarrier_t rocks[PONG_MAX_ROCKS];
    Enemy_t enemies[PONG_MAX_ENEMIES];
    Pickup_t pickups[PONG_MAX_PICKUPS];
    Bullet_t player_bullets[PONG_MAX_PLAYER_BULLETS];
    Bullet_t enemy_bullets[PONG_MAX_ENEMY_BULLETS];
    Boss_t boss;

    uint16_t score;
    uint8_t level;
    uint8_t game_over;

    uint16_t frame_counter;
    uint8_t remaining_rocks;
    uint8_t last_event;
    uint8_t last_action;

    int16_t door_x;
    int16_t door_y;
    int16_t door_w;
    int16_t door_h;
    uint8_t door_open;

    uint8_t shake_timer;
    uint8_t shake_strength;
    uint8_t boss_intro_timer;
} PongEngine_t;

void PongEngine_Init(PongEngine_t* engine,
                     int16_t paddle_x,
                     int16_t paddle_y,
                     int16_t paddle_width,
                     int16_t paddle_height,
                     int16_t ball_size,
                     float ball_speed);

uint8_t PongEngine_Update(PongEngine_t* engine, UserInput input, uint8_t action_pressed);
void PongEngine_Draw(PongEngine_t* engine);

uint8_t PongEngine_GetLives(PongEngine_t* engine);
uint16_t PongEngine_GetScore(PongEngine_t* engine);
uint16_t PongEngine_GetOpponentScore(PongEngine_t* engine);

uint8_t PongEngine_GetBottomPoints(PongEngine_t* engine);
uint8_t PongEngine_GetTopPoints(PongEngine_t* engine);
uint8_t PongEngine_GetBottomGames(PongEngine_t* engine);
uint8_t PongEngine_GetTopGames(PongEngine_t* engine);

const char* PongEngine_GetBottomPointText(PongEngine_t* engine);
const char* PongEngine_GetTopPointText(PongEngine_t* engine);
const char* PongEngine_GetLastShotText(PongEngine_t* engine);
const char* PongEngine_GetLastEventText(PongEngine_t* engine);

#endif
