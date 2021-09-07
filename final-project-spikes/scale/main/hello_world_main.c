#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hx711.h>


#define PD_SCK_GPIO 18
#define DOUT_GPIO   19
#define ZERO_VAL 12089
#define VAL_TO_G 0.04452875399361
static hx711_t foodScale;

esp_err_t initScale(){
    foodScale.dout = DOUT_GPIO;
    foodScale.pd_sck = PD_SCK_GPIO;
    foodScale.gain = HX711_GAIN_A_64;
    esp_err_t r;
    // initialize device
    for(int i = 0; i < 10;i++)
    {
        r = hx711_init(&foodScale);
        if (r == ESP_OK){
            break;
        }
        printf("Could not initialize Scale: %d (%s)\n", r, esp_err_to_name(r));
        for(int i = 0; i < 10000; i++){
        }
    }
    return r;
}

void readScale(void *pvParameters)
{
    int32_t data = 0;
    int32_t averageL1 = 0;
    while (1)
    {
        
        for(int i = 0; i<10; i++){
            esp_err_t r = hx711_wait(&foodScale, 500);
            if (r != ESP_OK)
            {
                printf("Device not found: %d (%s)\n", r, esp_err_to_name(r));
                continue;
            }

            
            r = hx711_read_data(&foodScale, &data);
            if (r != ESP_OK)
            {
                printf("Could not read data: %d (%s)\n", r, esp_err_to_name(r));
                continue;
            }
            averageL1 = (averageL1 * (i/(i+1))) +(data/(i+1)); 
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        printf("average: %i\n",(int)((average - ZERO_VAL)*VAL_TO_G));
    }

}

void app_main()
{
    ESP_ERROR_CHECK(initScale());
    xTaskCreate(readScale, "readScale", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);
}
