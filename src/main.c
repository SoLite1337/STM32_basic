#include "stm32f4xx_hal.h"
#include <stdbool.h>

#define LED_PORT GPIOC
#define LED_PIN GPIO_PIN_13
#define GLED_PIN GPIO_PIN_14

#define BTN_PORT GPIOB
#define BTN_PIN GPIO_PIN_0
#define BTN_IRQn EXTI0_IRQn
#define BTN2_PIN GPIO_PIN_6
#define BTN2_IRQn EXTI9_5_IRQn

#define DEBOUNCE_MS 50

TIM_HandleTypeDef htim2;

static void GPIO_Init(void);
static void TIM2_Init(void);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    GPIO_Init();
    TIM2_Init();

    while (1)
    {
        __WFI();
    }
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* On-board LED (PC13, active low) */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    gpio.Pin = LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);

    /* Green LED (PC14, active high) */
    HAL_GPIO_WritePin(LED_PORT, GLED_PIN, GPIO_PIN_RESET);
    gpio.Pin = GLED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);

    /* Button 1 (PB0, to 3V3 when pressed): rising edge, internal pull-down */
    gpio.Pin = BTN_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BTN_PORT, &gpio);

    /* Button 2 (PB6, to 3V3 via R1): rising edge, NO internal pull,
       external R2 pull-down + C form the RC debounce filter */
    gpio.Pin = BTN2_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BTN_PORT, &gpio);

    HAL_NVIC_SetPriority(BTN_IRQn, 2, 1);
    HAL_NVIC_EnableIRQ(BTN_IRQn);

    HAL_NVIC_SetPriority(BTN2_IRQn, 2, 2);
    HAL_NVIC_EnableIRQ(BTN2_IRQn);
}

static void TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 16000 - 1; /* 16 MHz / 16000 = 1 kHz (1 ms tick) */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = DEBOUNCE_MS - 1; /* 50 ms */
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

/* ---------------- Interrupt handlers ---------------- */

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BTN_PIN);
}

/* EXTI lines 5..9 share one vector */
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BTN2_PIN);
}

void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/* ---------------- Callbacks ---------------- */

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (BTN_PIN == pin)
    {
        HAL_NVIC_DisableIRQ(EXTI0_IRQn);

        __HAL_TIM_SetCounter(&htim2, 0);
        __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
        HAL_TIM_Base_Start_IT(&htim2);
    }
    else if (BTN2_PIN == pin)
    {
        HAL_GPIO_TogglePin(LED_PORT, GLED_PIN);
    }
}

/* Debounce window elapsed (button 1 only) */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (TIM2 == htim->Instance)
    {
        HAL_TIM_Base_Stop_IT(&htim2);

        if (GPIO_PIN_SET == HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN))
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }

        __HAL_GPIO_EXTI_CLEAR_IT(BTN_PIN);
        HAL_NVIC_ClearPendingIRQ(BTN_IRQn);
        HAL_NVIC_EnableIRQ(BTN_IRQn);
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}