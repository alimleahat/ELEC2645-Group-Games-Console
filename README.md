# ELEC2645 Group Games Console

Group project for ELEC2645 Embedded Systems — STM32 Nucleo-L476RG.

## Games

| Slot | Game | Status |
|------|------|--------|
| Game 1 | Two-Player Chess | ✅ Done |
| Game 2 | Rock-It Ralph | ✅ Done |
| Game 3 | Tron Light Cycles | ✅ Done |

## Structure

```
game_1/   — Chess (Ali)
game_2/   — Rock-It Ralph (Ahmad)
game_3/   — Tron (Ali)
shared/   — Menu system and input handler
Core/     — STM32 HAL files
```

## Controls

- Joystick: move / navigate
- BT2: action / select
- BT3: back to menu

## Notes

- Each game lives in its own folder and implements `GameX_Run()`
- Don't edit `shared/` without checking with the group first
- Screen: 240×320, 4-bit colour palette
- No malloc — use static variables for game state
