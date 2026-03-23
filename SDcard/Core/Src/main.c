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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdarg.h> //for va_list var arg functions

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define BUFFERSIZE 256
//#define POLLING_TIME 30
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint32_t button1 = 0;// button 1 counter
uint32_t button2 = 0;// button 2 counter
//int poll_time = 30;// 30 ms

uint32_t debounceDelay = 30;

// Button 1
GPIO_PinState btn1_lastReading = GPIO_PIN_RESET;
GPIO_PinState btn1_stableState = GPIO_PIN_RESET;
uint32_t btn1_lastDebounceTime = 0;

// Button 2
GPIO_PinState btn2_lastReading = GPIO_PIN_RESET;
GPIO_PinState btn2_stableState = GPIO_PIN_RESET;
uint32_t btn2_lastDebounceTime = 0;

//data to read
typedef struct {
    uint32_t record;
    float vdc;
    float idc;
    float power;
    float vrms;
    float irms;
    float freq;
    float phase;
    float real_power;
    float apparent_power;
    float reactive_power;
    float pf;
    float vptp;
    float iptp;
    float vTHD;
    float iTHD;
} Measurement_t;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void myprintf(const char *fmt, ...);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void myprintf(const char *fmt, ...) {
  static char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  int len = strlen(buffer);
  HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, -1);

}

// Write a string to an already-open CSV file
void writeCSV(FIL* fil, char* content) {
    FRESULT fres;
    UINT bytesWrote;

    fres = f_write(fil, content, strlen(content), &bytesWrote);
    if (fres == FR_OK) {
        myprintf("Wrote %u bytes to 'log.csv'!\r\n", bytesWrote);
    } else {
        myprintf("f_write error to csv (%i)\r\n", fres);
    }
}

// Log one measurement row to log.csv
 void LogMeasurement(
         uint32_t record,
         float vdc,
         float idc,
         float power,
         float vrms,
         float irms,
         float freq,
         float phase,
         float real_power,
         float apparent_power,
         float reactive_power,
         float pf,
         float vptp,
         float iptp,
         float vTHD,
         float iTHD
 ) {
     FIL fil;
     FRESULT fres;
     char writeBuffer[BUFFERSIZE];

     myprintf("\r\n~ now writing ~\r\n\r\n");

     // Open file for append, create if needed
     fres = f_open(&fil, "log.csv", FA_OPEN_ALWAYS | FA_WRITE);
     if (fres != FR_OK) {
         myprintf("f_open error for log.csv (%i)\r\n", fres);
         return;
     }

     // Move to end of file
     fres = f_lseek(&fil, f_size(&fil));
     if (fres != FR_OK) {
         myprintf("f_lseek error (%i)\r\n", fres);
         f_close(&fil);
         return;
     }

     // If file is empty, write header first
     if (f_size(&fil) == 0) {
         snprintf(writeBuffer, BUFFERSIZE,
                  "record,vdc,idc,power,vrms,irms,freq,phase,real_power,apparent_power,reactive_power,pf,vptp,iptp,vTHD,iTHD\r\n");
         writeCSV(&fil, writeBuffer);
     }

     // Write one measurement row
     snprintf(writeBuffer, BUFFERSIZE,
              "%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\r\n",
		        record,
		        vdc,// DC voltage (in V)
		        idc,// DC current (in A
				power, // DC power (in W)
		        vrms,// AC voltage (in V)
		        irms,// AC current (in A)
				freq, // AC frequency (in Hz)
				phase,//AC phase difference between voltage and current waveform (in degrees)
		        real_power,//AC real power (in W)
		        apparent_power,// AC reactive power (in VAR)
		        reactive_power,//AC apparent power (in VA)
		        pf,//AC power factor (unitless, between 0 and 1)
		        vptp,//AC peak-to-peak voltage (in V, not RMS)
		        iptp,//AC peak-to-peak current (in A, not RMS)
		        vTHD,//Total Harmonic Distortion (THD) of voltage (in %)
		        iTHD //THD of current (in %)
				);

     writeCSV(&fil, writeBuffer);

     f_sync(&fil);
     f_close(&fil);
 }

//read the last measurment taken
void readMeasurment(){
    FIL fil;
    FRESULT fres;
    char line[BUFFERSIZE];
    char lastLine[BUFFERSIZE] = {0};

    myprintf("\r\n~ now reading ~\r\n\r\n");

    // open file
    fres = f_open(&fil, "log.csv", FA_READ);
    if (fres != FR_OK) {
        myprintf("f_open error (%i) on reading\r\n", fres);
        return;
    }

    // read every line
    while (f_gets(line, sizeof(line), &fil) != NULL) {

        // skip header
        if (strncmp(line, "record,", 7) == 0)
            continue;

        // skip empty lines
        if (line[0] == '\r' || line[0] == '\n' || line[0] == '\0')
            continue;

        // store latest line
        strncpy(lastLine, line, sizeof(lastLine)-1);
        lastLine[sizeof(lastLine)-1] = '\0';
    }

    f_close(&fil);

    if (lastLine[0] != '\0') {
        myprintf("Latest measurement: %s", lastLine);
    } else {
        myprintf("No measurement data found\r\n");
    }

}

 //This function reads and returns the lastest measurment logged on the SD card


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

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
  MX_SPI3_Init();
  MX_FATFS_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  //This function write a single measurment to a csv file

  //HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET);//test: LED turn on

  myprintf("\r\n~ SD card start ~\r\n\r\n");

  HAL_Delay(1000); //delay to let SD card settle

  //FatFS variables

  FATFS FatFs; 	//Fatfs handle
  FIL fil; // file handle
  FRESULT fres;// result after operation
  static int record = 1;// keep track of the record

  UINT bytesWritten;

  //try to mount the SD card
  fres = f_mount(&FatFs, "", 1); //1=mount now
  if (fres != FR_OK) {
	 myprintf("f_mount error (%i)\r\n", fres);

	  //mount failed
	while(1);
  }

  //Let's get some statistics from the SD card
  DWORD free_clusters, free_sectors, total_sectors;

  FATFS* getFreeFs;

  fres = f_getfree("", &free_clusters, &getFreeFs);
  if (fres != FR_OK) {
	myprintf("f_getfree error (%i)\r\n", fres);
	while(1);
  }

  //Formula comes from ChaN's documentation
  total_sectors = (getFreeFs->n_fatent - 2) * getFreeFs->csize;
  free_sectors = free_clusters * getFreeFs->csize;

  myprintf("SD card stats:\r\n%10lu KiB total drive space.\r\n%10lu KiB available.\r\n", total_sectors / 2, free_sectors / 2);


  //TEST:

  //test writing code to .csv
  /**
   *    LogMeasurement(record, 12.400f, 1.250f, 15.500f,
                  240.100f, 0.850f, 50.000f, 12.300f,
                  180.000f, 204.000f, 96.000f, 0.882f,
                  679.000f, 2.400f, 3.100f, 4.200f);

   record++;// increment the record

   * **/

  //test reading from .csv
  readMeasurment();
  HAL_Delay(1000);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint32_t now = HAL_GetTick();

	     //button 1
	     GPIO_PinState btn1_reading = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3);//read current signal

	     if (btn1_reading != btn1_lastReading)// button pressed
	     {
	         btn1_lastDebounceTime = now;//get current input
	         btn1_lastReading = btn1_reading;//get current input
	     }

	     if ((now - btn1_lastDebounceTime) >= debounceDelay)//check button after 30 ms
	     {
	         if (btn1_stableState != btn1_reading)//confirm state of the button
	         {
	             btn1_stableState = btn1_reading;//get new button press value

	             // button press event 1
	             if (btn1_stableState == GPIO_PIN_SET)
	             {
	                 HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
	             }
	         }
	     }

	     //button 2
	     GPIO_PinState btn2_reading = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);//read current signal

	     if (btn2_reading != btn2_lastReading)// button pressed
	     {
	         btn2_lastDebounceTime = now;//get current input
	         btn2_lastReading = btn2_reading;//get current input
	     }

	     if ((now - btn2_lastDebounceTime) >= debounceDelay)//check button after 30 ms
	     {
	         if (btn2_stableState != btn2_reading)//confirm state of the button
	         {
	             btn2_stableState = btn2_reading;//get new button press value

	             // button press event  2
	             if (btn2_stableState == GPIO_PIN_SET)
	             {
	                 HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_6);
	             }
	         }

	    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 7;
  hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

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
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, pb_out_1_Pin|pb_out_2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : pb_1_Pin */
  GPIO_InitStruct.Pin = pb_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(pb_1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : pb_2_Pin */
  GPIO_InitStruct.Pin = pb_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(pb_2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : pb_out_1_Pin pb_out_2_Pin */
  GPIO_InitStruct.Pin = pb_out_1_Pin|pb_out_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
