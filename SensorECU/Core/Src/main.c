/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "hcsr04.h"
#include "isotp.h"
#include "uds.h"
#include "ota_flash.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    uint8_t  free_level;
    uint32_t esr;
    uint32_t tsr;
    uint32_t error_code;
} CAN_TxFailDiag_t;

typedef struct {
    uint8_t  fifo_fill;
    uint32_t esr;
    uint32_t error_code;
} CAN_RxDiag_t;

typedef struct {
    uint8_t  fifo_fill;
    uint32_t esr;
    uint32_t tsr;
    uint32_t error_code;
} CAN_ErrorDiag_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

IWDG_HandleTypeDef hiwdg;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static CAN_TxHeaderTypeDef tx_header;
static CAN_TxHeaderTypeDef obs_header;
static CAN_RxHeaderTypeDef rx_header;
static uint8_t  tx_data[8];
static uint8_t  rx_data[8];
static uint32_t tx_mailbox;

static volatile uint16_t s_distance_cm  = 0;
static volatile uint8_t  s_obstacle     = 0;
static volatile uint8_t  s_hb_sent      = 0;   /* 첫 heartbeat 송신 성공 = self-test liveness */
static uint32_t          s_measure_tick = 0;
static volatile uint8_t  s_hb_tx_fail_count;
static volatile uint8_t  s_obs_tx_fail_count;
static volatile uint8_t  s_can_rx0_full_count;
static volatile uint8_t  s_can_rx0_overrun_count;
static volatile uint8_t  s_can_error_count;
static volatile CAN_TxFailDiag_t s_hb_tx_fail_diag;
static volatile CAN_TxFailDiag_t s_obs_tx_fail_diag;
static volatile CAN_RxDiag_t     s_can_rx0_full_diag;
static volatile CAN_RxDiag_t     s_can_rx0_overrun_diag;
static volatile CAN_ErrorDiag_t  s_can_error_diag;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
static void MX_IWDG_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern uint32_t g_pfnVectors;

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

static uint32_t can_esr_tec(uint32_t esr)
{
    return (esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos;
}

static uint32_t can_esr_rec(uint32_t esr)
{
    return (esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos;
}

static uint32_t can_esr_lec(uint32_t esr)
{
    return (esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos;
}

static void copy_snapshot(void *dst, const volatile void *src, uint32_t len)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memcpy(dst, (const void *)src, len);
    if (!primask) {
        __enable_irq();
    }
}

static void capture_can_tx_fail(volatile CAN_TxFailDiag_t *diag, volatile uint8_t *count)
{
    diag->free_level = (uint8_t)HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
    diag->esr        = CAN1->ESR;
    diag->tsr        = CAN1->TSR;
    diag->error_code = hcan1.ErrorCode;
    (*count)++;
}

static void capture_can_rx_diag(volatile CAN_RxDiag_t *diag, volatile uint8_t *count)
{
    diag->fifo_fill  = (uint8_t)HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);
    diag->esr        = CAN1->ESR;
    diag->error_code = hcan1.ErrorCode;
    (*count)++;
}

static void capture_can_error_diag(CAN_HandleTypeDef *hcan)
{
    s_can_error_diag.fifo_fill  = (uint8_t)HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0);
    s_can_error_diag.esr        = CAN1->ESR;
    s_can_error_diag.tsr        = CAN1->TSR;
    s_can_error_diag.error_code = hcan->ErrorCode;
    s_can_error_count++;
}

static const char *isotp_tx_kind_name(uint8_t kind)
{
    if (kind == 1U) return "FC";
    if (kind == 2U) return "UDS-SF";
    return "UNKNOWN";
}

static const char *isotp_rx_abort_name(uint8_t reason)
{
    if (reason == 1U) return "CF_INACTIVE";
    if (reason == 2U) return "SN_MISMATCH";
    return "UNKNOWN";
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  SCB->VTOR = (uint32_t)&g_pfnVectors;
  __enable_irq();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN1_Init();
  MX_IWDG_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  CAN_FilterTypeDef filter = {0};
  filter.FilterBank           = 0;
  filter.FilterMode           = CAN_FILTERMODE_IDMASK;
  filter.FilterScale          = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh         = 0x0000;
  filter.FilterIdLow          = 0x0000;
  filter.FilterMaskIdHigh     = 0x0000;
  filter.FilterMaskIdLow      = 0x0000;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation     = ENABLE;
  HAL_StatusTypeDef cr = HAL_CAN_ConfigFilter(&hcan1, &filter);
  HAL_StatusTypeDef sr = HAL_CAN_Start(&hcan1);
  HAL_StatusTypeDef nr = HAL_CAN_ActivateNotification(
      &hcan1,
      CAN_IT_RX_FIFO0_MSG_PENDING |
      CAN_IT_RX_FIFO0_FULL |
      CAN_IT_RX_FIFO0_OVERRUN |
      CAN_IT_ERROR_WARNING |
      CAN_IT_ERROR_PASSIVE |
      CAN_IT_BUSOFF |
      CAN_IT_LAST_ERROR_CODE);
  printf("[CAN] ConfigFilter=%d Start=%d Notify=%d state=%lu ESR=0x%08lX\r\n",
         (int)cr, (int)sr, (int)nr, (uint32_t)hcan1.State, CAN1->ESR);

  tx_header.StdId = 0x201;
  tx_header.IDE   = CAN_ID_STD;
  tx_header.RTR   = CAN_RTR_DATA;
  tx_header.DLC   = 8;

  obs_header.StdId = 0x200;
  obs_header.IDE   = CAN_ID_STD;
  obs_header.RTR   = CAN_RTR_DATA;
  obs_header.DLC   = 8;

  HAL_TIM_Base_Start(&htim2);
  HAL_TIM_Base_Start_IT(&htim3);

  isotp_init(uds_on_isotp_rx);
  uds_init();

  printf("[SensorECU v%d] Start, Slot=%d\r\n", APP_VERSION, ota_get_active_slot());
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#ifdef HIL_SELFTEST_FAIL
    while (1) { }   /* HIL TC-02 픽스처: self-test 전 hang → IWDG 리셋 → 3-strike */
#endif
    HAL_IWDG_Refresh(&hiwdg);
    uds_process();

    /* self-test commit (FR-AB-004): main 루프 도달 + 페리페럴 init + heartbeat 송신 성공
     * → 현재 슬롯이 TRIAL이면 CONFIRMED로 확정(1회). 플래시 쓰기라 ISR 아닌 메인 루프에서. */
    static uint8_t s_self_confirmed = 0;
    if (s_hb_sent && !s_self_confirmed) {
        s_self_confirmed = 1;
        ota_meta_self_confirm(ota_get_active_slot());
    }

    static uint8_t s_last_tx_fail = 0;
    static uint8_t s_last_rx_abort = 0;
    static uint8_t s_last_hb_tx_fail = 0;
    static uint8_t s_last_obs_tx_fail = 0;
    static uint8_t s_last_rx0_full = 0;
    static uint8_t s_last_rx0_overrun = 0;
    static uint8_t s_last_can_error = 0;

    if (g_isotp_tx_fail_count != s_last_tx_fail) {
        ISOTP_TxFailDiag_t diag;
        isotp_diag_get_tx_fail(&diag);
        printf("[ISOTP TX FAIL] count=%u kind=%s pci0=0x%02X data1=0x%02X data2=0x%02X"
               " free=%u rx=%u/%u next_sn=%u"
               " ESR=0x%08lX TSR=0x%08lX err=0x%08lX TEC=%lu REC=%lu LEC=%lu\r\n",
               g_isotp_tx_fail_count, isotp_tx_kind_name(diag.kind),
               diag.frame0, diag.frame1, diag.frame2,
               diag.free_level, diag.received, diag.total_len, diag.next_sn,
               (unsigned long)diag.esr, (unsigned long)diag.tsr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_tx_fail = g_isotp_tx_fail_count;
    }

    if (g_isotp_rx_abort_count != s_last_rx_abort) {
        ISOTP_RxAbortDiag_t diag;
        isotp_diag_get_rx_abort(&diag);
        printf("[ISOTP RX ABORT] count=%u reason=%s got=%u expected=%u"
               " rx=%u/%u\r\n",
               g_isotp_rx_abort_count, isotp_rx_abort_name(diag.reason),
               diag.got_sn, diag.expected_sn, diag.received, diag.total_len);
        s_last_rx_abort = g_isotp_rx_abort_count;
    }

    if (s_hb_tx_fail_count != s_last_hb_tx_fail) {
        CAN_TxFailDiag_t diag;
        copy_snapshot(&diag, &s_hb_tx_fail_diag, sizeof(diag));
        printf("[CAN TX FAIL][HB] count=%u free=%u ESR=0x%08lX TSR=0x%08lX"
               " err=0x%08lX TEC=%lu REC=%lu LEC=%lu\r\n",
               s_hb_tx_fail_count, diag.free_level,
               (unsigned long)diag.esr, (unsigned long)diag.tsr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_hb_tx_fail = s_hb_tx_fail_count;
    }

    if (s_obs_tx_fail_count != s_last_obs_tx_fail) {
        CAN_TxFailDiag_t diag;
        copy_snapshot(&diag, &s_obs_tx_fail_diag, sizeof(diag));
        printf("[CAN TX FAIL][OBS] count=%u free=%u ESR=0x%08lX TSR=0x%08lX"
               " err=0x%08lX TEC=%lu REC=%lu LEC=%lu\r\n",
               s_obs_tx_fail_count, diag.free_level,
               (unsigned long)diag.esr, (unsigned long)diag.tsr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_obs_tx_fail = s_obs_tx_fail_count;
    }

    if (s_can_rx0_full_count != s_last_rx0_full) {
        CAN_RxDiag_t diag;
        copy_snapshot(&diag, &s_can_rx0_full_diag, sizeof(diag));
        printf("[CAN RX FIFO0 FULL] count=%u fill=%u ESR=0x%08lX err=0x%08lX"
               " TEC=%lu REC=%lu LEC=%lu\r\n",
               s_can_rx0_full_count, diag.fifo_fill,
               (unsigned long)diag.esr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_rx0_full = s_can_rx0_full_count;
    }

    if (s_can_rx0_overrun_count != s_last_rx0_overrun) {
        CAN_RxDiag_t diag;
        copy_snapshot(&diag, &s_can_rx0_overrun_diag, sizeof(diag));
        printf("[CAN RX FIFO0 OVERRUN] count=%u fill=%u ESR=0x%08lX err=0x%08lX"
               " TEC=%lu REC=%lu LEC=%lu\r\n",
               s_can_rx0_overrun_count, diag.fifo_fill,
               (unsigned long)diag.esr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_rx0_overrun = s_can_rx0_overrun_count;
    }

    if (s_can_error_count != s_last_can_error) {
        CAN_ErrorDiag_t diag;
        copy_snapshot(&diag, &s_can_error_diag, sizeof(diag));
        printf("[CAN ERROR] count=%u fill=%u ESR=0x%08lX TSR=0x%08lX err=0x%08lX"
               " TEC=%lu REC=%lu LEC=%lu\r\n",
               s_can_error_count, diag.fifo_fill,
               (unsigned long)diag.esr, (unsigned long)diag.tsr, (unsigned long)diag.error_code,
               (unsigned long)can_esr_tec(diag.esr),
               (unsigned long)can_esr_rec(diag.esr),
               (unsigned long)can_esr_lec(diag.esr));
        s_last_can_error = s_can_error_count;
    }

    if (HAL_GetTick() - s_measure_tick >= 50) {
      s_measure_tick = HAL_GetTick();
      uint16_t dist  = hcsr04_measure_cm();
      s_distance_cm  = dist;
      printf("[DIST] %u cm\r\n", dist);

      s_obstacle = (dist > 0 && dist <= 10) ? 1 : 0;

      uint8_t obs_data[8] = {s_obstacle,
                             (uint8_t)(dist >> 8), (uint8_t)dist,
                             0, 0, 0, 0, 0};
      uint32_t mb;
      if (HAL_CAN_AddTxMessage(&hcan1, &obs_header, obs_data, &mb) != HAL_OK) {
          capture_can_tx_fail(&s_obs_tx_fail_diag, &s_obs_tx_fail_count);
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 9;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_8TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 999;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 89;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 8999;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(HCSR_TRIG_GPIO_Port, HCSR_TRIG_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : HCSR_TRIG_Pin */
  GPIO_InitStruct.Pin = HCSR_TRIG_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HCSR_TRIG_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : HCSR_ECHO_Pin */
  GPIO_InitStruct.Pin = HCSR_ECHO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HCSR_ECHO_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM3) return;

    tx_data[0] = APP_VERSION;
    tx_data[1] = ota_get_active_slot();
    tx_data[2] = (uint8_t)(s_distance_cm >> 8);
    tx_data[3] = (uint8_t)s_distance_cm;
    tx_data[4] = s_obstacle;
    tx_data[5] = 0;
    tx_data[6] = 0;
    tx_data[7] = 0;

    if (HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &tx_mailbox) == HAL_OK) {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        s_hb_sent = 1;   /* self-test 항목: CAN 스택 생존 증명 */
    } else {
        capture_can_tx_fail(&s_hb_tx_fail_diag, &s_hb_tx_fail_count);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
    if (rx_header.StdId == ISOTP_RX_CAN_ID) {
        isotp_can_rx(rx_data, (uint8_t)rx_header.DLC);
    }
    /* Non-ISOTP frames are silently dropped.
     * printf() here would block ~4ms (HAL_UART_Transmit with HAL_MAX_DELAY),
     * causing RX FIFO overflow and ISO-TP SN mismatches during OTA. */
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *hcan)
{
    (void)hcan;
    capture_can_rx_diag(&s_can_rx0_full_diag, &s_can_rx0_full_count);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    capture_can_error_diag(hcan);

    if ((hcan->ErrorCode & HAL_CAN_ERROR_RX_FOV0) != 0U) {
        capture_can_rx_diag(&s_can_rx0_overrun_diag, &s_can_rx0_overrun_count);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
