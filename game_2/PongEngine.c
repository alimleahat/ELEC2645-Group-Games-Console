/**
 * @file PongEngine.c
 * @brief Rock-It Ralph 
 */

#include "PongEngine.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include "Utils.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define WALKWAY_Y 178
#define WALKWAY_H 26
#define FLOOR_Y (WALKWAY_Y + WALKWAY_H)

#define PLAYER_START_X 12
#define PLAYER_W 24
#define PLAYER_H 26
#define PLAYER_GROUND_Y (WALKWAY_Y - PLAYER_H)
#define PLAYER_SPEED 4
#define PLAYER_JUMP_VY -9
#define PLAYER_GRAVITY 1
#define PLAYER_MAX_HEALTH 5
#define PLAYER_MAX_STAMINA 6
#define PLAYER_DASH_SPEED 9
#define PLAYER_DASH_TIME 5
#define PLAYER_DASH_COOLDOWN 18

#define ROCK_W 16
#define ROCK_H 16
#define STACK_COLS 6
#define STACK_START_X 64
#define STACK_GAP_X 20

#define ENEMY_W 16
#define ENEMY_H 16
#define ENEMY_GROUND_OFFSET 5
#define FLYING_Y 112

#define PICKUP_W 10
#define PICKUP_H 10

#define PLAYER_BULLET_W 10
#define PLAYER_BULLET_H 4
#define ENEMY_BULLET_W 8
#define ENEMY_BULLET_H 4

#define WEAPON_NONE 0
#define WEAPON_BLASTER 1

#define PICKUP_BLASTER 1
#define PICKUP_HEART 2
#define PICKUP_STAMINA 3

#define ACTION_NONE 0
#define ACTION_MOVE 1
#define ACTION_SMASH 2
#define ACTION_POWER 3
#define ACTION_JUMP 4
#define ACTION_SHOOT 5
#define ACTION_PICKUP 6
#define ACTION_DASH 7

#define EVENT_READY 0
#define EVENT_HIT 1
#define EVENT_BREAK 2
#define EVENT_BLOCKED 3
#define EVENT_LEVEL 4
#define EVENT_MISS 5
#define EVENT_HURT 6
#define EVENT_ENEMY 7
#define EVENT_POWER 8
#define EVENT_BULLET 9
#define EVENT_JUMP 10
#define EVENT_PICKUP 11
#define EVENT_ARROW 12
#define EVENT_DASH 13

#define COLOR_BLACK 0
#define COLOR_WHITE 1
#define COLOR_RED 2
#define COLOR_GREEN 3
#define COLOR_BLUE 4
#define COLOR_YELLOW 6
#define COLOR_PURPLE 8
#define COLOR_NAVY 9
#define COLOR_BROWN 12
#define COLOR_CYAN 14
#define COLOR_MAGENTA 15

#define BUZZER_VOLUME 50
#define BUZZER_JUMP_FREQ 700
#define BUZZER_HIT_FREQ 820
#define BUZZER_BREAK_FREQ 1250
#define BUZZER_LEVEL_FREQ 1750
#define BUZZER_BLOCK_FREQ 350
#define BUZZER_POWER_FREQ 1900
#define BUZZER_HURT_FREQ 250
#define BUZZER_ENEMY_FREQ 900
#define BUZZER_PICKUP_FREQ 1450
#define BUZZER_SHOOT_FREQ 1180
#define BUZZER_ARROW_FREQ 980
#define BUZZER_DASH_FREQ 420
#define BUZZER_BOSS_FREQ 260

#define BUZZER_SHORT_MS 40
#define BUZZER_MID_MS 65
#define BUZZER_LONG_MS 120
#define BUZZER_POWER_MS 180

#define CHARGE_POWER_FRAMES 24
#define BOSS_LEVEL 5
#define BOSS_HP 12
#define BOSS_W 42
#define BOSS_H 34
#define BOSS_Y 88

extern Buzzer_cfg_t buzzer_cfg;

static uint32_t buzzer_stop_tick = 0;
static uint32_t buzzer_stage2_tick = 0;
static uint32_t buzzer_stage2_freq = 0;
static uint8_t level_intro_timer = 0;
static uint8_t game_won = 0;
static int8_t boss_vy = 1;

static uint8_t BgColor(uint8_t level)
{
    if (level == 1) return COLOR_BLUE;
    if (level == 2) return COLOR_YELLOW;
    if (level == 3) return COLOR_NAVY;
    if (level == 4) return COLOR_PURPLE;
    return COLOR_BLACK;
}

static uint8_t FloorColor(uint8_t level)
{
    if (level == 1) return COLOR_BROWN;
    if (level == 2) return COLOR_RED;
    if (level == 3) return COLOR_BLACK;
    if (level == 4) return COLOR_MAGENTA;
    return COLOR_NAVY;
}

static uint8_t RockColor(uint8_t level, uint8_t hp)
{
    if (hp >= 2) return COLOR_YELLOW;
    if (level == 1) return COLOR_RED;
    if (level == 2) return COLOR_BROWN;
    if (level == 3) return COLOR_CYAN;
    if (level == 4) return COLOR_MAGENTA;
    return COLOR_RED;
}

static int8_t ClampAimVy(int16_t from_y, int16_t target_y)
{
    int16_t diff = target_y - from_y;

    if (diff < -18) return -2;
    if (diff < -6) return -1;
    if (diff > 18) return 2;
    if (diff > 6) return 1;
    return 0;
}

static uint8_t JumpPressed(UserInput input)
{
    return (input.magnitude >= 0.90f &&
            (input.direction == N || input.direction == NE || input.direction == NW));
}

static void PongEngine_Beep(uint32_t freq_hz, uint32_t duration_ms)
{
    buzzer_tone(&buzzer_cfg, freq_hz, BUZZER_VOLUME);
    buzzer_stop_tick = HAL_GetTick() + duration_ms;
    buzzer_stage2_tick = 0;
    buzzer_stage2_freq = 0;
}

static void PongEngine_PlaySmashSound(void)
{
    buzzer_tone(&buzzer_cfg, 190, BUZZER_VOLUME);
    buzzer_stage2_tick = HAL_GetTick() + 30;
    buzzer_stage2_freq = 620;
    buzzer_stop_tick = buzzer_stage2_tick + 45;
}

static void PongEngine_UpdateBuzzer(void)
{
    uint32_t now = HAL_GetTick();

    if (buzzer_stage2_tick != 0 && (int32_t)(now - buzzer_stage2_tick) >= 0) {
        buzzer_tone(&buzzer_cfg, buzzer_stage2_freq, BUZZER_VOLUME);
        buzzer_stage2_tick = 0;
    }

    if (buzzer_stop_tick != 0 && (int32_t)(now - buzzer_stop_tick) >= 0) {
        buzzer_off(&buzzer_cfg);
        buzzer_stop_tick = 0;
        buzzer_stage2_tick = 0;
        buzzer_stage2_freq = 0;
    }
}

static uint8_t PongEngine_AABB(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                               int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
    return (ax < bx + bw &&
            ax + aw > bx &&
            ay < by + bh &&
            ay + ah > by);
}

static void StartShake(PongEngine_t* engine, uint8_t strength, uint8_t duration)
{
    engine->shake_strength = strength;
    engine->shake_timer = duration;
}

static int16_t ShakeX(PongEngine_t* engine)
{
    if (engine->shake_timer == 0) return 0;
    return (engine->frame_counter & 1) ? engine->shake_strength : -engine->shake_strength;
}

static int16_t ShakeY(PongEngine_t* engine)
{
    if (engine->shake_timer == 0) return 0;
    return (engine->frame_counter & 2) ? 1 : -1;
}

static RockBarrier_t* RockAt(PongEngine_t* engine, int16_t x, int16_t y)
{
    int i;

    for (i = 0; i < PONG_MAX_ROCKS; i++) {
        RockBarrier_t* rock = &engine->rocks[i];

        if (rock->active &&
            PongEngine_AABB(x, y, engine->player.width, engine->player.height,
                            rock->x, rock->y, rock->width, rock->height)) {
            return rock;
        }
    }

    return 0;
}

static Enemy_t* GroundEnemyAt(PongEngine_t* engine, int16_t x, int16_t y)
{
    int i;

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (enemy->active && !enemy->flying &&
            PongEngine_AABB(x, y, engine->player.width, engine->player.height,
                            enemy->x, enemy->y, enemy->width, enemy->height)) {
            return enemy;
        }
    }

    return 0;
}

static uint8_t BlockedAt(PongEngine_t* engine, int16_t x, int16_t y)
{
    return RockAt(engine, x, y) != 0 || GroundEnemyAt(engine, x, y) != 0;
}

static void MovePlayerX(PongEngine_t* engine, int16_t dx)
{
    int16_t i;
    int16_t dir;
    int16_t distance;

    if (dx == 0) return;

    dir = (dx > 0) ? 1 : -1;
    distance = (dx > 0) ? dx : -dx;

    for (i = 0; i < distance; i++) {
        int16_t next_x = engine->player.x + dir;

        if (next_x < 8) {
            engine->player.x = 8;
            return;
        }

        if (next_x + engine->player.width > SCREEN_WIDTH - 8) {
            engine->player.x = SCREEN_WIDTH - 8 - engine->player.width;
            return;
        }

        if (BlockedAt(engine, next_x, engine->player.y)) {
            engine->last_event = EVENT_BLOCKED;
            return;
        }

        engine->player.x = next_x;
    }
}

static void MovePlayerY(PongEngine_t* engine, int16_t dy)
{
    int16_t i;
    int16_t dir;
    int16_t distance;

    if (dy == 0) return;

    dir = (dy > 0) ? 1 : -1;
    distance = (dy > 0) ? dy : -dy;

    for (i = 0; i < distance; i++) {
        int16_t next_y = engine->player.y + dir;

        if (next_y >= PLAYER_GROUND_Y) {
            engine->player.y = PLAYER_GROUND_Y;
            engine->player.vy = 0;
            engine->player.on_ground = 1;
            return;
        }

        if (BlockedAt(engine, engine->player.x, next_y)) {
            engine->last_event = EVENT_BLOCKED;

            if (dir > 0) {
                engine->player.vy = 0;
                engine->player.on_ground = 1;
            } else {
                engine->player.vy = 1;
            }

            return;
        }

        engine->player.y = next_y;
    }
}

static void UnstickPlayer(PongEngine_t* engine)
{
    int i;

    if (!BlockedAt(engine, engine->player.x, engine->player.y)) return;

    for (i = 1; i < 28; i++) {
        if (engine->player.x - i >= 8 &&
            !BlockedAt(engine, engine->player.x - i, engine->player.y)) {
            engine->player.x -= i;
            return;
        }

        if (engine->player.x + i + engine->player.width <= SCREEN_WIDTH - 8 &&
            !BlockedAt(engine, engine->player.x + i, engine->player.y)) {
            engine->player.x += i;
            return;
        }

        if (engine->player.y - i > 20 &&
            !BlockedAt(engine, engine->player.x, engine->player.y - i)) {
            engine->player.y -= i;
            engine->player.vy = 0;
            engine->player.on_ground = 0;
            return;
        }
    }

    engine->player.x = PLAYER_START_X;
    engine->player.y = PLAYER_GROUND_Y;
    engine->player.vy = 0;
    engine->player.on_ground = 1;
}

static void ClearPickups(PongEngine_t* engine)
{
    int i;

    for (i = 0; i < PONG_MAX_PICKUPS; i++) {
        engine->pickups[i].active = 0;
        engine->pickups[i].type = 0;
        engine->pickups[i].width = PICKUP_W;
        engine->pickups[i].height = PICKUP_H;
    }
}

static void ClearBullets(PongEngine_t* engine)
{
    int i;

    for (i = 0; i < PONG_MAX_PLAYER_BULLETS; i++) {
        engine->player_bullets[i].active = 0;
        engine->player_bullets[i].width = PLAYER_BULLET_W;
        engine->player_bullets[i].height = PLAYER_BULLET_H;
    }

    for (i = 0; i < PONG_MAX_ENEMY_BULLETS; i++) {
        engine->enemy_bullets[i].active = 0;
        engine->enemy_bullets[i].width = ENEMY_BULLET_W;
        engine->enemy_bullets[i].height = ENEMY_BULLET_H;
    }
}

static void ClearEnemies(PongEngine_t* engine)
{
    int i;

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        engine->enemies[i].active = 0;
        engine->enemies[i].width = ENEMY_W;
        engine->enemies[i].height = ENEMY_H;
        engine->enemies[i].hp = 1;
        engine->enemies[i].speed = -2;
        engine->enemies[i].flying = 0;
        engine->enemies[i].shot_timer = 0;
    }
}

static void ClearBoss(PongEngine_t* engine)
{
    engine->boss.active = 0;
    engine->boss.width = BOSS_W;
    engine->boss.height = BOSS_H;
    engine->boss.hp = 0;
    engine->boss.max_hp = BOSS_HP;
    engine->boss.vx = -2;
    engine->boss.shot_timer = 0;
    boss_vy = 1;
}

static void ResetPlayerForLevel(PongEngine_t* engine)
{
    engine->player.x = PLAYER_START_X;
    engine->player.y = PLAYER_GROUND_Y;
    engine->player.width = PLAYER_W;
    engine->player.height = PLAYER_H;
    engine->player.facing_right = 1;
    engine->player.hurt_timer = 0;
    engine->player.attack_timer = 0;
    engine->player.charge_frames = 0;
    engine->player.prev_action_pressed = 0;
    engine->player.on_ground = 1;
    engine->player.vy = 0;
    engine->player.weapon_type = WEAPON_NONE;
    engine->player.weapon_timer = 0;
    engine->player.shot_cooldown = 0;
    engine->player.dash_timer = 0;
    engine->player.dash_cooldown = 0;
}

static void SetDoor(PongEngine_t* engine)
{
    engine->door_w = 18;
    engine->door_h = 30;
    engine->door_x = SCREEN_WIDTH - 28;
    engine->door_y = WALKWAY_Y - 12;
    engine->door_open = (engine->remaining_rocks == 0 && !engine->boss.active) ? 1 : 0;
}

static void SetupBossLevel(PongEngine_t* engine)
{
    engine->remaining_rocks = 0;
    engine->boss.active = 1;
    engine->boss.x = SCREEN_WIDTH - 80;
    engine->boss.y = BOSS_Y;
    engine->boss.width = BOSS_W;
    engine->boss.height = BOSS_H;
    engine->boss.hp = BOSS_HP;
    engine->boss.max_hp = BOSS_HP;
    engine->boss.vx = -2;
    boss_vy = 1;
    engine->boss.shot_timer = 28;
    engine->boss_intro_timer = 90;
    engine->door_open = 0;
    engine->last_event = EVENT_ENEMY;

    engine->pickups[0].active = 1;
    engine->pickups[0].type = PICKUP_BLASTER;
    engine->pickups[0].x = 46;
    engine->pickups[0].y = WALKWAY_Y - PICKUP_H - 4;
    engine->pickups[0].width = PICKUP_W;
    engine->pickups[0].height = PICKUP_H;

    PongEngine_Beep(BUZZER_BOSS_FREQ, BUZZER_LONG_MS);
}

static void LoadLevel(PongEngine_t* engine)
{
    int i;
    int rock_index = 0;
    int columns = 3 + engine->level;

    if (columns > STACK_COLS) columns = STACK_COLS;

    for (i = 0; i < PONG_MAX_ROCKS; i++) {
        engine->rocks[i].active = 0;
        engine->rocks[i].width = ROCK_W;
        engine->rocks[i].height = ROCK_H;
        engine->rocks[i].hp = 0;
    }

    engine->remaining_rocks = 0;
    engine->boss_intro_timer = 0;
    level_intro_timer = 20;   /* shorter level intro (~0.33s @ 60 FPS) */

    ClearEnemies(engine);
    ClearPickups(engine);
    ClearBullets(engine);
    ClearBoss(engine);
    ResetPlayerForLevel(engine);

    if (engine->level == BOSS_LEVEL) {
        SetupBossLevel(engine);
    } else {
        for (i = 0; i < columns; i++) {
            int layer;
            int stack_height = 1 + ((engine->level + i) % 3);

            for (layer = 0; layer < stack_height; layer++) {
                RockBarrier_t* rock;

                if (rock_index >= PONG_MAX_ROCKS) break;

                rock = &engine->rocks[rock_index];
                rock->x = STACK_START_X + (i * STACK_GAP_X);
                rock->y = WALKWAY_Y - ROCK_H - (layer * (ROCK_H - 2));
                rock->width = ROCK_W;
                rock->height = ROCK_H;
                rock->hp = 1 + ((engine->level + layer + i) % 2);

                if (engine->level >= 4 && layer == 0) {
                    rock->hp++;
                }

                rock->active = 1;
                engine->remaining_rocks++;
                rock_index++;
            }
        }
    }

    engine->last_event = EVENT_READY;
    engine->last_action = ACTION_NONE;
    engine->frame_counter = 0;
    SetDoor(engine);
}

static void DamagePlayer(PongEngine_t* engine)
{
    if (engine->player.hurt_timer != 0 ||
        engine->player.dash_timer > 0 ||
        engine->boss_intro_timer > 0 ||
        level_intro_timer > 0) {
        return;
    }

    if (engine->player.health > 0) engine->player.health--;

    engine->player.hurt_timer = 25;
    engine->last_event = EVENT_HURT;
    PongEngine_Beep(BUZZER_HURT_FREQ, BUZZER_LONG_MS);

    if (engine->player.health == 0) {
        engine->game_over = 1;
    }
}

static void RewardStamina(PongEngine_t* engine)
{
    if (engine->player.stamina < PLAYER_MAX_STAMINA) {
        engine->player.stamina++;
    }
}

static void SpawnPickup(PongEngine_t* engine, int16_t x, int16_t y)
{
    int i;
    uint16_t roll = Random_U16(100);
    uint8_t type = 0;

    if (roll < 18) type = PICKUP_BLASTER;
    else if (roll < 30) type = PICKUP_HEART;
    else if (roll < 46) type = PICKUP_STAMINA;
    else return;

    for (i = 0; i < PONG_MAX_PICKUPS; i++) {
        Pickup_t* pickup = &engine->pickups[i];

        if (!pickup->active) {
            pickup->active = 1;
            pickup->type = type;
            pickup->x = x;
            pickup->y = y;
            pickup->width = PICKUP_W;
            pickup->height = PICKUP_H;
            return;
        }
    }
}

static void DestroyRock(PongEngine_t* engine, RockBarrier_t* rock)
{
    uint8_t was_last_rock;

    if (!rock->active) return;

    was_last_rock = (engine->remaining_rocks == 1) ? 1 : 0;
    rock->active = 0;

    if (engine->remaining_rocks > 0) engine->remaining_rocks--;

    engine->score += 25;
    engine->last_event = EVENT_BREAK;
    PongEngine_Beep(BUZZER_BREAK_FREQ, BUZZER_LONG_MS);

    if (was_last_rock) StartShake(engine, 3, 10);

    SpawnPickup(engine, rock->x + 3, rock->y + 3);
    engine->door_open = (engine->remaining_rocks == 0 && !engine->boss.active) ? 1 : 0;
}

static void DestroyEnemy(PongEngine_t* engine, Enemy_t* enemy)
{
    if (!enemy->active) return;

    enemy->active = 0;
    engine->score += 30;
    engine->last_event = EVENT_BREAK;
    PongEngine_Beep(BUZZER_BREAK_FREQ, BUZZER_LONG_MS);
}

static void DamageBoss(PongEngine_t* engine, uint8_t damage)
{
    if (!engine->boss.active) return;

    if (engine->boss.hp > damage) {
        engine->boss.hp -= damage;
        engine->last_event = EVENT_HIT;
        PongEngine_Beep(BUZZER_HIT_FREQ, BUZZER_SHORT_MS);
    } else {
        engine->boss.hp = 0;
        engine->boss.active = 0;
        engine->score += 250;
        engine->last_event = EVENT_BREAK;
        engine->door_open = 1;
        StartShake(engine, 4, 14);
        PongEngine_Beep(BUZZER_BREAK_FREQ, BUZZER_LONG_MS);
    }
}

static RockBarrier_t* FindPunchRock(PongEngine_t* engine)
{
    int i;
    RockBarrier_t* best = 0;
    int16_t hit_w = 18;
    int16_t hit_h = engine->player.height + 6;
    int16_t hit_y = engine->player.y - 2;
    int16_t hit_x = engine->player.facing_right ?
                    engine->player.x + engine->player.width :
                    engine->player.x - hit_w;

    for (i = 0; i < PONG_MAX_ROCKS; i++) {
        RockBarrier_t* rock = &engine->rocks[i];

        if (!rock->active) continue;

        if (!PongEngine_AABB(hit_x, hit_y, hit_w, hit_h,
                             rock->x, rock->y, rock->width, rock->height)) {
            continue;
        }

        if (best == 0) best = rock;
        else if (engine->player.facing_right && rock->x < best->x) best = rock;
        else if (!engine->player.facing_right && rock->x > best->x) best = rock;
    }

    return best;
}

static Enemy_t* FindPunchEnemy(PongEngine_t* engine)
{
    int i;
    Enemy_t* best = 0;
    int16_t hit_w = 18;
    int16_t hit_h = engine->player.height + 6;
    int16_t hit_y = engine->player.y - 2;
    int16_t hit_x = engine->player.facing_right ?
                    engine->player.x + engine->player.width :
                    engine->player.x - hit_w;

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (!enemy->active) continue;

        if (!PongEngine_AABB(hit_x, hit_y, hit_w, hit_h,
                             enemy->x, enemy->y, enemy->width, enemy->height)) {
            continue;
        }

        if (best == 0) best = enemy;
        else if (engine->player.facing_right && enemy->x < best->x) best = enemy;
        else if (!engine->player.facing_right && enemy->x > best->x) best = enemy;
    }

    return best;
}

static uint8_t BossPunchable(PongEngine_t* engine)
{
    int16_t hit_w = 18;
    int16_t hit_h = engine->player.height + 6;
    int16_t hit_y = engine->player.y - 2;
    int16_t hit_x;

    if (!engine->boss.active) return 0;

    hit_x = engine->player.facing_right ?
            engine->player.x + engine->player.width :
            engine->player.x - hit_w;

    return PongEngine_AABB(hit_x, hit_y, hit_w, hit_h,
                           engine->boss.x, engine->boss.y,
                           engine->boss.width, engine->boss.height);
}

static void DoPunch(PongEngine_t* engine)
{
    Enemy_t* enemy;
    RockBarrier_t* rock;

    engine->player.attack_timer = 8;
    engine->last_action = ACTION_SMASH;

    if (BossPunchable(engine)) {
        PongEngine_PlaySmashSound();
        DamageBoss(engine, 1);
        RewardStamina(engine);
        engine->score += 20;
        return;
    }

    enemy = FindPunchEnemy(engine);
    if (enemy != 0) {
        PongEngine_PlaySmashSound();
        RewardStamina(engine);
        engine->score += 15;

        if (enemy->hp > 1) enemy->hp--;
        else DestroyEnemy(engine, enemy);

        return;
    }

    rock = FindPunchRock(engine);
    if (rock != 0) {
        PongEngine_PlaySmashSound();
        RewardStamina(engine);
        engine->score += 10;

        if (rock->hp > 1) rock->hp--;
        else DestroyRock(engine, rock);

        UnstickPlayer(engine);
        return;
    }

    engine->last_event = EVENT_MISS;
    PongEngine_Beep(BUZZER_BLOCK_FREQ, BUZZER_SHORT_MS);
}

static void DoPowerSmash(PongEngine_t* engine)
{
    int i;

    engine->player.stamina = 0;
    engine->player.attack_timer = 12;
    engine->last_action = ACTION_POWER;
    engine->last_event = EVENT_POWER;
    PongEngine_Beep(BUZZER_POWER_FREQ, BUZZER_POWER_MS);

    if (engine->boss.active) {
        if (engine->player.facing_right &&
            engine->boss.x >= engine->player.x &&
            engine->boss.x <= engine->player.x + 100) {
            DamageBoss(engine, 3);
        }

        if (!engine->player.facing_right &&
            engine->boss.x <= engine->player.x &&
            engine->boss.x >= engine->player.x - 100) {
            DamageBoss(engine, 3);
        }
    }

    for (i = 0; i < PONG_MAX_ROCKS; i++) {
        RockBarrier_t* rock = &engine->rocks[i];

        if (!rock->active) continue;

        if (engine->player.facing_right &&
            rock->x >= engine->player.x &&
            rock->x <= engine->player.x + 80) {
            DestroyRock(engine, rock);
        }

        if (!engine->player.facing_right &&
            rock->x <= engine->player.x &&
            rock->x >= engine->player.x - 80) {
            DestroyRock(engine, rock);
        }
    }

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (!enemy->active) continue;

        if (engine->player.facing_right &&
            enemy->x >= engine->player.x &&
            enemy->x <= engine->player.x + 80) {
            DestroyEnemy(engine, enemy);
        }

        if (!engine->player.facing_right &&
            enemy->x <= engine->player.x &&
            enemy->x >= engine->player.x - 80) {
            DestroyEnemy(engine, enemy);
        }
    }

    UnstickPlayer(engine);
}

static void FirePlayerBullet(PongEngine_t* engine)
{
    int i;

    if (engine->player.weapon_type != WEAPON_BLASTER ||
        engine->player.shot_cooldown > 0) {
        return;
    }

    for (i = 0; i < PONG_MAX_PLAYER_BULLETS; i++) {
        Bullet_t* bullet = &engine->player_bullets[i];

        if (!bullet->active) {
            bullet->active = 1;
            bullet->width = PLAYER_BULLET_W;
            bullet->height = PLAYER_BULLET_H;
            bullet->y = engine->player.y + 11;
            bullet->vy = 0;

            if (engine->player.facing_right) {
                bullet->x = engine->player.x + engine->player.width;
                bullet->vx = 7;
            } else {
                bullet->x = engine->player.x - PLAYER_BULLET_W;
                bullet->vx = -7;
            }

            engine->player.shot_cooldown = 10;
            engine->player.attack_timer = 5;
            engine->last_action = ACTION_SHOOT;
            engine->last_event = EVENT_BULLET;
            PongEngine_Beep(BUZZER_SHOOT_FREQ, BUZZER_SHORT_MS);
            return;
        }
    }
}

static void HandleAction(PongEngine_t* engine, uint8_t action_pressed)
{
    if (engine->boss_intro_timer > 0 || level_intro_timer > 0) {
        engine->player.prev_action_pressed = action_pressed;
        return;
    }

    if (action_pressed) {
        if (engine->player.weapon_type == WEAPON_BLASTER) {
            if (!engine->player.prev_action_pressed) FirePlayerBullet(engine);
        } else if (engine->player.charge_frames < 40) {
            engine->player.charge_frames++;
        }
    } else {
        if (engine->player.prev_action_pressed) {
            if (engine->player.weapon_type != WEAPON_BLASTER &&
                engine->player.charge_frames >= CHARGE_POWER_FRAMES &&
                engine->player.stamina >= PLAYER_MAX_STAMINA) {
                DoPowerSmash(engine);
            } else if (engine->player.weapon_type != WEAPON_BLASTER) {
                DoPunch(engine);
            }
        }

        engine->player.charge_frames = 0;
    }

    engine->player.prev_action_pressed = action_pressed;
}

static void SpawnEnemyBullet(PongEngine_t* engine, int16_t x, int16_t y)
{
    int i;
    int16_t target_y = engine->player.y + (engine->player.height / 2);

    for (i = 0; i < PONG_MAX_ENEMY_BULLETS; i++) {
        Bullet_t* bullet = &engine->enemy_bullets[i];

        if (!bullet->active) {
            bullet->active = 1;
            bullet->x = x;
            bullet->y = y;
            bullet->width = ENEMY_BULLET_W;
            bullet->height = ENEMY_BULLET_H;
            bullet->vx = -4;
            bullet->vy = ClampAimVy(y, target_y);
            engine->last_event = EVENT_ARROW;
            PongEngine_Beep(BUZZER_ARROW_FREQ, BUZZER_SHORT_MS);
            return;
        }
    }
}

static void UpdateBoss(PongEngine_t* engine)
{
    if (!engine->boss.active || engine->boss_intro_timer > 0 || level_intro_timer > 0) return;

    engine->boss.x += engine->boss.vx;
    engine->boss.y += boss_vy;

    if (engine->boss.x < 118) {
        engine->boss.x = 118;
        engine->boss.vx = (engine->boss.hp < 6) ? 3 : 2;
    }

    if (engine->boss.x + engine->boss.width > SCREEN_WIDTH - 18) {
        engine->boss.x = SCREEN_WIDTH - 18 - engine->boss.width;
        engine->boss.vx = (engine->boss.hp < 6) ? -3 : -2;
    }

    if (engine->boss.y < 60) {
        engine->boss.y = 60;
        boss_vy = (engine->boss.hp < 6) ? 2 : 1;
    }

    if (engine->boss.y + engine->boss.height > 148) {
        engine->boss.y = 148 - engine->boss.height;
        boss_vy = (engine->boss.hp < 6) ? -2 : -1;
    }

    if (engine->boss.shot_timer > 0) {
        engine->boss.shot_timer--;
    } else {
        SpawnEnemyBullet(engine, engine->boss.x, engine->boss.y + 10);
        SpawnEnemyBullet(engine, engine->boss.x + 10, engine->boss.y + 18);
        SpawnEnemyBullet(engine, engine->boss.x + 4, engine->boss.y + 26);
        engine->boss.shot_timer = (engine->boss.hp < 6) ? 18 : 26;
    }

    if (PongEngine_AABB(engine->player.x, engine->player.y,
                        engine->player.width, engine->player.height,
                        engine->boss.x, engine->boss.y,
                        engine->boss.width, engine->boss.height)) {
        DamagePlayer(engine);
    }
}

static void UpdateEnemies(PongEngine_t* engine)
{
    int i;

    if (engine->boss_intro_timer > 0 || level_intro_timer > 0) return;

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (!enemy->active) continue;

        enemy->x += enemy->speed;

        if (!enemy->flying) {
            enemy->y = WALKWAY_Y - enemy->height - ENEMY_GROUND_OFFSET;
        }

        if (enemy->x < -enemy->width) {
            enemy->active = 0;
            continue;
        }

        if (enemy->shot_timer > 0) {
            enemy->shot_timer--;
        } else {
            SpawnEnemyBullet(engine, enemy->x, enemy->y + 7);
            enemy->shot_timer = 38 + Random_U16(30);
        }

        if (PongEngine_AABB(engine->player.x, engine->player.y,
                            engine->player.width, engine->player.height,
                            enemy->x, enemy->y,
                            enemy->width, enemy->height)) {
            DamagePlayer(engine);
        }
    }
}

static void UpdateEnemyBullets(PongEngine_t* engine)
{
    int i;

    for (i = 0; i < PONG_MAX_ENEMY_BULLETS; i++) {
        Bullet_t* bullet = &engine->enemy_bullets[i];

        if (!bullet->active) continue;

        bullet->x += bullet->vx;
        bullet->y += bullet->vy;

        if (bullet->x < -bullet->width ||
            bullet->x > SCREEN_WIDTH ||
            bullet->y < 0 ||
            bullet->y > SCREEN_HEIGHT) {
            bullet->active = 0;
            continue;
        }

        if (PongEngine_AABB(engine->player.x, engine->player.y,
                            engine->player.width, engine->player.height,
                            bullet->x, bullet->y,
                            bullet->width, bullet->height)) {
            bullet->active = 0;
            DamagePlayer(engine);
        }
    }
}

static void UpdatePlayerBullets(PongEngine_t* engine)
{
    int i;
    int j;

    for (i = 0; i < PONG_MAX_PLAYER_BULLETS; i++) {
        Bullet_t* bullet = &engine->player_bullets[i];

        if (!bullet->active) continue;

        bullet->x += bullet->vx;

        if (bullet->x < -bullet->width || bullet->x > SCREEN_WIDTH) {
            bullet->active = 0;
            continue;
        }

        if (engine->boss.active &&
            PongEngine_AABB(bullet->x, bullet->y, bullet->width, bullet->height,
                            engine->boss.x, engine->boss.y,
                            engine->boss.width, engine->boss.height)) {
            bullet->active = 0;
            DamageBoss(engine, 1);
            continue;
        }

        for (j = 0; j < PONG_MAX_ROCKS; j++) {
            RockBarrier_t* rock = &engine->rocks[j];

            if (!rock->active) continue;

            if (PongEngine_AABB(bullet->x, bullet->y, bullet->width, bullet->height,
                                rock->x, rock->y, rock->width, rock->height)) {
                bullet->active = 0;

                if (rock->hp > 1) {
                    rock->hp--;
                    PongEngine_Beep(BUZZER_HIT_FREQ, BUZZER_SHORT_MS);
                } else {
                    DestroyRock(engine, rock);
                }

                break;
            }
        }

        if (!bullet->active) continue;

        for (j = 0; j < PONG_MAX_ENEMIES; j++) {
            Enemy_t* enemy = &engine->enemies[j];

            if (!enemy->active) continue;

            if (PongEngine_AABB(bullet->x, bullet->y, bullet->width, bullet->height,
                                enemy->x, enemy->y, enemy->width, enemy->height)) {
                bullet->active = 0;

                if (enemy->hp > 1) {
                    enemy->hp--;
                    PongEngine_Beep(BUZZER_HIT_FREQ, BUZZER_SHORT_MS);
                } else {
                    DestroyEnemy(engine, enemy);
                }

                break;
            }
        }
    }
}

static void CollectPickups(PongEngine_t* engine)
{
    int i;

    for (i = 0; i < PONG_MAX_PICKUPS; i++) {
        Pickup_t* pickup = &engine->pickups[i];

        if (!pickup->active) continue;

        if (PongEngine_AABB(engine->player.x, engine->player.y,
                            engine->player.width, engine->player.height,
                            pickup->x, pickup->y,
                            pickup->width, pickup->height)) {
            pickup->active = 0;
            engine->last_action = ACTION_PICKUP;
            engine->last_event = EVENT_PICKUP;
            PongEngine_Beep(BUZZER_PICKUP_FREQ, BUZZER_SHORT_MS);

            if (pickup->type == PICKUP_BLASTER) {
                engine->player.weapon_type = WEAPON_BLASTER;

                if (engine->level == BOSS_LEVEL) {
                    engine->player.weapon_timer = 255;
                } else {
                    engine->player.weapon_timer = 240;
                }
            } else if (pickup->type == PICKUP_HEART) {
                if (engine->player.health < PLAYER_MAX_HEALTH) engine->player.health++;
            } else {
                engine->player.stamina = PLAYER_MAX_STAMINA;
            }
        }
    }
}

static void StartDash(PongEngine_t* engine, UserInput input)
{
    if (engine->boss_intro_timer > 0 || level_intro_timer > 0) return;
    if (engine->player.dash_timer > 0 || engine->player.dash_cooldown > 0) return;
    if (input.magnitude < 0.60f) return;
    if (input.direction != S && input.direction != SE && input.direction != SW) return;

    if (input.direction == SE) engine->player.facing_right = 1;
    else if (input.direction == SW) engine->player.facing_right = 0;

    engine->player.dash_timer = PLAYER_DASH_TIME;
    engine->player.dash_cooldown = PLAYER_DASH_COOLDOWN;
    engine->last_action = ACTION_DASH;
    engine->last_event = EVENT_DASH;
    PongEngine_Beep(BUZZER_DASH_FREQ, BUZZER_MID_MS);
}

static void UpdateDash(PongEngine_t* engine)
{
    int16_t dx;

    if (engine->player.dash_cooldown > 0) engine->player.dash_cooldown--;

    if (engine->player.dash_timer == 0) return;

    dx = engine->player.facing_right ? PLAYER_DASH_SPEED : -PLAYER_DASH_SPEED;
    MovePlayerX(engine, dx);
    engine->player.dash_timer--;
}

static void UpdatePlayer(PongEngine_t* engine, UserInput input)
{
    int16_t dx = 0;

    if (engine->boss_intro_timer > 0 || level_intro_timer > 0) {
        if (engine->player.hurt_timer > 0) engine->player.hurt_timer--;
        if (engine->player.dash_cooldown > 0) engine->player.dash_cooldown--;
        return;
    }

    if (BlockedAt(engine, engine->player.x, engine->player.y)) {
        UnstickPlayer(engine);
    }

    if (engine->player.dash_timer == 0 && input.magnitude >= 0.40f) {
        if (input.direction == W || input.direction == NW || input.direction == SW) {
            dx = -PLAYER_SPEED;
            engine->player.facing_right = 0;
            engine->last_action = ACTION_MOVE;
        } else if (input.direction == E || input.direction == NE || input.direction == SE) {
            dx = PLAYER_SPEED;
            engine->player.facing_right = 1;
            engine->last_action = ACTION_MOVE;
        }
    }

    MovePlayerX(engine, dx);

    if (engine->player.on_ground && JumpPressed(input)) {
        engine->player.vy = PLAYER_JUMP_VY;
        engine->player.on_ground = 0;
        engine->last_action = ACTION_JUMP;
        engine->last_event = EVENT_JUMP;
        PongEngine_Beep(BUZZER_JUMP_FREQ, BUZZER_SHORT_MS);
    }

    if (!engine->player.on_ground) {
        MovePlayerY(engine, engine->player.vy);
        engine->player.vy += PLAYER_GRAVITY;
    } else {
        if (engine->player.y < PLAYER_GROUND_Y &&
            !BlockedAt(engine, engine->player.x, engine->player.y + 1)) {
            engine->player.on_ground = 0;
            engine->player.vy = 1;
        }
    }

    if (engine->player.hurt_timer > 0) engine->player.hurt_timer--;
    if (engine->player.attack_timer > 0) engine->player.attack_timer--;
    if (engine->player.shot_cooldown > 0) engine->player.shot_cooldown--;

    if (engine->player.weapon_timer > 0) {
        engine->player.weapon_timer--;
        if (engine->player.weapon_timer == 0) engine->player.weapon_type = WEAPON_NONE;
    }
}

static void SpawnEnemy(PongEngine_t* engine)
{
    int i;

    if (engine->level == BOSS_LEVEL) return;

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (!enemy->active) {
            enemy->active = 1;
            enemy->width = ENEMY_W;
            enemy->height = ENEMY_H;
            enemy->flying = (Random_U16(100) < (20 + (engine->level * 9))) ? 1 : 0;
            enemy->x = SCREEN_WIDTH - 36;

            if (enemy->flying) {
                enemy->y = FLYING_Y + (Random_U16(2) ? 0 : 14);
                enemy->hp = 1;
                enemy->speed = -2;
            } else {
                enemy->y = WALKWAY_Y - enemy->height - ENEMY_GROUND_OFFSET;
                enemy->hp = 1 + (engine->level >= 4 ? 1 : 0);
                enemy->speed = -2 - (engine->level / 3);
            }

            enemy->shot_timer = 28 + Random_U16(35);
            engine->last_event = EVENT_ENEMY;
            PongEngine_Beep(BUZZER_ENEMY_FREQ, BUZZER_SHORT_MS);
            return;
        }
    }
}

static void CheckDoor(PongEngine_t* engine)
{
    if (!engine->door_open) return;

    if (PongEngine_AABB(engine->player.x, engine->player.y,
                        engine->player.width, engine->player.height,
                        engine->door_x, engine->door_y,
                        engine->door_w, engine->door_h)) {
        engine->score += 100;

        if (engine->level == BOSS_LEVEL) {
            game_won = 1;
            PongEngine_Beep(BUZZER_LEVEL_FREQ, BUZZER_LONG_MS);
            return;
        }

        engine->level++;

        if (engine->player.health < PLAYER_MAX_HEALTH) {
            engine->player.health++;
        }

        engine->last_event = EVENT_LEVEL;
        PongEngine_Beep(BUZZER_LEVEL_FREQ, BUZZER_LONG_MS);
        LoadLevel(engine);
    }
}

void PongEngine_Init(PongEngine_t* engine,
                     int16_t paddle_x,
                     int16_t paddle_y,
                     int16_t paddle_width,
                     int16_t paddle_height,
                     int16_t ball_size,
                     float ball_speed)
{
    (void)paddle_x;
    (void)paddle_y;
    (void)paddle_width;
    (void)paddle_height;
    (void)ball_size;
    (void)ball_speed;

    game_won = 0;
    level_intro_timer = 0;
    boss_vy = 1;

    engine->score = 0;
    engine->level = 1;
    engine->game_over = 0;
    engine->frame_counter = 0;
    engine->remaining_rocks = 0;
    engine->last_event = EVENT_READY;
    engine->last_action = ACTION_NONE;
    engine->shake_timer = 0;
    engine->shake_strength = 0;
    engine->boss_intro_timer = 0;

    engine->player.health = PLAYER_MAX_HEALTH;
    engine->player.stamina = 0;

    LoadLevel(engine);
}

uint8_t PongEngine_Update(PongEngine_t* engine, UserInput input, uint8_t action_pressed)
{
    uint16_t enemy_spawn_period;

    if (engine->game_over || game_won) {
        PongEngine_UpdateBuzzer();
        return 0;
    }

    engine->frame_counter++;

    if (level_intro_timer > 0) level_intro_timer--;
    if (engine->boss_intro_timer > 0) engine->boss_intro_timer--;
    if (engine->shake_timer > 0) engine->shake_timer--;

    StartDash(engine, input);
    UpdatePlayer(engine, input);
    UpdateDash(engine);
    UpdateBoss(engine);
    UpdateEnemies(engine);
    UpdateEnemyBullets(engine);
    HandleAction(engine, action_pressed);
    UpdatePlayerBullets(engine);
    CollectPickups(engine);
    CheckDoor(engine);

    enemy_spawn_period = 190;
    if (engine->level >= 2) enemy_spawn_period = 150;
    if (engine->level >= 3) enemy_spawn_period = 125;
    if (engine->level >= 4) enemy_spawn_period = 105;

    if (level_intro_timer == 0 && (engine->frame_counter % enemy_spawn_period) == 0) {
        SpawnEnemy(engine);
    }

    PongEngine_UpdateBuzzer();

    return (engine->player.health > 0) ? engine->player.health : 0;
}

static void DrawPlayer(PongEngine_t* engine, int16_t ox, int16_t oy)
{
    int16_t px = engine->player.x + ox;
    int16_t py = engine->player.y + oy;

    LCD_Draw_Rect(px + 7, py + 1, 10, 8, COLOR_RED, 1);
    LCD_Draw_Rect(px + 4, py + 9, 16, 10, COLOR_RED, 1);
    LCD_Draw_Rect(px + 2, py + 12, 20, 5, COLOR_RED, 1);
    LCD_Draw_Line(px + 10, py + 19, px + 7, py + 25, COLOR_RED);
    LCD_Draw_Line(px + 14, py + 19, px + 17, py + 25, COLOR_RED);

    if (engine->player.attack_timer > 0 || engine->player.charge_frames > 0) {
        if (engine->player.facing_right) {
            LCD_Draw_Line(px + 16, py + 12, px + 26, py + 10, COLOR_YELLOW);

            if (engine->player.weapon_type == WEAPON_BLASTER) {
                LCD_Draw_Rect(px + 21, py + 8, 7, 4, COLOR_CYAN, 1);
            } else {
                LCD_Draw_Rect(px + 21, py + 7, 5, 5, COLOR_YELLOW, 1);
            }
        } else {
            LCD_Draw_Line(px + 8, py + 12, px - 2, py + 10, COLOR_YELLOW);

            if (engine->player.weapon_type == WEAPON_BLASTER) {
                LCD_Draw_Rect(px - 4, py + 8, 7, 4, COLOR_CYAN, 1);
            } else {
                LCD_Draw_Rect(px - 2, py + 7, 5, 5, COLOR_YELLOW, 1);
            }
        }
    } else {
        LCD_Draw_Line(px + 8, py + 12, px + 3, py + 16, COLOR_RED);
        LCD_Draw_Line(px + 16, py + 12, px + 21, py + 16, COLOR_RED);
    }

    if (engine->player.dash_timer > 0) {
        if (engine->player.facing_right) {
            LCD_Draw_Line(px - 8, py + 12, px - 2, py + 12, COLOR_WHITE);
            LCD_Draw_Line(px - 12, py + 16, px - 4, py + 16, COLOR_WHITE);
        } else {
            LCD_Draw_Line(px + 26, py + 12, px + 32, py + 12, COLOR_WHITE);
            LCD_Draw_Line(px + 28, py + 16, px + 36, py + 16, COLOR_WHITE);
        }
    }
}

static void DrawAtmosphere(PongEngine_t* engine, int16_t ox, int16_t oy)
{
    uint8_t bg = BgColor(engine->level);
    uint8_t floor = FloorColor(engine->level);

    LCD_Draw_Rect(0 + ox, 0 + oy, SCREEN_WIDTH, SCREEN_HEIGHT, bg, 1);

    if (engine->level == 1) {
        LCD_Draw_Rect(12 + ox, 76 + oy, 18, 48, COLOR_NAVY, 1);
        LCD_Draw_Rect(46 + ox, 58 + oy, 22, 66, COLOR_NAVY, 1);
        LCD_Draw_Rect(86 + ox, 66 + oy, 18, 58, COLOR_NAVY, 1);
        LCD_Draw_Rect(128 + ox, 52 + oy, 24, 72, COLOR_NAVY, 1);
        LCD_Draw_Rect(174 + ox, 62 + oy, 20, 62, COLOR_NAVY, 1);
        LCD_Draw_Rect(208 + ox, 48 + oy, 22, 76, COLOR_NAVY, 1);
    } else if (engine->level == 2) {
        LCD_Draw_Rect(18 + ox, 112 + oy, 48, 7, COLOR_BLACK, 1);
        LCD_Draw_Rect(80 + ox, 104 + oy, 58, 7, COLOR_BLACK, 1);
        LCD_Draw_Rect(152 + ox, 112 + oy, 58, 7, COLOR_BLACK, 1);
        LCD_Draw_Line(25 + ox, 112 + oy, 40 + ox, 88 + oy, COLOR_BLACK);
        LCD_Draw_Line(112 + ox, 104 + oy, 130 + ox, 82 + oy, COLOR_BLACK);
    } else if (engine->level == 3) {
        LCD_Draw_Rect(18 + ox, 28 + oy, 3, 3, COLOR_WHITE, 1);
        LCD_Draw_Rect(58 + ox, 48 + oy, 3, 3, COLOR_WHITE, 1);
        LCD_Draw_Rect(116 + ox, 32 + oy, 3, 3, COLOR_WHITE, 1);
        LCD_Draw_Rect(184 + ox, 44 + oy, 3, 3, COLOR_WHITE, 1);
        LCD_Draw_Rect(205 + ox, 26 + oy, 3, 3, COLOR_WHITE, 1);
    } else if (engine->level == 4) {
        LCD_Draw_Line(0 + ox, 122 + oy, 240 + ox, 100 + oy, COLOR_RED);
        LCD_Draw_Line(0 + ox, 146 + oy, 240 + ox, 130 + oy, COLOR_RED);
    } else {
        LCD_Draw_Rect(0 + ox, 118 + oy, 240, 8, COLOR_RED, 1);
        LCD_Draw_Rect(0 + ox, 132 + oy, 240, 4, COLOR_PURPLE, 1);
    }

    LCD_Draw_Rect(0 + ox, WALKWAY_Y + oy, SCREEN_WIDTH, WALKWAY_H, floor, 1);
    LCD_Draw_Line(0 + ox, WALKWAY_Y + oy, SCREEN_WIDTH - 1 + ox, WALKWAY_Y + oy, COLOR_WHITE);
}

void PongEngine_Draw(PongEngine_t* engine)
{
    int i;
    int16_t ox = ShakeX(engine);
    int16_t oy = ShakeY(engine);
    uint8_t door_colour;

    LCD_Fill_Buffer(COLOR_BLACK);
    DrawAtmosphere(engine, ox, oy);

    door_colour = engine->door_open ? COLOR_GREEN : COLOR_PURPLE;
    LCD_Draw_Rect(engine->door_x + ox, engine->door_y + oy,
                  engine->door_w, engine->door_h, door_colour, 1);
    LCD_Draw_Rect(engine->door_x + ox, engine->door_y + oy,
                  engine->door_w, engine->door_h, COLOR_WHITE, 0);

    for (i = 0; i < PONG_MAX_ROCKS; i++) {
        RockBarrier_t* rock = &engine->rocks[i];

        if (!rock->active) continue;

        LCD_Draw_Rect(rock->x + ox, rock->y + oy,
                      rock->width, rock->height,
                      RockColor(engine->level, rock->hp), 1);
        LCD_Draw_Rect(rock->x + ox, rock->y + oy,
                      rock->width, rock->height, COLOR_BLACK, 0);
    }

    for (i = 0; i < PONG_MAX_PICKUPS; i++) {
        Pickup_t* pickup = &engine->pickups[i];

        if (!pickup->active) continue;

        if (pickup->type == PICKUP_BLASTER) {
            LCD_Draw_Rect(pickup->x + ox, pickup->y + 2 + oy, 10, 5, COLOR_CYAN, 1);
            LCD_Draw_Rect(pickup->x + 8 + ox, pickup->y + 3 + oy, 3, 3, COLOR_WHITE, 1);
        } else if (pickup->type == PICKUP_HEART) {
            LCD_Draw_Rect(pickup->x + ox, pickup->y + 2 + oy, 10, 7, COLOR_RED, 1);
        } else {
            LCD_Draw_Rect(pickup->x + ox, pickup->y + oy, 10, 10, COLOR_CYAN, 1);
        }
    }

    for (i = 0; i < PONG_MAX_ENEMIES; i++) {
        Enemy_t* enemy = &engine->enemies[i];

        if (!enemy->active) continue;

        if (enemy->flying) {
            LCD_Draw_Rect(enemy->x + 2 + ox, enemy->y + 5 + oy, 12, 8, COLOR_MAGENTA, 1);
            LCD_Draw_Line(enemy->x + 2 + ox, enemy->y + 5 + oy,
                          enemy->x - 3 + ox, enemy->y + 1 + oy, COLOR_WHITE);
            LCD_Draw_Line(enemy->x + 14 + ox, enemy->y + 5 + oy,
                          enemy->x + 19 + ox, enemy->y + 1 + oy, COLOR_WHITE);
        } else {
            LCD_Draw_Rect(enemy->x + 3 + ox, enemy->y + 5 + oy, 10, 11, COLOR_CYAN, 1);
            LCD_Draw_Line(enemy->x + 8 + ox, enemy->y + 16 + oy,
                          enemy->x + 4 + ox, enemy->y + 21 + oy, COLOR_CYAN);
            LCD_Draw_Line(enemy->x + 8 + ox, enemy->y + 16 + oy,
                          enemy->x + 12 + ox, enemy->y + 21 + oy, COLOR_CYAN);
        }
    }

    if (engine->boss.active) {
        LCD_Draw_Rect(engine->boss.x + ox, engine->boss.y + 6 + oy,
                      engine->boss.width, engine->boss.height, COLOR_MAGENTA, 1);
        LCD_Draw_Rect(engine->boss.x + 5 + ox, engine->boss.y + oy,
                      24, 10, COLOR_MAGENTA, 1);
        LCD_Draw_Line(engine->boss.x + 4 + ox, engine->boss.y + 22 + oy,
                      engine->boss.x - 8 + ox, engine->boss.y + 27 + oy, COLOR_MAGENTA);
        LCD_Draw_Line(engine->boss.x + 38 + ox, engine->boss.y + 22 + oy,
                      engine->boss.x + 50 + ox, engine->boss.y + 27 + oy, COLOR_MAGENTA);

        LCD_Draw_Rect(70, 42, 100, 8, COLOR_WHITE, 0);
        LCD_Draw_Rect(71, 43, (engine->boss.hp * 98) / engine->boss.max_hp, 6, COLOR_RED, 1);
    }

    for (i = 0; i < PONG_MAX_PLAYER_BULLETS; i++) {
        Bullet_t* bullet = &engine->player_bullets[i];

        if (bullet->active) {
            LCD_Draw_Rect(bullet->x + ox, bullet->y + oy,
                          bullet->width, bullet->height, COLOR_YELLOW, 1);
        }
    }

    for (i = 0; i < PONG_MAX_ENEMY_BULLETS; i++) {
        Bullet_t* bullet = &engine->enemy_bullets[i];

        if (bullet->active) {
            LCD_Draw_Rect(bullet->x + ox, bullet->y + oy,
                          bullet->width, bullet->height, COLOR_WHITE, 1);
            LCD_Draw_Line(bullet->x + ox, bullet->y + 1 + oy,
                          bullet->x - 2 + ox, bullet->y + oy, COLOR_RED);
        }
    }

    DrawPlayer(engine, ox, oy);

    LCD_Draw_Rect(8, 28, 52, 8, COLOR_WHITE, 0);
    LCD_Draw_Rect(9, 29, engine->player.health * 10, 6, COLOR_RED, 1);

    LCD_Draw_Rect(176, 28, 56, 8, COLOR_WHITE, 0);
    LCD_Draw_Rect(177, 29, engine->player.stamina * 9, 6, COLOR_CYAN, 1);

    if (engine->player.charge_frames > 0) {
        uint8_t charge_width = engine->player.charge_frames;
        if (charge_width > 40) charge_width = 40;

        LCD_Draw_Rect(100, 28, 42, 8, COLOR_WHITE, 0);
        LCD_Draw_Rect(101, 29, charge_width, 6, COLOR_YELLOW, 1);
    }

    if (engine->player.weapon_type == WEAPON_BLASTER) {
        LCD_Draw_Rect(100, 8, 10, 5, COLOR_CYAN, 1);
        LCD_Draw_Rect(108, 9, 4, 3, COLOR_WHITE, 1);
    }

    if (level_intro_timer > 0) {
        if (engine->level == 1) LCD_printString("LEVEL 1", 45, 92, COLOR_WHITE, 3);
        else if (engine->level == 2) LCD_printString("LEVEL 2", 45, 92, COLOR_WHITE, 3);
        else if (engine->level == 3) LCD_printString("LEVEL 3", 45, 92, COLOR_WHITE, 3);
        else if (engine->level == 4) LCD_printString("LEVEL 4", 45, 92, COLOR_WHITE, 3);
        else LCD_printString("FINAL", 58, 82, COLOR_WHITE, 3);
    }

    if (engine->boss_intro_timer > 0) {
        LCD_printString("BOSS", 78, 108, COLOR_WHITE, 4);
    }

    if (engine->game_over) {
        LCD_Draw_Rect(20, 78, 200, 76, COLOR_BLACK, 1);
        LCD_Draw_Rect(20, 78, 200, 76, COLOR_RED, 0);
        LCD_printString("GAME", 62, 90, COLOR_WHITE, 4);
        LCD_printString("OVER", 62, 122, COLOR_WHITE, 4);
    }

    if (game_won) {
        LCD_Draw_Rect(20, 78, 200, 76, COLOR_BLACK, 1);
        LCD_Draw_Rect(20, 78, 200, 76, COLOR_GREEN, 0);
        LCD_printString("YOU", 70, 88, COLOR_WHITE, 4);
        LCD_printString("WIN", 70, 122, COLOR_WHITE, 4);
    }
}

uint8_t PongEngine_GetLives(PongEngine_t* engine)
{
    return engine->player.health;
}

uint16_t PongEngine_GetScore(PongEngine_t* engine)
{
    return engine->score;
}

uint16_t PongEngine_GetOpponentScore(PongEngine_t* engine)
{
    (void)engine;
    return 0;
}

uint8_t PongEngine_GetBottomPoints(PongEngine_t* engine)
{
    return engine->player.health;
}

uint8_t PongEngine_GetTopPoints(PongEngine_t* engine)
{
    return engine->player.stamina;
}

uint8_t PongEngine_GetBottomGames(PongEngine_t* engine)
{
    return engine->level;
}

uint8_t PongEngine_GetTopGames(PongEngine_t* engine)
{
    if (engine->boss.active) return engine->boss.hp;
    return engine->remaining_rocks;
}

const char* PongEngine_GetBottomPointText(PongEngine_t* engine)
{
    (void)engine;
    return "HP";
}

const char* PongEngine_GetTopPointText(PongEngine_t* engine)
{
    (void)engine;
    return "ST";
}

const char* PongEngine_GetLastShotText(PongEngine_t* engine)
{
    switch (engine->last_action) {
    case ACTION_MOVE: return "MOVE";
    case ACTION_SMASH: return "SMASH";
    case ACTION_POWER: return "POWER";
    case ACTION_JUMP: return "JUMP";
    case ACTION_SHOOT: return "SHOT";
    case ACTION_PICKUP: return "PICK";
    case ACTION_DASH: return "DASH";
    default: return "READY";
    }
}

const char* PongEngine_GetLastEventText(PongEngine_t* engine)
{
    switch (engine->last_event) {
    case EVENT_HIT: return "HIT";
    case EVENT_BREAK: return "BREAK";
    case EVENT_BLOCKED: return "BLOCK";
    case EVENT_LEVEL: return "NEXT";
    case EVENT_MISS: return "MISS";
    case EVENT_HURT: return "HURT";
    case EVENT_ENEMY: return "ENEMY";
    case EVENT_POWER: return "POWER";
    case EVENT_BULLET: return "BULLET";
    case EVENT_JUMP: return "JUMP";
    case EVENT_PICKUP: return "ITEM";
    case EVENT_ARROW: return "ARROW";
    case EVENT_DASH: return "DASH";
    default: return "READY";
    }
}
