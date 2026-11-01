/* ============================================================================
 * Мигание двумя светодиодами по двум кнопкам, HAL + прерывания.
 *
 *  PC13 — синий/бортовой LED, АКТИВНЫЙ НИЗКИЙ (0 = светит)
 *  PC14 — зелёный LED,        АКТИВНЫЙ ВЫСОКИЙ (1 = светит)
 *  PB0  — кнопка 1, замыкает на 3V3, внутренний pull-down, EXTI0
 *  PB6  — кнопка 2, замыкает на 3V3 через R1, внешний RC-фильтр, EXTI6 (вектор 9_5)
 *
 *  BTN1: антидребезг программный — EXTI отключается на 50 мс (TIM2),
 *        затем проверяется, что кнопка всё ещё нажата, и происходит toggle.
 *  BTN2: полагаемся на аппаратный RC-фильтр — каждый фронт = toggle.
 *
 *  SystemClock_Config() НЕ вызывается, чип работает от HSI 16 МГц
 *  после сброса. Prescaler TIM2 посчитан именно под 16 МГц на входе таймера.
 *  Если добавить PLL / смену тактирования — пересчитать или сломается 50 мс.
 * ========================================================================== */

#include "stm32f4xx_hal.h"
#include <stdbool.h>          
/* --- Пины и порты --------------------------------------------------------- */
#define LED_PORT        GPIOC          // порт для обоих светодиодов
#define LED_PIN         GPIO_PIN_13    // бортовой LED, активный низкий
#define GLED_PIN        GPIO_PIN_14    // зелёный LED, активный высокий

#define BTN_PORT        GPIOB
#define BTN_PIN         GPIO_PIN_0     // кнопка 1 -> EXTI0
#define BTN_IRQn        EXTI0_IRQn     // вектор прерывания для PB0
#define BTN2_PIN        GPIO_PIN_6     // кнопка 2 -> EXTI6, попадает в вектор EXTI9_5
#define BTN2_IRQn       EXTI9_5_IRQn   // линии EXTI5..9 делят одно прерывание

#define DEBOUNCE_MS     50             // окно антидребезга BTN1

/* Глобальный (не static — стоит сделать static) handle таймера TIM2.
 * HAL требует, чтобы handle жил всё время работы таймера, поэтому не в стеке. */
TIM_HandleTypeDef htim2;

/* Прототипы внутренних функций */
static void GPIO_Init(void);
static void TIM2_Init(void);
void        Error_Handler(void);

/* ========================================================================== */
int main(void)
{
    HAL_Init();          // инициализация HAL, SysTick, NVIC priorities group
    GPIO_Init();         // настройка пинов + включение нужных EXTI
    TIM2_Init();         // таймер 1 кГц, шаг 50 мс, используется для BTN1

    /* Вся работа идёт в прерываниях, main просто уходит в сон до любого IRQ.
     * Замечание: SysTick будит ядро каждую 1 мс, так что "сон" неглубокий.
     * Для реальной экономии надо HAL_SuspendTick()/HAL_ResumeTick() вокруг WFI. */
    while (1)
    {
        __WFI();         // wait for interrupt — ядро спит до следующего прерывания
    }
}

/* ========================================================================== */
static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};   // обнуление — важно, иначе мусор в полях

    /* Тактирование портов B и C. Без этого записи в регистры GPIO
    
     * игнорируются (периферия обесточена по тактам). */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* --- Бортовой LED PC13, активный НИЗКИЙ ------------------------------
     * Сначала выставляем "погашен" (SET = 1 = высокий = погас), ПОТОМ
     * переводим в output — иначе на пине будет глитч при переключении режима. */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;      // push-pull
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;      // для LED хватает, меньше EMI
    HAL_GPIO_Init(LED_PORT, &gpio);

    /* --- Зелёный LED PC14, активный ВЫСОКИЙ ------------------------------
     * Аналогично: сначала RESET (=0, погашен), затем переключение в output. */
    HAL_GPIO_WritePin(LED_PORT, GLED_PIN, GPIO_PIN_RESET);
    gpio.Pin   = GLED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);

    /* --- Кнопка 1: PB0 -> 3V3 при нажатии ---------------------------------
     * Внутренний pull-down удерживает 0, нажатие даёт фронт 0->1 — RISING.
     * Дребезг на этом фронте обрабатывается через TIM2 (см. колбэки). */
    gpio.Pin  = BTN_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;       // interrupt on rising edge
    gpio.Pull = GPIO_PULLDOWN;             // без внешнего резистора
    HAL_GPIO_Init(BTN_PORT, &gpio);

    /* --- Кнопка 2: PB6 -> 3V3 через R1 -----------------------------------
     * Внутренний pull не используем: внешние R2 (pull-down) и C образуют
     * RC-фильтр. Программной защиты от дребезга тут НЕТ — вся надежда
     * на то, что RC гасит все перепады. Требование к железу: τ = R2*C
     * должно быть заведомо больше времени дребезга кнопки. */
    gpio.Pin  = BTN2_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BTN_PORT, &gpio);

    /* Настраиваем и разрешаем NVIC для EXTI0 (BTN1).
     * Приоритет 2, subpriority 1 — ниже TIM2 (он должен иметь возможность
     * вытеснить EXTI, чтобы остановить таймер строго по времени). */
    HAL_NVIC_SetPriority(BTN_IRQn, 2, 1);
    HAL_NVIC_EnableIRQ(BTN_IRQn);

    /* NVIC для общего вектора EXTI9_5 (BTN2).
     * Subpriority 2 — BTN2 не вытесняет BTN1, но оба ниже TIM2. */
    HAL_NVIC_SetPriority(BTN2_IRQn, 2, 2);
    HAL_NVIC_EnableIRQ(BTN2_IRQn);
}

/* ========================================================================== */
static void TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();          // тактирование TIM2

    htim2.Instance               = TIM2;
    /* prescaler = 16000-1 при входе 16 МГц (HSI) даёт 1 кГц.
     * ВНИМАНИЕ: если включить PLL и поменять SystemClock, TIM2 будет
     * тикать от APB1 (или PCLK1*2) — 50 мс превратятся во что-то другое. */
    htim2.Init.Prescaler         = 16000 - 1;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = DEBOUNCE_MS - 1;   // 50 тиков = 50 мс
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();      // не смогли инициализировать таймер — дальше нет смысла
    }

    /* TIM2 приоритет 1 (выше EXTI-кнопок, ниже SysTick по умолчанию).
     * Чтобы вытеснять EXTI-колбэк кнопок и корректно останавливать таймер. */
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

/* ---------------- Обработчики прерываний --------------------------------- */

/* SysTick 1 кГц, инкрементирует счётчик HAL_GetTick().
 * Определён здесь вручную — при появлении stm32f4xx_it.c будет конфликт
 * (multiple definition). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Вектор EXTI0 — обслуживает BTN1 (PB0). HAL сам снимет флаг и вызовет
 * HAL_GPIO_EXTI_Callback(BTN_PIN). */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BTN_PIN);
}

/* Вектор EXTI9_5 — общий для линий 5..9. У нас на нём BTN2 (PB6 -> EXTI6). */
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BTN2_PIN);
}

/* Вектор TIM2 — HAL внутри разберёт, что случилось (update), сбросит флаг
 * и вызовет HAL_TIM_PeriodElapsedCallback(). */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/* ---------------- Колбэки HAL -------------------------------------------- */

/* Общий колбэк для всех EXTI, HAL передаёт сюда номер сработавшей линии. */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (BTN_PIN == pin)
    {
        /* BTN1 нажата: начинаем окно антидребезга.
         * Отключаем EXTI0, чтобы новые фронты от дребезга не мешали. */
        HAL_NVIC_DisableIRQ(EXTI0_IRQn);

        /* Сброс счётчика и флага — окно ровно 50 мс от нажатия.
         * __HAL_TIM_CLEAR_FLAG обязателен, иначе "старый" update-флаг
         * может сразу выстрелить колбэком. */
        __HAL_TIM_SetCounter(&htim2, 0);
        __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

        /* Возврат не проверяется. Если HAL_TIM_Base_Start_IT вернёт ошибку,
         * EXTI0 останется отключённым и BTN1 перестанет работать навсегда —
         * это место стоит обернуть проверкой и восстановлением EXTI. */
        HAL_TIM_Base_Start_IT(&htim2);
    }
    else if (BTN2_PIN == pin)
    {
        /* BTN2: без программной проверки — доверяем RC-фильтру.
         * Каждый прошедший фронт -> toggle. Если RC слабый, будет
         * многократное мигание на одном нажатии. */
        HAL_GPIO_TogglePin(LED_PORT, GLED_PIN);
    }
}

/* Колбэк по переполнению TIM2 — конец окна антидребезга BTN1. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (TIM2 == htim->Instance)
    {
        HAL_TIM_Base_Stop_IT(&htim2);   // окно закрыто, таймер больше не нужен

        /* Проверяем реальное состояние кнопки: если через 50 мс всё ещё
         * нажата — это настоящее нажатие, а не игла. */
        if (GPIO_PIN_SET == HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN))
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }

        /* Порядок важен: сначала очистить флаг EXTI в периферии,
         * затем pending в NVIC (там мог защёлкнуться фронт, пока IRQ был
         * выключен), и только потом разрешить прерывание — иначе сразу
         * после EnableIRQ вылетит ложное срабатывание. */
        __HAL_GPIO_EXTI_CLEAR_IT(BTN_PIN);
        HAL_NVIC_ClearPendingIRQ(BTN_IRQn);
        HAL_NVIC_EnableIRQ(BTN_IRQn);
    }
}

/* Аварийный останов: запретить все прерывания и не возвращаться.
 * Если запускаете под отладчиком — полезно добавить __BKPT(0)
 * под #ifdef DEBUG, чтобы отладчик встал здесь, а не просто завис. */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}