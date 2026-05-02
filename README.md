# ELEC2645 Group Games Console

**Module:** ELEC2645 Embedded Systems Project — Unit 4 Group Project  
**Platform:** STM32 Nucleo-L476RG  
**Deadline:** Thursday 7 May 2026, 2:00 PM

---

## 🎮 Games

| Slot | Student | Game | Status |
|------|---------|------|--------|
| Game 1 | Ali | ♟ Two-Player Chess | ✅ Complete |
| Game 2 | Ahmad | 🚀 Rock-It Ralph | ✅ Complete |
| Game 3 | Ali | 🏍 Tron Light Cycles | ✅ Complete |

---

## Project Structure

```
ELEC2645-Group-Games-Console/
├── Core/                      # STM32 HAL auto-generated files
├── Drivers/                   # STM32 HAL drivers
├── shared/                    # ⚠️ Group shared code (discuss changes together)
│   ├── Menu.h / Menu.c        # Main menu system
│   └── InputHandler.h/.c      # Button input handler
├── game_1/                    # Student 1 — Chess
│   ├── Game_1.h
│   └── Game_1.c
├── game_2/                    # Student 2 — edit only this folder
│   ├── Game_2.h
│   └── Game_2.c
├── game_3/                    # Student 3 — edit only this folder
│   ├── Game_3.h
│   └── Game_3.c
├── Joystick/                  # Joystick driver
├── PWM/                       # PWM LED driver
├── Buzzer/                    # Buzzer driver
├── ST7789V2_Driver_STM32L4/   # LCD driver
├── TIMER_USAGE_GUIDE.md       # Timer reference
└── CMakeLists.txt
```

---

## How to Work on Your Game

1. **Clone the repo:**
   ```bash
   git clone https://github.com/alimleahat/ELEC2645-Group-Games-Console.git
   ```

2. **Open in STM32CubeIDE:**  
   File → Import → Existing Projects into Workspace → select the cloned folder

3. **Edit only your game folder:**
   - Student 1 → `game_1/Game_1.c`
   - Student 2 → `game_2/Game_2.c`
   - Student 3 → `game_3/Game_3.c`

4. **Each game must implement one function:**
   ```c
   MenuState GameX_Run(void);
   // Runs its own loop, returns MENU_STATE_HOME when player exits
   ```

5. **Commit and push regularly** (at least weekly for journal evidence):
   ```bash
   git add game_X/
   git commit -m "Description of what you did"
   git push
   ```

---

## Controls (common to all games)

| Input | Function |
|-------|----------|
| Joystick (N/S/W/E) | Move / navigate |
| BT2 | Select / action |
| BT3 | Cancel / return to menu |

### Chess (Game 1) specific
| Input | Action |
|-------|--------|
| Joystick | Move cursor over board |
| BT2 | Select piece / confirm move |
| BT3 (1st press) | Cancel current selection |
| BT3 (2nd press) | Return to main menu |

---

## Available Hardware APIs

```c
// LCD
LCD_Fill_Buffer(colour);                          // clear framebuffer
LCD_Draw_Rect(x, y, w, h, colour, filled);        // draw rectangle
LCD_Draw_Circle(cx, cy, radius, colour, filled);  // draw circle
LCD_printString(text, x, y, colour, scale);       // draw text
LCD_Refresh(&cfg0);                               // push buffer to screen

// Input
Input_Read();                        // call once per frame
current_input.btn2_pressed           // BT2 edge-detected press
current_input.btn3_pressed           // BT3 edge-detected press
Joystick_Read(&joystick_cfg);        // read joystick ADC
Joystick_GetInput(&joystick_cfg);    // returns UserInput (direction, magnitude)

// Audio
buzzer_tone(&buzzer_cfg, freq_hz, volume);
buzzer_off(&buzzer_cfg);

// LEDs
PWM_SetDuty(&pwm_cfg, percent);      // 0-100%

// Timing
HAL_GetTick();    // milliseconds since boot
HAL_Delay(ms);   // blocking delay
```

**Screen dimensions:** 240 × 320 px (portrait)  
**Colour palette:** 4-bit index (0 = black, 15 = white)  
See `TIMER_USAGE_GUIDE.md` for TIM6/TIM7 timer usage.

---

## Memory Guidelines (STM32L476RG)

| Resource | Total | Notes |
|----------|-------|-------|
| Flash | 1 MB | Program code + const data |
| RAM | 128 KB | Stack + globals + LCD framebuffer (~38 KB) |

- ✅ Use `static` for game state variables (no heap)
- ✅ Prefix all functions with `Game1_`, `Game2_`, `Game3_` to avoid name clashes
- ✅ Keep large lookup tables in `const` (stored in Flash, not RAM)
- ❌ Do not use `malloc` / `free`
