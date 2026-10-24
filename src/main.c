#include "stm32f4xx_hal.h"
#include <stdbool.h>

/* Red LED (on-board, active-low) */
#define LED_PORT GPIOB
#define LED_R_PIN GPIO_PIN_0

/* Green LED (external, active-high: PB0 -> resistor -> LED -> GND) */
#define LED_G_PIN GPIO_PIN_1

/* Buttons: one side to the pin, other side to GND */
#define BTN_R_PORT GPIOA
#define BTN_R_PIN GPIO_PIN_5 // exti 5A
#define BTN_G_PORT GPIOB     // exte9_5
#define BTN_G_PIN GPIO_PIN_6 // exti 6B 6A

#define DEBOUNCE_MS 100

static volatile uint32_t last_press_r = 0;
static volatile uint32_t last_press_g = 0;
static volatile bool press_r = false;
static volatile bool press_g = false;

static volatile uint32_t last_press = 0;

static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = LED_R_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
    HAL_GPIO_WritePin(LED_PORT, LED_R_PIN, GPIO_PIN_SET);

    gpio.Pin = LED_G_PIN;
    HAL_GPIO_Init(LED_PORT, &gpio);

    gpio.Pin = BTN_R_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BTN_R_PORT, &gpio);

    /* Green button (PB6): falling edge, pull-up */
    gpio.Pin = BTN_G_PIN;
    HAL_GPIO_Init(BTN_G_PORT, &gpio);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BTN_R_PIN);
    HAL_GPIO_EXTI_IRQHandler(BTN_G_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin == BTN_R_PIN)
    {
        if (now - last_press_r > DEBOUNCE_MS)
        {
            last_press_r = now;
            press_r = true;
        }
    }
    else if (GPIO_Pin == BTN_G_PIN)
    {
        if (now - last_press_g > DEBOUNCE_MS)
        {
            last_press_g = now;
            press_g = true;
        }
    }
}

int main(void)
{
    HAL_Init();
    GPIO_Init();

    while (1)
    {
        if (press_r)
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_R_PIN);
            press_r = false;
        }
        
        if (press_g)
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_G_PIN);
            press_g = false;
        }
    }
}