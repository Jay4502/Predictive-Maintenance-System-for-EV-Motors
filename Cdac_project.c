#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "adc.h"
#include "gpio.h"
#include "stdio.h"
#include "math.h"
#include "string.h"

// MPU6050 Definitions
#define MPU6050_ADDR         (0x68 << 1)   // MPU6050 device address (shifted for HAL)
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

uint16_t adc_val = 0;
float temperature_c = 0.0f;

uint8_t MPU6050_Data[6];
int16_t acc_x, acc_y, acc_z;

char uart_buf[100];

// --------------------------- //
//  MPU6050 Initialization    //
// --------------------------- //
void MPU6050_Init(void)
{
    uint8_t data = 0;
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, MPU6050_PWR_MGMT_1, 1, &data, 1, HAL_MAX_DELAY);
}

// --------------------------- //
// Read Accelerometer          //
// --------------------------- //
void MPU6050_Read_Accel(void)
{
    HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, 1, MPU6050_Data, 6, HAL_MAX_DELAY);

    acc_x = (int16_t)(MPU6050_Data[0] << 8 | MPU6050_Data[1]);
    acc_y = (int16_t)(MPU6050_Data[2] << 8 | MPU6050_Data[3]);
    acc_z = (int16_t)(MPU6050_Data[4] << 8 | MPU6050_Data[5]);
}

// ---------------------------------------------- //
// Convert ADC value from Thermistor to Celsius   //
// ---------------------------------------------- //
float Convert_ADC_To_Temperature(uint16_t adc_val)
{
    float R_ntc, Temp;
    float R_fixed = 10000.0f;
    float Beta = 3950.0f;
    float T0 = 298.15f;
    float R0 = 10000.0f;

    R_ntc = R_fixed * ((4095.0f / adc_val) - 1.0f);

    // Steinhart Equation
    Temp = 1.0f / ((log(R_ntc / R0) / Beta) + (1.0f / T0));
    Temp = Temp - 273.15f;  // Convert from Kelvin to Celsius

    return Temp;
}

// ===================================== //
//             MAIN FUNCTION             //
// ===================================== //
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();

    HAL_Delay(500);
    MPU6050_Init();  // Wake up the MPU6050

    while (1)
    {
        // --- Read Thermistor via ADC ---
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        adc_val = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);

        // --- Convert to Temperature
        temperature_c = Convert_ADC_To_Temperature(adc_val);

        // --- Read MPU6050 Accelerometer
        MPU6050_Read_Accel();

        // --- Send to Serial (UART)
        snprintf(uart_buf, sizeof(uart_buf),
                 "Temp: %.2f°C | Acc X:%d Y:%d Z:%d\r\n",
                 temperature_c, acc_x, acc_y, acc_z);

        HAL_UART_Transmit(&huart2, (uint8_t*) uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

        HAL_Delay(500);  // Delay for readability
    }
}
