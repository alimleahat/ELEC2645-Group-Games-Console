#include "InputHandler.h"
#include "main.h"

// Global input state
InputState current_input = {0};

// Track button presses in interrupt
static volatile uint8_t btn2_raw_press = 0;
static volatile uint8_t btn3_raw_press = 0;

void Input_Init(void) {
    // GPIO and EXTI already initialized by MX_GPIO_Init() in main.c
    // Just reset the state
    current_input.btn2_pressed = 0;
    current_input.btn3_pressed = 0;
    btn2_raw_press = 0;
    btn3_raw_press = 0;
}

void Input_Read(void) {
    // Copy the button press flags from interrupt to current input state
    // This is read once per frame by the main loop
    current_input.btn2_pressed = btn2_raw_press;
    current_input.btn3_pressed = btn3_raw_press;
    
    // Reset the flags after reading so they only trigger once
    btn2_raw_press = 0;
    btn3_raw_press = 0;
}

// ===== INTERRUPT CALLBACK FOR BUTTONS =====
// Called by hardware when button is pressed
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    static uint32_t last_btn2_interrupt = 0;
    static uint32_t last_btn3_interrupt = 0;
    uint32_t current_time = HAL_GetTick();
    
    // Handle BT2 button (PC2) — in-game interact / action
    if (GPIO_Pin == BTN2_Pin) {
        if ((current_time - last_btn2_interrupt) > 200) {
            last_btn2_interrupt = current_time;
            btn2_raw_press = 1;
        }
    }

    // Handle joystick press (BTN3, PC3) — menu select / back to menu
    if (GPIO_Pin == BTN3_Pin) {
        if ((current_time - last_btn3_interrupt) > 200) {
            last_btn3_interrupt = current_time;
            btn3_raw_press = 1;
        }
    }
}
