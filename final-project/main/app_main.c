#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <mqtt_client.h>
#include <sys/time.h>
#include <hd44780.h>
#include <pcf8574.h>
#include <encoder.h>
#include <driver/ledc.h>
#include <hx711.h>
#include <esp_sntp.h>


#define SDA_GPIO 16
#define SCL_GPIO 17
#define I2C_ADDR 0x27
#define PD_SCK_GPIO 18
#define DOUT_GPIO   19
#define ZERO_VAL 12089
#define VAL_TO_G 0.04452875399361
#define ESN "1234567890"
#define menuCount 3
#define SSL_CERT "-----BEGIN CERTIFICATE-----\nMIIDtTCCAp2gAwIBAgIUENYncH7x8MUH9UCw4fSwh2cB4GEwDQYJKoZIhvcNAQEL\nBQAwajELMAkGA1UEBhMCVVMxETAPBgNVBAgMCE1pY2hpZ2FuMRIwEAYDVQQHDAlL\nYWxhbWF6b28xHDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBMdGQxFjAUBgNVBAMM\nDWdyYW50ZmFyLnNpdGUwHhcNMjEwNDIyMjAwMzUzWhcNMjIwNDIyMjAwMzUzWjBq\nMQswCQYDVQQGEwJVUzERMA8GA1UECAwITWljaGlnYW4xEjAQBgNVBAcMCUthbGFt\nYXpvbzEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEWMBQGA1UEAwwNZ3Jh\nbnRmYXIuc2l0ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKK3ac0I\nIIXvh5/tuMo1l+95jokyBFA6MwzMqCFcNffIruUmuL3K3TmTf4+OixhdRinXiz00\neSO9uCVyHVnoO3EVHHjo4ZKOHMB9RRyHU3m4OUlEc+SA3/WHb87iwWYSOyrc/mYT\nuBlyx+lpC4CV+9MbiCKCS+ArVYYXKlLLTYpVEfq+MEJdOCQfGLWUP8yI0uiruSEi\nceczHw6u6bSm+jhQWHX2TV3794jCJDcdQ9oRit43TQQi3G2c7LcUNTHwbhtGUYa4\nUD1lJwh+dIwyXBMOEouizaHdQqMKiJl8X46QtoJ+qvVlOPt7owbrHkJFi/LilmHp\nTCime4EPjpu8DdkCAwEAAaNTMFEwHQYDVR0OBBYEFGY7tvvmrW+VSxs26yPuGid8\n2XhnMB8GA1UdIwQYMBaAFGY7tvvmrW+VSxs26yPuGid82XhnMA8GA1UdEwEB/wQF\nMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAIJ2ia0/Vao2PVLOAiUTplETrv7WPrAs\noH3XjLGobvS7ciKpUosCF0I0OkjvVOL6MCqI3idyC+oUqvn0Gzjs9uoe3q5jvY3D\nS3mHzSBbjNJwZoorcOWujlupGuEA0yvIv29r/qw1bP9aEN/wTAkHEg4tKHqLmGEH\n8lMne5/obmnSZyieBFWCMznSloP+CtXb8ptAi7bRyx62Wv8+qfd2fT0dON6MIA+e\nrIFaITBXsNp+sUCnZxYMfM2ClSg3IQP+8BWqxrA1c3/Omfp9e+P18c69JaII+Zrb\nY/iVFI6Rk4n5eio5CDF0WUbQeMO8t/x6TVYpK55bjlD+R2DQNUb+4mA=\n-----END CERTIFICATE-----"

/**
 * Start: Structs
 * End: Defines
 **/

typedef struct menuItem
{
    char * name;
    void (*mhandler)();
} menuItem_t;

typedef struct MQTTMessage{
    char * data;
    char * topic;
} MQTTMessage_t;

typedef struct config_s{
    unsigned short maxFoodAmmount;
    unsigned short foodAmmount[4];
    struct tm foodTime[4];
} config_t;



/**
 * Start: Static Variables
 * End: Structs
 **/

static hx711_t foodScale;
static i2c_dev_t pcf8574;
static char scaleTopic[31];
static char configTopic[26];
static char commandTopic[27];
static rotary_encoder_t encoderS;
static hd44780_t lcd; 
static QueueHandle_t encoderQueue;
static QueueHandle_t MQTTQueue;
static QueueHandle_t configCommandQueue;
static rotary_encoder_event_t eventBuff;
static esp_mqtt_client_config_t mqtt_cfg;
static esp_mqtt_client_handle_t client;
static menuItem_t lcdMenu[menuCount];


static ledc_timer_config_t pmw_timer = {
    .duty_resolution = LEDC_TIMER_13_BIT,
    .freq_hz = 769,                      
    .speed_mode = LEDC_HIGH_SPEED_MODE,           
    .timer_num = LEDC_TIMER_0,            
    .clk_cfg = LEDC_AUTO_CLK,              
};

static ledc_channel_config_t pmw_channel = {
        .channel    = 0,
        .duty       = 500,
        .gpio_num   = 15,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0,
};

static config_t deviceConfig;
const char * TAG = "main:";

static SemaphoreHandle_t scaleMutex;
static SemaphoreHandle_t configMutex;
static SemaphoreHandle_t despenceMutex;
static SemaphoreHandle_t mqttQueueWriteMutex;
/**
 * These wifi provisioning functions are copied from
 * https://github.com/espressif/esp-idf/blob/master/examples/provisioning/wifi_prov_mgr/main/app_main.c
 * They are public domain
 **/

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:

                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        /* Signal main application to continue execution */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    }
}

static void wifi_init_sta(void)
{
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}


esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

void provision_init(){
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
}


esp_err_t provisionBT(char * pop){
    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));

    /* What is the security level that we want (0 or 1):
        *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
        *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
        *          using X25519 key exchange and proof of possession (pop) and AES-CTR
        *          for encryption/decryption of messages.
        */
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;

    
    
    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };

    const char *service_key = NULL;
    
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    wifi_prov_mgr_endpoint_create("custom-data");
    //Start provisioning service
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

    return wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
}

/**
 * End Copied portion
 **/

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:


        esp_mqtt_client_subscribe(client,configTopic, 0);


        esp_mqtt_client_subscribe(client,commandTopic, 1);

        break;


    case MQTT_EVENT_DATA:
    {
        struct MQTTMessage messageTemp = {
            .data = malloc(event->data_len+1),
            .topic = malloc(event->topic_len+1),
        };
        
        memcpy(messageTemp.data,event->data, event->data_len);
        memcpy(messageTemp.topic,event->topic, event->topic_len);
        messageTemp.data[event->data_len] = '\0';
        messageTemp.topic[event->topic_len] = '\0';
        xQueueSend(configCommandQueue,&messageTemp,1000/portTICK_PERIOD_MS);
        break;
    }
    default:
        break;
    }
}



void printLine(char * str, int line){
    printf("%s\n",str);
    char writeStr[20];
    strcpy(writeStr,str);
    for(int i = strlen(writeStr);i < 19; i++){
        writeStr[i] = ' ';
    }
    writeStr[19] = '\0';
    hd44780_gotoxy(&lcd,0,line);
    hd44780_puts(&lcd,writeStr);
}

esp_err_t initScale(){
    foodScale.dout = DOUT_GPIO;
    foodScale.pd_sck = PD_SCK_GPIO;
    foodScale.gain = HX711_GAIN_A_64;
    esp_err_t r;
    // initialize device
    for(int i = 0; i < 100;i++)
    {
        r = hx711_init(&foodScale);
        if (r == ESP_OK){
            break;
        }
        printf("Could not initialize Scale: %d (%s)\n", r, esp_err_to_name(r));
        for(int j = 0; j < 10000; j++){
        }
    }
    return r;
}

int32_t readScale() {
    int32_t data = 0;
    
        esp_err_t r = hx711_wait(&foodScale, 500);
        if (r != ESP_OK)
        {
            printf("Device not found: %d (%s)\n", r, esp_err_to_name(r));
        }

        
        r = hx711_read_data(&foodScale, &data);
        if (r != ESP_OK)
        {
            printf("Could not read data: %d (%s)\n", r, esp_err_to_name(r));
        }
        
    return data;
}

int rawToGrams(int32_t data){
    return (int)((data-ZERO_VAL)*VAL_TO_G);
}

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}

void despenceFood(unsigned short ammount){
    if(xSemaphoreTake(despenceMutex,1000/portTICK_PERIOD_MS)==pdTRUE){
        if(xSemaphoreTake(scaleMutex,1000/portTICK_PERIOD_MS)==pdTRUE){
            unsigned short original = rawToGrams(readScale());
            while((original + ammount > deviceConfig.maxFoodAmmount ? deviceConfig.maxFoodAmmount:original + ammount)>rawToGrams(readScale())){
                ledc_channel_config(&pmw_channel);
                vTaskDelay(2000/portTICK_PERIOD_MS);
                ledc_stop(LEDC_HIGH_SPEED_MODE,0,0);
            }
            xSemaphoreGive(scaleMutex);
        }
        xSemaphoreGive(despenceMutex);
    }
}


void lcd_init()
{
    lcd.write_cb = write_lcd_data;
    lcd.font = HD44780_FONT_5X8;
    lcd.lines = 2;
    lcd.pins.rs = 0;
    lcd.pins.e  = 2;
    lcd.pins.d4 = 4;
    lcd.pins.d5 = 5;
    lcd.pins.d6 = 6;
    lcd.pins.d7 = 7;
    lcd.pins.bl = 3;

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, 0, I2C_ADDR, SDA_GPIO, SCL_GPIO));

    ESP_ERROR_CHECK(hd44780_init(&lcd));

    hd44780_switch_backlight(&lcd, true);
}

void provision_handler(){
    printLine("Proof of Ownership:",0);
    char pop[7];
    pop[6] ='\0';
    for(int i = 0;i<6;i++){
        pop[i] = (rand() % 89) + 33;
    }
    printLine(pop,1);
    provisionBT(pop);
}
void food_ammount_handler(){
    char buf[15];
    char cont = 0xff;
    int32_t data;
    printLine("Click to exit",1);
    while(cont){
        
        data = readScale();
        sprintf(buf,"%i",data);
        printf(buf);
        printLine(buf,0);
        while(uxQueueMessagesWaiting(encoderQueue)){
            xQueueReceive(encoderQueue,&eventBuff,20/portTICK_RATE_MS);
            switch (eventBuff.type)
            {
            case RE_ET_BTN_CLICKED:
                cont = 0;
                break;
            
            default:
                break;
            }
        }
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
}
void food_despence_handler(){}
/**
 * Start: Section: Tasks
 **/
void lcdTask(void *pvParameters){
    int menuVal = 0;
    printLine(lcdMenu[menuVal].name,0);
    while (1)
    {
        xQueueReceive(encoderQueue,&eventBuff,portMAX_DELAY);
        switch (eventBuff.type)
        {
        case RE_ET_CHANGED:
            if(eventBuff.diff > 0){
                menuVal = (menuVal + 1) % menuCount;
            }
            else if(eventBuff.diff < 0){
                menuVal = menuCount - 1;
            }

            break;
        case RE_ET_BTN_CLICKED:
            (lcdMenu[menuVal].mhandler)();
            break;
        default:
            break;
        }
        printLine(lcdMenu[menuVal].name,0);
        printLine("",1);
    }
}

void scaleTask(void *pvParameters)
{
    char buf[15];
    int32_t data = 0;
    int32_t values [3];
    int32_t averageL1 = 0;
    int32_t averageL2 = 0;
    MQTTMessage_t * scaleMessage;
    while (1)
    {   if(xSemaphoreTake(scaleMutex,portMAX_DELAY)==pdTRUE){
            for(int i = 0; i<3;i++){
                averageL1 = 0;
                for(int j = 0; j<10; j++){
                    data = readScale();
                    averageL1 = (averageL1 * (j/(j+1))) +(data/(j+1)); 
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                }
                values[i] = averageL1;
                //take 3 samples over 10 seconds
                vTaskDelay(3333 / portTICK_PERIOD_MS);
            }
            xSemaphoreGive(scaleMutex);


            //if the values mesured are too far apart the reading might not be valid (pet is eating etc.)
            if(xSemaphoreTake(mqttQueueWriteMutex,portMAX_DELAY) == pdTRUE && abs(values[0]-values[1])<100 && abs(values[0]-values[2])<100 && abs(values[1]-values[2])<100){
                averageL2 = (values[0]/3) + (values[1]/3) + (values[2]/3);
                sprintf(buf,"%i",rawToGrams(averageL2));
                scaleMessage = malloc(sizeof(MQTTMessage_t));
                scaleMessage->data = malloc(strlen(buf)+1);
                strcpy(scaleMessage->data,buf);
                scaleMessage->topic = malloc(32);
                strcpy(scaleMessage->topic,scaleTopic);
                xQueueSend(MQTTQueue,scaleMessage,500/portTICK_PERIOD_MS);
                xSemaphoreGive(mqttQueueWriteMutex);
            }
        }
        vTaskDelay(500/portTICK_PERIOD_MS); 
    }
}

void MQTT_Task(void *pvParameters)
{
    MQTTMessage_t messageBuf;
    //wait until wifi connects
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, portMAX_DELAY);
    esp_mqtt_client_start(client);
    while (1)
    {
        //send messages from queue
        if(xQueueReceive(MQTTQueue,&messageBuf,1000/portTICK_PERIOD_MS)){
            esp_mqtt_client_publish(client,messageBuf.topic,messageBuf.data,0,2,0);
            free(messageBuf.topic);
            free(messageBuf.data);
        }
    }
}

void despence_task(void *pvParameters){
    unsigned char next=0;
    char despenceAfter=0;
    struct tm  * now;
    time_t tempTime;
    unsigned int timeofday=0;
    unsigned int tmpTimeOfDay=0;
    unsigned int bestTimeOfDay=0;
    unsigned short foodAmmount=0;
    while (1)
    {
        tempTime = time(NULL);
        now = localtime(&tempTime);
        timeofday = now->tm_sec + (60 * now->tm_min) + (now->tm_hour * 3600);
        next = 0;
        
        if(xSemaphoreTake(configMutex,1000/portTICK_PERIOD_MS)==pdTRUE){
            for(int i = 1; i < 4; i++){
                tmpTimeOfDay = (deviceConfig.foodTime[i].tm_hour * 3600) + deviceConfig.foodTime[i].tm_sec + (60 * deviceConfig.foodTime[i].tm_min);
                if(deviceConfig.foodAmmount[i] != 0 && tmpTimeOfDay > timeofday && tmpTimeOfDay < bestTimeOfDay ){
                    bestTimeOfDay = tmpTimeOfDay;
                    next = i;
                }
            }
            despenceAfter = (bestTimeOfDay > timeofday && (foodAmmount = deviceConfig.foodAmmount[next])!=0);
            xSemaphoreGive(configMutex);
            //wait 5 minutes or to the next despence time which ever is lesser
            //can't just wait to next despence time because the time might have updated
            vTaskDelay(((bestTimeOfDay > timeofday && bestTimeOfDay-timeofday < 300) ? bestTimeOfDay-timeofday:300)*1000/portTICK_PERIOD_MS);
            if(despenceAfter){
                despenceFood(foodAmmount);
            }
        }
    }
}

void ConfigCommand_Task(void *pvParameters)
{
    char buf1[32];
    char buf2[32];
    unsigned int int1;
    unsigned int int2;
    unsigned int int3;
    unsigned int int4;
    MQTTMessage_t messageBuf;
    while(1){
        if(xQueueReceive(configCommandQueue,&messageBuf,portMAX_DELAY)){
            if(strcmp(messageBuf.topic,configTopic) == 0 && xSemaphoreTake(configMutex,portMAX_DELAY)==pdTRUE){
                sscanf(messageBuf.data,"%s:%s",buf1,buf2);
                if(strcmp(buf1,"foodtime")==0){
                    //hours,minutes,seconds,slot
                    sscanf(buf2,"%ui,%ui,%ui,%ui",&int1,&int2,&int3,&int4);
                    deviceConfig.foodTime[int4].tm_sec = int3;
                    deviceConfig.foodTime[int4].tm_min = int2;
                    deviceConfig.foodTime[int4].tm_hour = int1;
                }
                else if(strcmp(buf1,"foodammount")==0){
                    sscanf(buf2,"%ui,%ui",&int1,&int2);
                    deviceConfig.foodAmmount[int2] = (unsigned char)int1;
                }
                else if(strcmp(buf1,"timezone")==0){
                    setenv("tz",buf2,1);
                }
                else if(strcmp(buf1,"maxfoodammount")==0){
                    sscanf(buf2,"%hu",&(deviceConfig.maxFoodAmmount));
                }
                xSemaphoreGive(configMutex);
            }
            else if(strcmp(messageBuf.topic,commandTopic)==0){
                sscanf(messageBuf.data,"%s:%s",buf1,buf2);
                if(strcmp(buf1,"dispence")==0){
                    sscanf(buf2,"%ui",&int1);
                    despenceFood(int1);
                }
            }
        }
    }
}

/**
 * End Section: Tasks
 **/

void app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    /**
    size_t length;
    char * password = "!@#$^GS";
    
    nvs_open("MQTT_CONFIG",NVS_READWRITE,&MQTT_CONFIG_HANDLE);
    if(nvs_get_str(MQTT_CONFIG_HANDLE,"password",NULL,&length) == ESP_OK){
        password= malloc(length);
        nvs_get_str(MQTT_CONFIG_HANDLE,"password",password,&length);
    }
    else{
        password = malloc(16);
        password[15] = '\0';
        for(int i = 0;i<15;i++){
            password[i] = (rand() % 89) + 33;
        }
        nvs_set_str(MQTT_CONFIG_HANDLE,"password",password);
    }
    **/
    provision_init();
    
    bool provisioned = false;
    /* Let's find out if the device is provisioned */
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    
    if (provisioned) {
         wifi_init_sta();
    }

    ESP_ERROR_CHECK(i2cdev_init());
    
    encoderQueue = xQueueCreate(5,sizeof(rotary_encoder_event_t));
    MQTTQueue = xQueueCreate(5,sizeof(MQTTMessage_t));
    configCommandQueue = xQueueCreate(10,sizeof(MQTTMessage_t));
    encoderS.pin_a = 34;
    encoderS.pin_b = 35;
    encoderS.pin_btn = 32;
    
    ESP_ERROR_CHECK(rotary_encoder_init(encoderQueue));
    ESP_ERROR_CHECK(rotary_encoder_add(&encoderS));
    lcd_init(&lcd);
    ESP_ERROR_CHECK(initScale());   
    
    
    
    scaleTopic[0] = '\0';
    configTopic[0] = '\0';
    commandTopic[0] = '\0';
    strcat(scaleTopic,"devices/");
    strcat(scaleTopic,ESN);
    
    strcat(configTopic,scaleTopic);
    strcat(commandTopic,scaleTopic);
    
    strcat(scaleTopic,"/food_weight");
    strcat(configTopic,"/config");
    strcat(commandTopic,scaleTopic);

    mqtt_cfg.uri = "mqtt://grantfar.site:8883";
    mqtt_cfg.cert_pem = SSL_CERT;
    //mqtt_cfg.username = ESN;
    //strcpy(mqtt_cfg.password,password);
    
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    

    lcdMenu[0].name = "View food amount";
    lcdMenu[0].mhandler = food_ammount_handler;
    
    lcdMenu[2].name = "WIFI Setup";
    lcdMenu[2].mhandler = provision_handler;
             
    lcdMenu[1].name = "Despence Food";
    lcdMenu[1].mhandler = food_despence_handler;

    mqttQueueWriteMutex = xSemaphoreCreateMutex();
    scaleMutex = xSemaphoreCreateMutex();

    //default time zone is michigan
    setenv("tz","EST+5EDT,M3.2.0/2,M11.1.0/2",1);
    //start time sync opererations
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    ledc_timer_config(&pmw_timer);
    
    xTaskCreate(scaleTask,"scaleTask",15000,NULL,2,NULL);
    xTaskCreate(lcdTask,"lcdTask",30000,NULL,configMAX_PRIORITIES-1,NULL);
    xTaskCreate(MQTT_Task,"mqttTask",15000,NULL,configMAX_PRIORITIES/2,NULL);
    xTaskCreate(ConfigCommand_Task,"configCommandTask",15000,NULL,configMAX_PRIORITIES/2,NULL);
    xTaskCreate(despence_task,"despenceTask",15000,NULL,configMAX_PRIORITIES/2,NULL);
}
