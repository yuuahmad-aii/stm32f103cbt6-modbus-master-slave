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
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SLAVE_ID 0x01
#define RX_BUF_SIZE 16
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;
DMA_HandleTypeDef hdma_usart3_tx;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */

uint8_t tx_buf[16];
uint8_t rx_buf[RX_BUF_SIZE];
volatile uint8_t rx_complete_flag = 0;
volatile uint16_t rx_length = 0;

// State Machine Variables
typedef enum {
	STATE_IDLE,
	STATE_SEND_FC05,
	STATE_WAIT_FC05,
	STATE_SEND_FC02,
	STATE_DELAY_FC02, // <-- TAMBAHAN: State untuk jeda antar frame
	STATE_WAIT_FC02
} MasterState_t;

MasterState_t modbus_state = STATE_IDLE;
uint32_t state_timer = 0;

uint8_t my_btn_state = 0;
uint8_t slave_btn_state = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USB_PCD_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

uint16_t Modbus_CRC16(uint8_t *buf, uint8_t len) {
	uint16_t crc = 0xFFFF;
	for (int pos = 0; pos < len; pos++) {
		crc ^= (uint16_t) buf[pos];
		for (int i = 8; i != 0; i--) {
			if ((crc & 0x0001) != 0) {
				crc >>= 1;
				crc ^= 0xA001;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc;
}

// Fungsi untuk memulai transmisi DMA
void RS485_Send_DMA(uint8_t *data, uint16_t len) {
	HAL_GPIO_WritePin(USART3_SEL_GPIO_Port, USART3_SEL_Pin, GPIO_PIN_SET); // DE/RE HIGH (Transmit)
	// Beri sedikit jeda agar IC MAX485 siap (biasanya hitungan nanosecond, tapi aman pakai instruksi NOP)
	__NOP();
	__NOP();
	__NOP();
	HAL_UART_Transmit_DMA(&huart3, data, len);
}

// Callback saat pengiriman (TX) selesai dari DMA
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART3) {
		HAL_GPIO_WritePin(USART3_SEL_GPIO_Port, USART3_SEL_Pin, GPIO_PIN_RESET); // DE/RE LOW (Receive)
		// Mulai dengarkan balasan
		HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_buf, RX_BUF_SIZE);
	}
}

// Callback saat penerimaan (RX) mendeteksi garis IDLE (Selesai menerima 1 frame Modbus)
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	if (huart->Instance == USART3) {
		rx_length = Size;
		rx_complete_flag = 1;
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART3) {
		// Jika terjadi error pada jalur RS485, bersihkan error dan mulai dengar ulang
		HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_buf, RX_BUF_SIZE);
	}
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

	/* USER CODE BEGIN 1 */

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
	MX_DMA_Init();
	MX_ADC1_Init();
	MX_USART3_UART_Init();
	MX_USB_PCD_Init();
	/* USER CODE BEGIN 2 */
	HAL_GPIO_WritePin(USART3_SEL_GPIO_Port, USART3_SEL_Pin, GPIO_PIN_RESET); // Mode Receive Awal
	uint16_t crc;
	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {

		// Baca status tombol lokal Master (Aktif low)
		my_btn_state =
				(HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin)
						== GPIO_PIN_RESET) ? 1 : 0;

		// FSM (Finite State Machine) untuk Master tanpa Delay
		switch (modbus_state) {

		case STATE_IDLE:
			if (HAL_GetTick() - state_timer > 100) { // Kirim data setiap 100ms
				modbus_state = STATE_SEND_FC05;
			}
			break;

		case STATE_SEND_FC05: // Minta Slave mengubah LED-nya
			memset((void*) rx_buf, 0, RX_BUF_SIZE); // <-- PENTING: Bersihkan buffer
			rx_complete_flag = 0; // Reset flag penerimaan
			tx_buf[0] = SLAVE_ID;
			tx_buf[1] = 0x05;
			tx_buf[2] = 0x00;
			tx_buf[3] = 0x00;
			tx_buf[4] = my_btn_state ? 0xFF : 0x00;
			tx_buf[5] = 0x00;
			crc = Modbus_CRC16(tx_buf, 6);
			tx_buf[6] = crc & 0xFF;
			tx_buf[7] = (crc >> 8) & 0xFF;

			RS485_Send_DMA(tx_buf, 8);
			state_timer = HAL_GetTick(); // Catat waktu pengiriman
			modbus_state = STATE_WAIT_FC05;
			break;

		case STATE_WAIT_FC05:
			if (rx_complete_flag) {
				// Berhasil menerima balasan (opsional bisa di-cek CRC-nya jika perlu)
//				modbus_state = STATE_SEND_FC02; // Lanjut baca tombol slave
				state_timer = HAL_GetTick();
				modbus_state = STATE_DELAY_FC02; // <-- UBAH: Masuk ke state jeda dulu
			} else if (HAL_GetTick() - state_timer > 50) {
				// Timeout 50ms jika Slave mati
//				modbus_state = STATE_SEND_FC02; // Tetap lanjut ke state berikutnya
				modbus_state = STATE_DELAY_FC02; // <-- UBAH: Tetap masuk ke state jeda
			}
			break;

		case STATE_DELAY_FC02:
			if (HAL_GetTick() - state_timer > 5) { // Jeda aman 5ms
				modbus_state = STATE_SEND_FC02;
			}
			break;

		case STATE_SEND_FC02: // Minta status tombol Slave
			memset((void*)rx_buf, 0, RX_BUF_SIZE); // <-- PENTING: Bersihkan buffer
			rx_complete_flag = 0;
			tx_buf[0] = SLAVE_ID;
			tx_buf[1] = 0x02;
			tx_buf[2] = 0x00;
			tx_buf[3] = 0x00;
			tx_buf[4] = 0x00;
			tx_buf[5] = 0x01;
			crc = Modbus_CRC16(tx_buf, 6);
			tx_buf[6] = crc & 0xFF;
			tx_buf[7] = (crc >> 8) & 0xFF;

			RS485_Send_DMA(tx_buf, 8);
			state_timer = HAL_GetTick();
			modbus_state = STATE_WAIT_FC02;
			break;

		case STATE_WAIT_FC02:
			if (rx_complete_flag) {
				// Validasi Frame Modbus FC02
				if (rx_length >= 6 && rx_buf[0] == SLAVE_ID
						&& rx_buf[1] == 0x02) {
					if (Modbus_CRC16(rx_buf, rx_length - 2)
							== (rx_buf[rx_length - 2]
									| (rx_buf[rx_length - 1] << 8))) {
						slave_btn_state = rx_buf[3]; // Ambil data status tombol

						// Update LED Master
						if (slave_btn_state)
							HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin,
									GPIO_PIN_SET);
						else
							HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin,
									GPIO_PIN_RESET);
					}
				}
				state_timer = HAL_GetTick();
				modbus_state = STATE_IDLE; // Kembali ke awal
			} else if (HAL_GetTick() - state_timer > 50) {
				// Timeout
				state_timer = HAL_GetTick();
				modbus_state = STATE_IDLE;
			}
			break;
		}

		// Anda dapat menambahkan kode lain di sini. Program utama ini 100% Non-Blocking!

		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };
	RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC | RCC_PERIPHCLK_USB;
	PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
	PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
		Error_Handler();
	}
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void) {

	/* USER CODE BEGIN ADC1_Init 0 */

	/* USER CODE END ADC1_Init 0 */

	ADC_ChannelConfTypeDef sConfig = { 0 };

	/* USER CODE BEGIN ADC1_Init 1 */

	/* USER CODE END ADC1_Init 1 */

	/** Common config
	 */
	hadc1.Instance = ADC1;
	hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
	hadc1.Init.ContinuousConvMode = DISABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion = 1;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) {
		Error_Handler();
	}

	/** Configure Regular Channel
	 */
	sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN ADC1_Init 2 */

	/* USER CODE END ADC1_Init 2 */

}

/**
 * @brief USART3 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART3_UART_Init(void) {

	/* USER CODE BEGIN USART3_Init 0 */

	/* USER CODE END USART3_Init 0 */

	/* USER CODE BEGIN USART3_Init 1 */

	/* USER CODE END USART3_Init 1 */
	huart3.Instance = USART3;
	huart3.Init.BaudRate = 115200;
	huart3.Init.WordLength = UART_WORDLENGTH_8B;
	huart3.Init.StopBits = UART_STOPBITS_1;
	huart3.Init.Parity = UART_PARITY_NONE;
	huart3.Init.Mode = UART_MODE_TX_RX;
	huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart3.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart3) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USART3_Init 2 */

	/* USER CODE END USART3_Init 2 */

}

/**
 * @brief USB Initialization Function
 * @param None
 * @retval None
 */
static void MX_USB_PCD_Init(void) {

	/* USER CODE BEGIN USB_Init 0 */

	/* USER CODE END USB_Init 0 */

	/* USER CODE BEGIN USB_Init 1 */

	/* USER CODE END USB_Init 1 */
	hpcd_USB_FS.Instance = USB;
	hpcd_USB_FS.Init.dev_endpoints = 8;
	hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
	hpcd_USB_FS.Init.low_power_enable = DISABLE;
	hpcd_USB_FS.Init.lpm_enable = DISABLE;
	hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
	if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USB_Init 2 */

	/* USER CODE END USB_Init 2 */

}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

	/* DMA controller clock enable */
	__HAL_RCC_DMA1_CLK_ENABLE();

	/* DMA interrupt init */
	/* DMA1_Channel2_IRQn interrupt configuration */
	HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
	/* DMA1_Channel3_IRQn interrupt configuration */
	HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOB, USART3_SEL_Pin | USER_LED_Pin, GPIO_PIN_RESET);

	/*Configure GPIO pin : USER_BTN_Pin */
	GPIO_InitStruct.Pin = USER_BTN_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(USER_BTN_GPIO_Port, &GPIO_InitStruct);

	/*Configure GPIO pins : USART3_SEL_Pin USER_LED_Pin */
	GPIO_InitStruct.Pin = USART3_SEL_Pin | USER_LED_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
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
