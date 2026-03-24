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

uint8_t isRecording = 0;
char currentLogFile[32] = {0};

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
void writeCSV(FIL* fil, const char* content) {
    FRESULT fres;
    UINT bytesWrote;

    fres = f_write(fil, content, strlen(content), &bytesWrote);
    if (fres == FR_OK) {
        myprintf("Wrote %u bytes\r\n", bytesWrote);
    } else {
        myprintf("f_write error to csv (%i)\r\n", fres);
    }
}
// Create a new file for a new recording session
void StartRecording(void) {
    FIL fil;
    FRESULT fres;
    char writeBuffer[BUFFERSIZE];

    if (isRecording) {
        myprintf("Already recording to %s\r\n", currentLogFile);
        return;
    }

    // Find the next free filename: LOG000.CSV, LOG001.CSV, ...
    for (int i = 0; i < 1000; i++) {
        snprintf(currentLogFile, sizeof(currentLogFile), "LOG%03d.CSV", i);

        FILINFO fno;
        fres = f_stat(currentLogFile, &fno);

        if (fres == FR_NO_FILE) {
            // File does not exist yet, use this one
            break;
        }

        if (i == 999) {
            myprintf("No free log filename found\r\n");
            currentLogFile[0] = '\0';
            return;
        }
    }

    // Create new file
    fres = f_open(&fil, currentLogFile, FA_CREATE_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        myprintf("f_open error creating %s (%i)\r\n", currentLogFile, fres);
        currentLogFile[0] = '\0';
        return;
    }

    // Write CSV header
    snprintf(writeBuffer, BUFFERSIZE,
             "vdc,idc,power,vrms,irms,freq,phase,real_power,apparent_power,reactive_power,pf,vptp,iptp,vTHD,iTHD\r\n");
    writeCSV(&fil, writeBuffer);

    f_sync(&fil);
    f_close(&fil);

    isRecording = 1;
    myprintf("Started recording: %s\r\n", currentLogFile);
}

// Stop the current recording session
void StopRecording(void) {
    if (!isRecording) {
        myprintf("Recording already stopped\r\n");
        return;
    }

    myprintf("Stopped recording: %s\r\n", currentLogFile);
    isRecording = 0;
}
// Log one measurement row to current session CSV
void LogMeasurement(
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
    int len;

    if (!isRecording) {
        myprintf("Not recording, measurement not saved\r\n");
        return;
    }

    if (currentLogFile[0] == '\0') {
        myprintf("No active log file\r\n");
        return;
    }

    myprintf("\r\n~ now writing to %s ~\r\n\r\n", currentLogFile);

    fres = f_open(&fil, currentLogFile, FA_OPEN_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        myprintf("f_open error for %s (%i)\r\n", currentLogFile, fres);
        return;
    }

    fres = f_lseek(&fil, f_size(&fil));
    if (fres != FR_OK) {
        myprintf("f_lseek error (%i)\r\n", fres);
        f_close(&fil);
        return;
    }

    len = snprintf(writeBuffer, sizeof(writeBuffer),
             "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\r\n",
             vdc,
             idc,
             power,
             vrms,
             irms,
             freq,
             phase,
             real_power,
             apparent_power,
             reactive_power,
             pf,
             vptp,
             iptp,
             vTHD,
             iTHD);

    myprintf("formatted length = %d\r\n", len);
    myprintf("row = %s", writeBuffer);

    if (len <= 0) {
        myprintf("snprintf failed\r\n");
        f_close(&fil);
        return;
    }

    writeCSV(&fil, writeBuffer);

    f_sync(&fil);
    f_close(&fil);
}


//read the last measurment taken
void readMeasurment(void) {
    FIL fil;
    FRESULT fres;
    char line[BUFFERSIZE];
    char lastLine[BUFFERSIZE] = {0};

    if (currentLogFile[0] == '\0') {
        myprintf("No log file selected\r\n");
        return;
    }

    myprintf("\r\n~ now reading %s ~\r\n\r\n", currentLogFile);

    fres = f_open(&fil, currentLogFile, FA_READ);
    if (fres != FR_OK) {
        myprintf("f_open error (%i) on reading\r\n", fres);
        return;
    }

    while (f_gets(line, sizeof(line), &fil) != NULL) {
        // skip header
        if (strncmp(line, "vdc,", 4) == 0)
            continue;

        // skip empty lines
        if (line[0] == '\r' || line[0] == '\n' || line[0] == '\0')
            continue;

        strncpy(lastLine, line, sizeof(lastLine) - 1);
        lastLine[sizeof(lastLine) - 1] = '\0';
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

  myprintf("\r\n--- TEST SESSION 1 START ---\r\n");
  StartRecording();

  LogMeasurement(
      12.400f, 1.250f, 15.500f,
      240.100f, 0.850f, 50.000f, 12.300f,
      180.000f, 204.000f, 96.000f, 0.882f,
      679.000f, 2.400f, 3.100f, 4.200f
  );
  HAL_Delay(500);

  LogMeasurement(
      12.600f, 1.300f, 16.380f,
      239.800f, 0.900f, 50.000f, 11.800f,
      181.000f, 205.000f, 97.000f, 0.884f,
      680.000f, 2.500f, 3.000f, 4.100f
  );
  HAL_Delay(500);

  LogMeasurement(
      12.800f, 1.350f, 17.280f,
      240.300f, 0.920f, 50.000f, 12.100f,
      182.000f, 206.000f, 98.000f, 0.886f,
      681.000f, 2.600f, 2.900f, 4.000f
  );
  HAL_Delay(500);

  StopRecording();
  myprintf("--- TEST SESSION 1 STOP ---\r\n");
  HAL_Delay(1000);


  // TEST: session 2
  myprintf("\r\n--- TEST SESSION 2 START ---\r\n");
  StartRecording();

  LogMeasurement(
      13.100f, 1.400f, 18.340f,
      241.000f, 0.950f, 50.000f, 12.500f,
      183.000f, 207.000f, 99.000f, 0.890f,
      682.000f, 2.700f, 2.800f, 3.900f
  );
  HAL_Delay(500);

  LogMeasurement(
      13.300f, 1.450f, 19.285f,
      241.200f, 0.980f, 50.000f, 12.700f,
      184.000f, 208.000f, 100.000f, 0.892f,
      683.000f, 2.800f, 2.700f, 3.800f
  );
  HAL_Delay(500);

  StopRecording();
  myprintf("--- TEST SESSION 2 STOP ---\r\n");
  HAL_Delay(1000);


  // Read latest measurement from current file
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
