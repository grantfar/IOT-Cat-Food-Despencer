#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <hd44780.h>
#include <pcf8574.h>
#include <encoder.h>
#include <string.h>
#include <esp_log.h>

#define SDA_GPIO 16
#define SCL_GPIO 17
#define I2C_ADDR 0x27

const char * options[5] = {"Test","Grant","BLE Provision","Despence Food", "User"};
static i2c_dev_t pcf8574;

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}


void lcdChange(unsigned char val, hd44780_t * lcd){
    hd44780_clear(lcd);
    hd44780_puts(lcd,options[val]);
}

void lcd_init(hd44780_t * lcd)
{
    (*lcd).write_cb = write_lcd_data;
    (*lcd).font = HD44780_FONT_5X8;
    (*lcd).lines = 2;
    (*lcd).pins.rs = 0;
    (*lcd).pins.e  = 2;
    (*lcd).pins.d4 = 4;
    (*lcd).pins.d5 = 5;
    (*lcd).pins.d6 = 6;
    (*lcd).pins.d7 = 7;
    (*lcd).pins.bl = 3;

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, 0, I2C_ADDR, SDA_GPIO, SCL_GPIO));

    ESP_ERROR_CHECK(hd44780_init(lcd));

    hd44780_switch_backlight(lcd, true);
}


void app_main()
{
    ESP_ERROR_CHECK(i2cdev_init());
    
    hd44780_t lcd; 
    QueueHandle_t encoderQueue = xQueueCreate(5,sizeof(rotary_encoder_event_t));
    rotary_encoder_event_t eventBuff;
    rotary_encoder_t encoderS = {
        .pin_a = 34,
        .pin_b = 35,
        .pin_btn = 32,
    };
    
    ESP_ERROR_CHECK(rotary_encoder_init(encoderQueue));
    ESP_ERROR_CHECK(rotary_encoder_add(&encoderS));
    lcd_init(&lcd);
    char menuVal = 0;
    lcdChange(menuVal,&lcd);
    while (1)
    {
        xQueueReceive(encoderQueue,&eventBuff,portMAX_DELAY);
        switch (eventBuff.type)
        {
        case RE_ET_CHANGED:
            ESP_LOGI("Debug","%i",eventBuff.diff);
            if(eventBuff.diff > 0){
                menuVal = (menuVal + 1) % 6;
            }
            else if(eventBuff.diff < 0){
                menuVal = (menuVal + 5)%6;
            }
            lcdChange(menuVal,&lcd);    
            break;
        
        default:
            break;
        }
    }
    
}