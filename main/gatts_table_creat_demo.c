/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/****************************************************************************
*
* This file is for gatt server. It can send adv data, be connected by client.
* Run the gatt_client demo, the client demo will automatically connect to the gatt_server_service_table demo.
* Client demo will enable gatt_server_service_table's notify after connection. Then two devices will exchange
* data.
*
****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <rom/ets_sys.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "nvs.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "gatts_table_creat_demo.h"
#include "esp_gatt_common_api.h"

#include "esp_sleep.h"
//#include "esp_deep_sleep.h"
#include "esp_log.h"
#include "esp32/ulp.h"
#include "driver/touch_pad.h"
#include "driver/adc.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
//#include "soc/rtc.h"
#include <math.h>
#include "driver/uart.h"
#include "soc/uart_struct.h"
#include "string.h"

static const int RX_BUF_SIZE = 1024;


#define GPIO_OUTPUT_IO_0		2              // pin 2 for on-board LED
#define GPIO_OUTPUT_IO_MOTOR    13         //water discharge pump connected to pin 13
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_MOTOR))  //bit mask pin 2 & 13 as output
#define GPIO_OUTPUT_IO_BUZZ			27       //pin 11 connected to buzzer             
#define GPIO_INPUT_IO_FLOATMIN		32      // pin 32 connected to float sensor for reservoir
#define GPIO_INPUT_IO_FLOATMAX		33      // pin 33 for maximum water level float sensor
#define GPIO_INPUT_IO_CAL_BUTTON	21      // pin 21 as input for calibration button
#define GPIO_INPUT_PIN_SEL_FLOAT  ((1ULL<<GPIO_INPUT_IO_FLOATMAX) | (1ULL<<GPIO_INPUT_IO_FLOATMIN))  // bit mask multiple input pins i.e float sensors, calibration button
//#define GPIO_INPUT_IO_FLOWSENSOR1     18    //pin 18 as flow sensor for inlet valve
//#define GPIO_INPUT_PIN_SEL_FS  (1ULL<<GPIO_INPUT_IO_FLOWSENSOR1)  //bit mask flow sensor input pins
#define PCNT_TEST_UNIT      PCNT_UNIT_0
#define GPIO_OUTPUT_IO_INLET_VALVE0   22   
#define GPIO_OUTPUT_IO_INLET_VALVE1   23 
//#define GPIO_OUTPUT_IO_OUTLET_VALVE0  14   
#define GPIO_OUTPUT_IO_WATER_INDICATOR  14          //17 
#define GPIO_OUTPUT_PIN_SEL_VALVE  ((1ULL<<GPIO_OUTPUT_IO_INLET_VALVE0) | (1ULL<<GPIO_OUTPUT_IO_INLET_VALVE1))     //bit mask pin 22 & 23 as output
#define GPIO_OUTPUT_IO_BLE_BUTTON  19       //pin 19 is connected to BLE advertise button

#define RXD_PIN (GPIO_NUM_16)    //Rx pin of UART2 
#define TXD_PIN (GPIO_NUM_17)	//Tx pin of UART2  

//#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_IO_0)
#define ESP_INTR_FLAG_DEFAULT 0
//#define PCNT_H_LIM_VAL      30000
#define AD_DATA_PIN		26         //pin 26 connected to ADC hx711 data pin
#define AD_SCK_PIN		25         //pin 25 as ADC hx711 clock pin

#define GATTS_TABLE_TAG "GATTS_TABLE_DEMO"

#define PROFILE_NUM                 1
#define PROFILE_APP_IDX             0
#define ESP_APP_ID                  0x55
//#define SAMPLE_DEVICE_NAME          "ESP_GATTS_DEMO"
#define SAMPLE_DEVICE_NAME          "Tubby"
#define SVC_INST_ID                 0

#define GATTS_DEMO_CHAR_VAL_LEN_MAX 100
#define PREPARE_BUF_MAX_SIZE        1024
#define CHAR_DECLARATION_SIZE       (sizeof(uint8_t))

#define ADV_CONFIG_FLAG             (1 << 0)
#define SCAN_RSP_CONFIG_FLAG        (1 << 1)

#define wakeup_time_sec             18


//static int cnt = 0;

static RTC_DATA_ATTR bool bleConnected = false;       //boolean variable to check ble connected or not
static RTC_DATA_ATTR bool fillingWater = false;		  //to check water pump discharge active or not
static RTC_DATA_ATTR bool alarmStop = false;          //check alarm stop
static RTC_DATA_ATTR bool bleAdvertise = false;
static RTC_DATA_ATTR bool bleButton = false;
static RTC_DATA_ATTR bool Tubby_pause = false;

RTC_DATA_ATTR static int16_t count = 0;
RTC_DATA_ATTR static int32_t Slope_int = 0;
RTC_DATA_ATTR static volatile int waterVolume = 0;     
RTC_DATA_ATTR static volatile int calibrate = 0;
RTC_DATA_ATTR static volatile int32_t ADCmax = 0;    
RTC_DATA_ATTR static volatile int32_t ADCmin = 0;
RTC_DATA_ATTR static volatile int32_t ADC_current = 0;
RTC_DATA_ATTR static volatile int32_t ADC_target = 0;
RTC_DATA_ATTR static volatile int32_t ADC_prev = 0;
RTC_DATA_ATTR static volatile int32_t ADC_res = 0;
RTC_DATA_ATTR static volatile int32_t weightAvailable = 0;
RTC_DATA_ATTR static volatile int32_t fillCount = 0;
RTC_DATA_ATTR static volatile int32_t bleCount = 0;
RTC_DATA_ATTR static volatile uint8_t Tubby_calibrate = 0;  //Tubby calibration status flag for App
RTC_DATA_ATTR static volatile int actualVolume = 0;       //variable for water discharge volume more than 10L
RTC_DATA_ATTR static volatile int32_t quantity = 0;
static int checkCount[10];

RTC_DATA_ATTR static uint8_t  Device = 3;                //1-Pebble_SVC , 2-Pebble_MVC , 3-Tubby 

RTC_DATA_ATTR static int read_stop = 0;
//static int cal = 0;
xQueueHandle pcnt_evt_queue;   // A queue to handle pulse counter events
static xQueueHandle gpio_evt_queue = NULL;

//RTC_DATA_ATTR float Slope_f = 1.00;
RTC_DATA_ATTR float Slope = 1.00;          //float type slope to calculate ADC to water volume or vice versa

//static RTC_DATA_ATTR struct timeval sleep_enter_time;

static uint8_t adv_config_done       = 0;

//uint16_t heart_rate_handle_table[HRS_IDX_NB];
uint16_t aquarius_handle_table[AQUARIUS_IDX_NB];

static struct timeval systemTimeVal;
static struct timezone systemTimeZone;

static time_t calendarNow;
static struct tm timeinfo;
//static RTC_DATA_ATTR uint8_t arrayLength;

int32_t OFFSET = 0;
float SCALE = 1;

//RTC_DATA_ATTR static int32_t readCount = 1;

static RTC_DATA_ATTR struct timeAlarm {           //structure array for time point received from APP
  bool active;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint16_t duration;
  uint16_t volume;
}startAlarm[28], stopAlarm[28];

RTC_DATA_ATTR static uint8_t check_hour, check_min, check_day;

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t prepare_write_env;

//#define CONFIG_SET_RAW_ADV_DATA
#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] = {
        /* flags */
        0x02, 0x01, 0x06,
        /* tx power*/
        0x02, 0x0a, 0xeb,
        /* service uuid */
        0x03, 0x03, 0xFF, 0x00,
        /* device name */
        0x0f, 0x09, 'A', 'q', 'u', 'a', 'r', 'i', 'u', 's'
};
static uint8_t raw_scan_rsp_data[] = 
{
        /* flags */
        0x02, 0x01, 0x06,
        /* tx power */
        0x02, 0x0a, 0xeb,
        /* service uuid */
        0x03, 0x03, 0xFF,0x00
};

#else
static uint8_t service_uuid[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    //0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC
    0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

/* The length of adv data must be less than 31 bytes */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0x00,
    .manufacturer_len    = 0,    //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, //test_manufacturer,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = sizeof(service_uuid),
    .p_service_uuid      = service_uuid,
    //.flag                = 0,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),  //.flag = ESP_BLE_ADV_FLAG_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,  //Advertising limit for 30 seconds only
};

// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp        = true,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0x00,
    .manufacturer_len    = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, //&test_manufacturer[0],
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 16,
    .p_service_uuid      = service_uuid,
    //.flag                = 0,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),  //.flag = ESP_BLE_ADV_FLAG_LIMIT_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,  //Advertising limit for 30 seconds only
};
#endif /* CONFIG_SET_RAW_ADV_DATA */

static esp_ble_adv_params_t adv_params = {
    .adv_int_min         = 0x20,
    .adv_int_max         = 0x40,
    .adv_type            = ADV_TYPE_IND,
    .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
    .channel_map         = ADV_CHNL_ALL,
    .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst 
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
					esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/* Service */
static const uint16_t GATTS_SERVICE_UUID_AQUARIUS   =   0x00FF;
static const uint16_t GATTS_CHAR_UUID_BATTERY       =   0xFF01;
static const uint16_t GATTS_CHAR_UUID_CURRENT_TIME  =   0xFF02;
static const uint16_t GATTS_CHAR_UUID_POTS          =   0xFF03;
static const uint16_t GATTS_CHAR_UUID_NEW_TIME_POINT = 0xFF04;
static const uint16_t GATTS_CHAR_UUID_COMMAND       =   0xFF05;
static const uint16_t GATTS_CHAR_UUID_LOG_EVENT    =   0xFF06;


static const uint16_t primary_service_uuid         = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid   = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_read                = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write               = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_read_write          = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_write_notify   = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t heart_measurement_ccc[2]      = {0x00, 0x00};
static const uint8_t char_value[9]                 = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};


/* Full Database Description - Used to add attributes into the database */
static const esp_gatts_attr_db_t gatt_db[AQUARIUS_IDX_NB] =
{
    // Service Declaration
    [IDX_SVC_AQUARIUS]        =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(GATTS_SERVICE_UUID_AQUARIUS), (uint8_t *)&GATTS_SERVICE_UUID_AQUARIUS}},

    /* Characteristic Declaration */
    [IDX_CHAR_BATTERY]     =
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_BATTERY] =
    {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_BATTERY, ESP_GATT_PERM_READ,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /* Client Characteristic Configuration Descriptor */
    /*[IDX_CHAR_CFG_A]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(uint16_t), sizeof(heart_measurement_ccc), (uint8_t *)heart_measurement_ccc}},*/

    /* Characteristic Declaration */
    [IDX_CHAR_CURRENT_TIME]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_CURRENT_TIME]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_CURRENT_TIME, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /* Characteristic Declaration */
    [IDX_CHAR_COMMAND]      =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_COMMAND]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_COMMAND, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /*Characteristic Declaration */
    [IDX_CHAR_POTS]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_POTS] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_POTS, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /*Characteristic Declaration */
    [IDX_CHAR_NEW_TIME_POINT]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_NEW_TIME_POINT] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_NEW_TIME_POINT, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

    /*Characteristic Declaration */
    [IDX_CHAR_LOG_EVENT]     =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

    /* Characteristic Value */
    [IDX_CHAR_VAL_LOG_EVENT] =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_LOG_EVENT, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    #ifdef CONFIG_SET_RAW_ADV_DATA
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
    #else
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0){
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
    #endif
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            /* advertising start complete event to indicate advertising start successfully or failed */
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed");
				bleAdvertise = false;
            }else{
                ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");
				bleAdvertise = true;
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
				bleAdvertise = false;
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "update connetion params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
            break;
        default:
            break;
    }
}

void example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    } else {
        if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_OFFSET;
        } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_ATTR_LEN;
        }
    }
    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp){
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL){
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK){
               ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        }else{
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK){
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;

}

void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf){
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TABLE_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;

    for(int i = 0; i < AQUARIUS_IDX_NB ; i++){
        if(handle == aquarius_handle_table[i])
		{
            return i;
        }
    }

    return error;
 }

static int32_t read_nvs_data(const char* Key)
{
	esp_err_t err;
	int32_t readValue = 0;
	printf("\nOpening Non-Volatile Storage (NVS) handle... ");
	nvs_handle my_handle;
	err = nvs_open("storage", NVS_READWRITE, &my_handle);

	if (err != ESP_OK) 
	{
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} 

	else 
	{
		printf("Done\n");

		// Read
		printf("Reading flow count from NVS ... ");

		//int32_t restart_counter = 0; // value will default to 0, if not set yet in NVS
		err = nvs_get_i32(my_handle, Key, &readValue);

		switch (err)
		{
			case ESP_OK:
				printf("Done\n");
				printf("read value from NVS of %s = %d\n",Key,readValue);
				break;
			case ESP_ERR_NVS_NOT_FOUND:
				printf("The value is not initialized yet!\n");
				break;
			default :
				printf("Error (%s) reading!\n", esp_err_to_name(err));
		}
	 }
	 // Close
	 nvs_close(my_handle);
	 return readValue;
}

static void write_nvs_data(const char* Key, int32_t value)              
{
	esp_err_t err;
	// Open
	printf("\nOpening Non-Volatile Storage (NVS) handle... ");
	nvs_handle my_handle;
	err = nvs_open("storage", NVS_READWRITE, &my_handle);
	if (err != ESP_OK) 
	{
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} 
	else 
	{
		printf("Done\n");
		// Write
		err = nvs_set_i32(my_handle, Key, value);
		printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

		// Commit written value.
		// After setting any values, nvs_commit() must be called to ensure changes are written
		// to flash storage. Implementations may write to storage at other times,
		// but this is not guaranteed.
		printf("Committing updates in NVS ... ");
		err = nvs_commit(my_handle);
		printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

		// Close
		nvs_close(my_handle);
	}
 } 


/*static void write_nvs_str(const char* key, uint8_t* value, size_t length)              
{
		esp_err_t err;
		// Open
		printf("\nOpening Non-Volatile Storage (NVS) handle... ");
		nvs_handle str_handle;
		err = nvs_open("storage", NVS_READWRITE, &str_handle);
		if (err != ESP_OK) 
		{
			printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
		} 
		else 
		{
			printf("Done\n");
			// Write
			err = nvs_set_blob(str_handle, key, value, length);
			printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

			printf("Committing updates in NVS ... ");
			err = nvs_commit(str_handle);
			printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

			// Close
			nvs_close(str_handle);
		}
} 

static uint8_t* read_nvs_str(const char* key)
{
	esp_err_t err;
	size_t required_size;
	uint8_t* run_time = NULL;
	//uint8_t run_time[9];

	printf("\nOpening Non-Volatile Storage (NVS) handle... ");
	nvs_handle str_handle;
	err = nvs_open("storage", NVS_READWRITE, &str_handle);

	if (err != ESP_OK) 
	{
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} 

	else 
	{
		printf("Done\n");

		// Read
		printf("Reading time points from NVS ... ");

		// Read the size of memory space required for blob
	    required_size = 0;  // value will default to 0, if not set yet in NVS
		err = nvs_get_blob(str_handle, key, NULL, &required_size);
		if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) 
			return err;
		// Read previously saved blob if available
	    run_time = malloc(required_size + sizeof(uint8_t));
		 if (required_size > 0) {
		err = nvs_get_blob(str_handle, key, run_time, sizeof(run_time));
		 if (err != ESP_OK) 
			 return err;
    }

		switch (err) 
		{
			case ESP_OK:
				printf("Done\n");
				printf("read value from NVS of %s = %s\n",key,run_time);
				break;
			case ESP_ERR_NVS_NOT_FOUND:
				printf("The value is not initialized yet!\n");
				break;
			default :
				printf("Error (%s) reading!\n", esp_err_to_name(err));
		}
	 }
	 nvs_close(str_handle);

	 return run_time;
}*/

void HX711_power_down()               //function to put hx711 in power down mode
{
	gpio_set_level(AD_SCK_PIN, 0);
	ets_delay_us(1);
	gpio_set_level(AD_SCK_PIN, 1);
	ets_delay_us(1);
}

void HX711_power_up()				  //function to put hx711 in power up mode
{
	gpio_set_level(AD_SCK_PIN, 0);
}
// set the SCALE value; this value is used to convert the raw data to "human readable" data (measure units)
//void set_scale(float scale = 1.f);

void set_offset(int32_t offset) 
{
	OFFSET = offset;
}

void set_scale(float scale)          //set scale factor to convert ADC value into desired units
{
	SCALE = scale;
}

bool is_ready()                      //check if hx711 is ready to send data bytes
{
	return (gpio_get_level(AD_DATA_PIN) == 0);
}

uint8_t shiftInSlow()                //read 8 data bytes from hx711 to esp 32 
{
    uint8_t value = 0;
    uint8_t i;

    for(i = 0; i < 8; ++i) 
	{
        gpio_set_level(AD_SCK_PIN, 1);
        ets_delay_us(1);
        value |= (gpio_get_level(AD_DATA_PIN) << (7 - i));
        gpio_set_level(AD_SCK_PIN, 0);
        ets_delay_us(1);
    }
	//printf("byte value: %d\n",val);
    return value;
}

int32_t read()                       //read 24-bit data bits and convert into 32-bit int32_t variable
{
	uint32_t rxData = 0;
	uint8_t data[3] = { 0 };
	uint8_t filler = 0x00;
	// wait for the chip to become ready
	/*while (!is_ready()) {
		// Will do nothing on Arduino but prevent resets of ESP8266 (Watchdog Issue)
		vPortYield();
		ESP_LOGI("ADC","hx711 not ready");
	}*/
    while(gpio_get_level(AD_DATA_PIN) == 1)
	{
		vPortYield();
	}

	// pulse the clock pin 24 times to read the data
	data[2] = shiftInSlow();
	data[1] = shiftInSlow();
	data[0] = shiftInSlow();

	//set the channel and the gain factor for the next reading using the clock pin
	for (unsigned int i = 0; i < 1; i++) {
		gpio_set_level(AD_SCK_PIN, 1);
		ets_delay_us(1);
		gpio_set_level(AD_SCK_PIN, 0);
		ets_delay_us(1);
	}

	// Replicate the most significant bit to pad out a 32-bit signed integer
	if (data[2] & 0x80) 
	{
		filler = 0xFF;
	} 
	else 
	{
		filler = 0x00;

	}

	// Construct a 32-bit signed integer
	rxData = ((uint32_t)filler << 24| (uint32_t)data[2] << 16| (uint32_t)data[1] << 8| (uint32_t)data[0]);
    //printf("Instant weight value: %lu\n",rxData);
	//rxData = rxData + 8388608;

	return (int32_t)(rxData);
}

int32_t read_average(int times)                      //find average of certain no. of adc readings
{
	int32_t sum = 0, val = 0, prevValue = 0;
	for (int i = 0; i < times; i++)
	{	
		prevValue = read();
		
		if(val > 0 && ((prevValue > (val + 1000)) || (prevValue < (val - 1000))))     // code to remove any spike value from average
		{
			//printf("Instant weight value %d: #####%d\n",i,prevValue);				  ////If two consecutive reading difference is greater/less than 1000	
			i = i-1;
			val = 0;
		}
		//else if(val > 0 && ((prevValue > (val + 800)) && (prevValue < (val + 1000))))
			//printf("Instant weight value %d: !!!!!%d\n",i,prevValue);                  //If two consecutive reading difference is between 800 to 1000
		else
		{
		   val = prevValue;
			//printf("Instant weight value %d: %d\n",i,prevValue);
		}
		sum += val; 
		vPortYield();
		if(read_stop == 1)
		{
			read_stop = 0;
			times = i + 1;
			break;
		}
	}
    printf("\nAvg weight value: %d\n\n",(sum / times));
	   return (sum / times);
	
}

int32_t get_adc_value(int times)      //function to calculate average adc reading value with offset
{
	int32_t adc_val = 0;
	adc_val = read_average(times);
	printf("Average weight value with offset : %d\n",(adc_val - OFFSET));
	return (adc_val - OFFSET);
}

int32_t get_units(int times)                     //calculate adc average value with scale
{
	return get_adc_value(times) / SCALE;
}

void cal_offset(int times)                       //calculate offset adc average value
{
	int32_t sum = read_average(times);
	set_offset(sum);
}

 void uart_init()	// initialise UART2 of ESP32 with baud rate, pin no. and driver install
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
}

int sendString(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_2, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

int sendChar(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_2, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

static void motor_open()						//turn ON water discharge pump
{
	char str[10];
	ESP_LOGI(GATTS_TABLE_TAG, "Starting Motor...");
	rtc_gpio_hold_dis(GPIO_OUTPUT_IO_MOTOR);
	rtc_gpio_set_level(GPIO_OUTPUT_IO_MOTOR, 0);
	rtc_gpio_hold_en(GPIO_OUTPUT_IO_MOTOR);
	sendChar("UART Tx", "A");					//display Discharge ON
	//tostring(str, quantity);
	sprintf(str, "%d", quantity); 
	if(quantity > 0)
		sendString("UART Tx", str);

	printf("Motor ON\n");
}

static void motor_close()						//turn OFF water discharge pump
{
	ESP_LOGI(GATTS_TABLE_TAG, "Stoping Motor...");
	rtc_gpio_hold_dis(GPIO_OUTPUT_IO_MOTOR);
	rtc_gpio_set_level(GPIO_OUTPUT_IO_MOTOR, 1);
	rtc_gpio_hold_en(GPIO_OUTPUT_IO_MOTOR);
	sendChar("UART Tx", "B");                    //display Discharge OFF
	printf("Motor OFF\n");
}

static void inlet_valve_open()
{
	ESP_LOGI(GATTS_TABLE_TAG, "Opening inlet Solenoid...\n");
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE0, 0);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE1, 1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE0, 1);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE1, 1);
}

static void inlet_valve_close()
{
	ESP_LOGI(GATTS_TABLE_TAG, "Closing inlet Solenoid...\n");
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE1, 0);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE0, 1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE1, 1);
	gpio_set_level(GPIO_OUTPUT_IO_INLET_VALVE0, 1);
}

static void rx_task()
{
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
	const int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 1000 / portTICK_RATE_MS);
	if (rxBytes > 0) 
   {
		data[rxBytes] = 0;
		ESP_LOGI("RX_TASK", "Read %d bytes: '%s'", rxBytes, data);
		ESP_LOG_BUFFER_HEXDUMP("RX_TASK", data, rxBytes, ESP_LOG_INFO);
	
	}
    free(data);
}

 static void check_tpError()
 {

	time(&calendarNow);
	localtime_r(&calendarNow, &timeinfo);
	char strftime_buf[64];
	setenv("TZ", "IST-5:30", 0);
	tzset();
	localtime_r(&calendarNow, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);

	 if((timeinfo.tm_wday == check_day) && (timeinfo.tm_hour == check_hour) && (timeinfo.tm_min == check_min) && fillingWater)   //wait for 1hr to discharge water as per time point
	 {
	   motor_close();
	   fillingWater = false;
	   printf("Time point missed: %d:%d\n",(check_hour-1),check_min);
	   //sendChar("UART Tx", "a");
	   check_day = 0;
	   check_hour = 0;
	   check_min = 0;
	   rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
	   rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
	   vTaskDelay(200 / portTICK_PERIOD_MS);
	   rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
	   rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
	 }
 }

 static bool check_flowsensor()               //check flow sensor attached to Inlet valve for water availability
 {	
	int32_t i,j,flag = 0;

	for(i=0; i<10; i++)
	   {
		 checkCount[i] = fillCount;
		 vTaskDelay(50 / portTICK_PERIOD_MS);
	   }
	for(i=0; i<10; i++)
	 {
		for(j=i+1;j<10;j++)
		{
			if(checkCount[j] > checkCount[i])
			{
			 flag = 1;
			}
			else
			  flag = 0;
		}
	 }   
    if(flag == 1)
	{
	  printf("No water from inlet pipe\n");
	  gpio_set_level(GPIO_OUTPUT_IO_WATER_INDICATOR, 1);      //turn ON LED  
	  return true;
	}
	else
	 {
	  gpio_set_level(GPIO_OUTPUT_IO_WATER_INDICATOR, 0);      //turn OFF LED 
	  return false;	
	 }
		
 }

 static void water_refill()                        //refill water using inlet value if water quantity available less than to be discharge
 {
    printf("Opening inlet solenoid to refill\n");
	sendChar("UART Tx", "I");
	inlet_valve_open();
    while(gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 0)
    {
      ADC_current = get_units(10);
	  //check_flowsensor();
	  //if(check_flowsensor())
		//  break;
    }
	if(gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 1)
	 {
		inlet_valve_close();
		printf("Inlet valve closed\n");
		gpio_set_level(GPIO_OUTPUT_IO_WATER_INDICATOR, 0);

		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
		//ADCmax = ADC_current;
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		ADC_current = get_units(20);
		ADC_prev = ADC_current;
		printf("Water refilled to maximum limit...\nADC_current = %d\nADCmax = %d\nSlope = %.4f\n",ADC_current,ADCmax,Slope);
		
		sendChar("UART Tx", "J");
		write_nvs_data("ADC_prev",ADC_prev);
	 }
	
 }

 static void calibration()                           //calibration function to fill water and set ADCmin and ADCmax to calculate slope when user presses calibration button
 {
	int32_t nvs_Slope = 0;
	gpio_config_t io_conf_float;
	//cal = 0;
	ADCmax = 0;
	ADCmin = 0;
	Slope = 0;
    
	if(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0)
	{
		inlet_valve_open();
		sendChar("UART Tx", "F");                  //Calibration Start message for ESP8266 to display on LCD
		printf("Inlet valve opened\n");

		while(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0)
		{
		  ADC_current = read_average(10);
		  printf("ADC weight value : %d\n",ADC_current);
		 // check_flowsensor();
		  //if(check_flowsensor())
			//  break;
		}
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		
		inlet_valve_close();    //close inlet valve to calculate ADCmin

		cal_offset(20);
		ADC_current = 0;
		calibrate = 1;
		ADCmin = get_units(20);

		printf("ADC reservoir limit,ADCmin : %d\n",ADCmin);
		//vTaskDelay(1000 / portTICK_PERIOD_MS);
		write_nvs_data("Offset",OFFSET);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 
		printf("weight OFFSET value : %d\n",OFFSET);
		//readCount = 0;

		inlet_valve_open();             //Open inlet solenoid valve for ADCmax

		while((gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 0))
		{
			//check_flowsensor();
			//if(check_flowsensor())
			//break;
			ADC_current = read_average(10);
			printf("ADC weight value : %d\n",ADC_current);
		}
		if((gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 1) && (calibrate == 1))        
		 {
			sendChar("UART Tx", "G");           //Calibration finish message for ESP8266 to display on LCD
			inlet_valve_close();
			printf("Inlet valve closed\n");
			gpio_set_level(GPIO_OUTPUT_IO_WATER_INDICATOR, 0);
			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);

			ADC_current = get_units(20);
			ADCmax = ADC_current;
			ADC_prev = ADCmax;
			Slope = (float)(ADCmax - ADCmin) / (float)10000;          // for capacity of 10 litres
			//Slope = ceil(Slope_f);
			printf("Water reached maximum limit in tank\nADCmax = %d\nSlope = %.4f\n",ADCmax,Slope);
			calibrate = 0;

			nvs_Slope = (int32_t)(Slope * 1000);
			write_nvs_data("Slope",nvs_Slope);
			write_nvs_data("ADC_prev",ADC_prev);
			Tubby_calibrate = 1;    // Tubby Calibration staus flag: calibrated	
		 }
	 }
	 else
	 {
	  printf("Empty tubby for calibration...\n");
	  sendChar("UART Tx", "L");
	 }
 }

 static void volume_beyondLimit()
 {
	int32_t ADC_quant = 0;
	int16_t i, temp_vol1, temp_vol2;
	temp_vol1 = (actualVolume / 10000) - 1;
	temp_vol2 = actualVolume % 10000;
	if(temp_vol1 > 0)                // For volume in multiple of 10000ml
	{	  
	  for(i=0; i<temp_vol1; i++)	
	  {
		weightAvailable = (int32_t)((float)(ADC_current + ADC_res) / Slope);
		waterVolume = 10000;

		if((weightAvailable < waterVolume) && (gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 0))
		{
			printf("Water insufficient to discharge...Please fill water\nWater discharge: %d\nWater available: %d\n",waterVolume,weightAvailable);
			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0); 
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
			water_refill();
			printf("Water ready to discharge...\n");
			vTaskDelay(10000 / portTICK_PERIOD_MS);
		}
		ADC_current = get_units(20); 
		if(ADC_prev != 0)
		 {
		   ADC_res = ADC_prev - ADC_current;	
		   write_nvs_data("ADC_res",ADC_res);
		 }
		ADC_quant = (int32_t)((float)waterVolume * Slope);
		ADC_target = ADC_current - ADC_quant;
		fillingWater = true;
		motor_open();
		printf("Water volume for Motor close: %d ml\nADC_current = %d\nADC_prev = %d\nADC_res = %d\nSlope = %.2f\nADC_quant = %d\nADC_target = %d\n",waterVolume,ADC_current,ADC_prev,ADC_res,Slope,ADC_quant,ADC_target);
		while((ADC_current > ADC_target) && fillingWater)
		{
			ADC_current = get_units(20);
			printf("ADC weight value : %d\n",ADC_current);
		}
		if(ADC_current <= ADC_target && fillingWater)
		{
			read_stop = 1;
			motor_close();
			ADC_prev = ADC_current + ADC_res;
			printf("Motor turned OFF\n Target value reached..\n ADC current = %d\nADC_prev = %d\n",ADC_current,ADC_prev);
			fillingWater = false;
			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	
			ADC_target = 0;	
			write_nvs_data("ADC_prev",ADC_prev);
			actualVolume = actualVolume - 10000;

		 }
	  }
	  if(actualVolume == 0)
		  quantity = 0;
	}
	if(temp_vol2 > 0)                        // for volume remaining after multiple of 10000ml
	{
		weightAvailable = (int32_t)((float)(ADC_current + ADC_res) / Slope);
		waterVolume = temp_vol2;

		if((weightAvailable < waterVolume) && (gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 0))
		{
		 printf("Water insufficient to discharge...Please fill water\nWater discharge: %d\nWater available: %d\n",waterVolume,weightAvailable);
		 rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0); 
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
		water_refill();
		printf("Water ready to discharge...\n");
		vTaskDelay(10000 / portTICK_PERIOD_MS);
		}
		ADC_current = get_units(20); 
		if(ADC_prev != 0)
		{
		   ADC_res = ADC_prev - ADC_current;	
		   write_nvs_data("ADC_res",ADC_res);
		}

		ADC_quant = (int32_t)((float)waterVolume * Slope);
		ADC_target = ADC_current - ADC_quant;
		fillingWater = true;
		motor_open();
		printf("Water volume for Motor close: %d ml\nADC_current = %d\nADC_prev = %d\nADC_res = %d\nSlope = %.2f\nADC_quant = %d\nADC_target = %d\n",waterVolume,ADC_current,ADC_prev,ADC_res,Slope,ADC_quant,ADC_target);
		actualVolume = actualVolume - waterVolume;
	}

 }

 static void water_discharge()                  // check if water reaches to reservoir level, reaches to maximum level or reaches to target value
 {
	int32_t nvs_Slope = 0,nvs_ADCprev = 0;

	ADC_current = get_units(20);       // check current ADC value to stop water pump

	if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)				//if(cal == 1)
	 {
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)
		{
		printf("Calibration mode active\n");
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(200 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
		calibration();
		}
	  }
	 if((gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0) && fillingWater)        
	 {
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		motor_close();
		fillingWater = false;
		ADC_current = get_units(20);
		read_stop = 1;		
		//ADCmin = ADC_current + ADC_res;
		ADCmin = get_units(20);
		printf("Water reached reservoir limit in tank\n ADCmin = %d\nADC_current = %d\n",ADCmin,ADC_current);
		//vTaskDelay(1000 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);

		Tubby_calibrate = 0;	// Change Tubby calibration status if water reaches reservoir limt i.e 0
		
		if(waterVolume >= 10000)
		   volume_beyondLimit();                   
	 }

	  if((gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0) && (Tubby_calibrate == 1))        
	 {
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0)
		 {
			Tubby_calibrate = 0;	// Change Tubby calibration status if water reaches reservoir limt i.e 0                 
			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			ADC_current = get_units(20);
			read_stop = 1;		
			motor_close();
			fillingWater = false;
			//ADCmin = ADC_current + ADC_res;
			ADCmin = get_units(20);
			printf("Water reached reservoir limit in tank\n ADCmin = %d\nADC_current = %d\n",ADCmin,ADC_current);
			//vTaskDelay(1000 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);			
		 }
	  }

	 if(ADC_current <= ADC_target && fillingWater && (actualVolume == 0))       //code to check ADC value for volume < 10000
	{
	    read_stop = 1;
		motor_close();
		ADC_prev = ADC_current + ADC_res;
		printf("Motor turned OFF\n Target value reached..\n ADC current = %d\nADC_prev = %d\n",ADC_current,ADC_prev);
		fillingWater = false;
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	
		ADC_target = 0;	
		write_nvs_data("ADC_prev",ADC_prev);
		quantity = 0;
	 }

	 if(ADC_current <= ADC_target && fillingWater && (actualVolume > 0))			//code to check ADC value for volume > 10000
	{
	    read_stop = 1;
		motor_close();
		ADC_prev = ADC_current + ADC_res;
		printf("Motor turned OFF\n Target value reached..\n ADC current = %d\nADC_prev = %d\n",ADC_current,ADC_prev);
		fillingWater = false;
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	
		ADC_target = 0;	
		write_nvs_data("ADC_prev",ADC_prev);
		volume_beyondLimit();
	 }
	 
 }

 static void check_waterFlow()						//check if water reaches to reservoir level, reaches to maximum level or reaches to target value inside alarms
 {
	 int32_t nvs_Slope = 0,nvs_ADCprev = 0;
     //ADC_current = get_units(20);	

	if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)				//if(cal == 1)
	{
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)
		{
		printf("Calibration mode active\n");
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(200 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
		calibration();
		}
	}
	 if((gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0) && fillingWater)        
	 {
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		motor_close();
		fillingWater = false;
		ADC_current = get_units(20);
		read_stop = 1;		
		//ADCmin = ADC_current + ADC_res;
		ADCmin = get_units(20);
		printf("Water reached reservoir limit in tank\n ADCmin = %d\nADC_current = %d\n",ADCmin,ADC_current);
		//vTaskDelay(1000 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);

		Tubby_calibrate = 0;		// Change Tubby calibration status if water reaches reservoir limt i.e 0

		if(waterVolume >= 10000)
		   volume_beyondLimit();
	 }

	 if((gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0) && (Tubby_calibrate == 1))        
	 {
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0)
		 {
			Tubby_calibrate = 0;	// Change Tubby calibration status if water reaches reservoir limt i.e 0                 
			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			ADC_current = get_units(20);
			read_stop = 1;		
			motor_close();
			fillingWater = false;
			//ADCmin = ADC_current + ADC_res;
			ADCmin = get_units(20);
			printf("Water reached reservoir limit in tank\n ADCmin = %d\nADC_current = %d\n",ADCmin,ADC_current);
			//vTaskDelay(1000 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);			
		 }
	  }

	 if(ADC_current <= ADC_target && fillingWater && (actualVolume == 0))       //code to check ADC value for volume < 10000
	{
	    read_stop = 1;
		motor_close();
		ADC_prev = ADC_current + ADC_res;
		//nvs_ADCprev = ADC_prev;
		printf("Motor turned OFF\n Target value reached..\n ADC current = %d\nADC_prev = %d\n",ADC_current,ADC_prev);
		fillingWater = false;
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	
		ADC_target = 0;	
		write_nvs_data("ADC_prev",ADC_prev);
		quantity = 0;
	 }
     
	 if(ADC_current <= ADC_target && fillingWater && (actualVolume > 0))			//code to check ADC value for volume > 10000
	{
	    read_stop = 1;
		motor_close();
		ADC_prev = ADC_current + ADC_res;
		printf("Motor turned OFF\n Target value reached..\n ADC current = %d\nADC_prev = %d\n",ADC_current,ADC_prev);
		fillingWater = false;
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		vTaskDelay(500 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	
		ADC_target = 0;	
		write_nvs_data("ADC_prev",ADC_prev);
		volume_beyondLimit();
	 }

 }

 static void pause_tubby()
{
  while(Tubby_pause && (!fillingWater))
  {
    printf("Tubby is paused\n");
	ADC_current = get_units(20);
	printf("ADC weight value : %d\n",ADC_current);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
  } 
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
	uint8_t res = 0xff;   // Test to find characteristics
	int32_t quant = 0;
    switch (event) {
        case ESP_GATTS_REG_EVT:{
            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);
            if (set_dev_name_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
            }
    #ifdef CONFIG_SET_RAW_ADV_DATA
            esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
            if (raw_adv_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
            }
            adv_config_done |= ADV_CONFIG_FLAG;
            esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
            if (raw_scan_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
            }
            adv_config_done |= SCAN_RSP_CONFIG_FLAG;
    #else
            //config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
            }
			else
				ESP_LOGI(GATTS_TABLE_TAG,"config advertisement data successfully...");
            adv_config_done |= ADV_CONFIG_FLAG;
            //config scan response data
            ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
            }
			else
				ESP_LOGI(GATTS_TABLE_TAG,"config scan rsp data successfully...");
            adv_config_done |= SCAN_RSP_CONFIG_FLAG;
    #endif
            esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, AQUARIUS_IDX_NB, SVC_INST_ID);
            if (create_attr_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
            }
        }
       	    break;
        case ESP_GATTS_READ_EVT:
            //ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_READ_EVT");
			
			ESP_LOGI(GATTS_TABLE_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
			esp_gatt_rsp_t rsp;
			memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
			rsp.attr_value.handle = param->read.handle;
			rsp.attr_value.len = 6;       //sizeof(char_value);
			if(Tubby_calibrate == 1)
				quant = (int32_t)((float)(ADC_current + ADC_res) / Slope);
			else
				quant = 0;
			rsp.attr_value.value[0] = (uint8_t) (quant >> 24);                //0xc8;
			rsp.attr_value.value[1] = (uint8_t) (quant >> 16);                //0x64;
			rsp.attr_value.value[2] = (uint8_t) (quant >> 8);
			rsp.attr_value.value[3] = (uint8_t) quant;
			rsp.attr_value.value[4] = Tubby_calibrate;
			rsp.attr_value.value[5] = Device;
			//res = find_char_and_desr_index(param->read.handle);
			res = (int) param->read.handle;
            if(res == 42)
			{
                //TODO:client read the status characteristic
				printf("weight value for display : %d\nActual weight(ml): %d\n",(ADC_current + ADC_res),quant);
				printf("Device for display : %d\n",Device);
				esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            }
       	    break;
        case ESP_GATTS_WRITE_EVT:

			//res = find_char_and_desr_index(param->write.handle);
            if (!param->write.is_prep){
                ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d, value :", param->write.handle, param->write.len);
                esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);


                uint16_t dataHandle = (int)param->write.handle;
                uint8_t* dataValue = param->write.value;

                //gpio_config_t io_conf;

                switch (dataHandle) {
                  case 44:
                    ESP_LOGI("gatt write event","Current Time written");
                    uint32_t systemTime = 0;
                    int index;
                    for(index=0;index<4;index++) {  
                      ESP_LOGI("dataValue","%d", *dataValue);
                      systemTime = systemTime << 8;
                      systemTime = systemTime + *dataValue;
                      ESP_LOGI("current system time","%d", systemTime);
                      dataValue++;
                    }

                    systemTimeVal.tv_sec = systemTime;
                    systemTimeVal.tv_usec = 0;
                    systemTimeZone.tz_minuteswest = -330;
                    systemTimeZone.tz_dsttime = 0;
                    settimeofday(&systemTimeVal, NULL);

                    time(&calendarNow);
                    localtime_r(&calendarNow, &timeinfo);
                    char strftime_buf[64];

                    // Set timezone to Indian Standard Time and print local time
                    //setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
                    setenv("TZ", "IST-5:30", 0);
                    tzset();
                    localtime_r(&calendarNow, &timeinfo);
                    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
                    ESP_LOGI("Current Time","The current date/time in India is: %s", strftime_buf);
					rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
					rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
					vTaskDelay(200 / portTICK_PERIOD_MS);
					rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
					rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);

                  break;

                  case 46:
                    ESP_LOGI("gatt write event","Valve Command written : %d", *dataValue);
                    switch (*dataValue) 
					{
                      case 1:
							ESP_LOGI("Command","Flush Open");
							if(!Tubby_pause)
							{
								fillingWater = true;
								quantity = 0;
								rtc_gpio_hold_dis(GPIO_OUTPUT_IO_0);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 1);  //rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 0);
								rtc_gpio_hold_en(GPIO_OUTPUT_IO_0);
																								  
								rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);

								motor_open();                //Open water pump
								
								vTaskDelay(200 / portTICK_PERIOD_MS);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
								rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 

								//uint8_t* output = read_nvs_str("alarm1");
								//printf("Read string from NVS is: %d %d %d %d %d %d %d %d %d",output[0], output[1], output[2],  output[3],  output[4], output[5], output[6], output[7], output[8]);

							}

						break;

                      case 2:
							ESP_LOGI("Command","Start");
							rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
							vTaskDelay(200 / portTICK_PERIOD_MS);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
							rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
							Tubby_pause = false;
							
						break;

					  case 3:
							ESP_LOGI("Command","Stop");  
							printf("Deactivate all alarms\n");
							Tubby_pause = false;
							uint8_t k;
							//solenoid = false;
							rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
							vTaskDelay(200 / portTICK_PERIOD_MS);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
							rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 
							for(k = 0; k < 28; k++)
							{
							  startAlarm[k].active = 0;
							  stopAlarm[k].active = 0;
							}							

						break;

					  case 4:
							ESP_LOGI("Command","Pause");
							Tubby_pause = true;
							/*const int ext_wakeup_pin_1 = 15;
							const uint64_t ext_wakeup_pin_1_mask = 1 << ext_wakeup_pin_1;
							const int ext_wakeup_pin_2 = 4;
							const uint64_t ext_wakeup_pin_2_mask = 1 << ext_wakeup_pin_2;

							ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_1);
							esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);
							printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
							esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
							//ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_2);
							//esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);

							ESP_LOGI("Disconnect Command","Entering deep sleep\n");
							//countPrev = flowCount;
							esp_deep_sleep_start();*/
							rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
							vTaskDelay(200 / portTICK_PERIOD_MS);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
							rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);

						break;


                      case 5:
							ESP_LOGI("Command","Flush Close");
							if(!Tubby_pause)
							{
								quantity = 0;
								fillingWater = false;

								rtc_gpio_hold_dis(GPIO_OUTPUT_IO_0);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 0);  //turn off on-board led
								rtc_gpio_hold_en(GPIO_OUTPUT_IO_0);

								rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);

								motor_close();                //switch off water pump
								
								vTaskDelay(200 / portTICK_PERIOD_MS);
								rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
								rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 

								ADC_current = get_units(20);
								ADC_prev = ADC_current;
								write_nvs_data("ADC_prev",ADC_prev);
							}
						break;
                      
						  default:
							ESP_LOGI("Command","Unknown Command");
						break;
                    }
					break;

                  case 48:
                    ESP_LOGI("gatt write event","Number of Pots written: %d",dataValue[0]);
		
					
                  break;

                  case 50:
                    ESP_LOGI("gatt write event","New Time Point written: Active=%d Index=%d Day=%d Hour=%d Minute=%d DurationMSB=%d DurationLSB=%d VolumeMSB=%d VolumeLSB=%d",dataValue[4],dataValue[0],dataValue[1],dataValue[2],dataValue[3],dataValue[5],dataValue[6],dataValue[7],dataValue[8]);
                    if(!Tubby_pause && (Tubby_calibrate == 1))
					{
						if(dataValue[0] == 0)
						{
							uint8_t k;
							for(k = 0; k < 28; k++)            //Deactivate all alarms to set new alarms
							{
							  startAlarm[k].active = 0;
							  stopAlarm[k].active = 0;
							}
							printf("Old Time points erased..\n");
						}
						else
						{
							startAlarm[dataValue[0]-1].active = dataValue[4];
							startAlarm[dataValue[0]-1].day = dataValue[1]-1;
							if(startAlarm[dataValue[0]-1].day == 0)
								startAlarm[dataValue[0]-1].day = 7;
							startAlarm[dataValue[0]-1].hour = dataValue[2];
							startAlarm[dataValue[0]-1].minute = dataValue[3];
							startAlarm[dataValue[0]-1].duration = (256 * (uint8_t)dataValue[5]) + (uint8_t)dataValue[6];
							startAlarm[dataValue[0]-1].volume = (256 * (uint8_t)dataValue[7]) + (uint8_t)dataValue[8];
							ESP_LOGI("gatt event","Duration=%d Volume=%d\n",startAlarm[dataValue[0]-1].duration,startAlarm[dataValue[0]-1].volume);
							rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
							vTaskDelay(200 / portTICK_PERIOD_MS);
							rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
							rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);	

							//write_nvs_str("alarm1", dataValue, sizeof(dataValue));
						}

						
					}
                  break;

                  default:
                    ESP_LOGI("gatt write event","Unknown Handle");
                  break;
                }
                /*if (aquarius_handle_table[IDX_CHAR_CFG_A] == param->write.handle && param->write.len == 2){
                    uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                    if (descr_value == 0x0001){
                        ESP_LOGI(GATTS_TABLE_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i % 0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_A],
                                                sizeof(notify_data), notify_data, false);
                    }else if (descr_value == 0x0002){
                        ESP_LOGI(GATTS_TABLE_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i % 0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, heart_rate_handle_table[IDX_CHAR_VAL_A],
                                            sizeof(indicate_data), indicate_data, true);
                    }
                    else if (descr_value == 0x0000){
                        ESP_LOGI(GATTS_TABLE_TAG, "notify/indicate disable ");
                    }else{
                        ESP_LOGE(GATTS_TABLE_TAG, "unknown descr value");
                        esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
                    }

                }*/
                /* send response when param->write.need_rsp is true*/
                if (param->write.need_rsp){
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }else{
                /* handle prepare write */
                example_prepare_write_event_env(gatts_if, &prepare_write_env, param);
            }
      	    break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            example_exec_write_event_env(&prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d", param->conf.status);
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_CONNECT_EVT:
            bleConnected = true;
			bleButton = false;
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
            esp_log_buffer_hex(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
            //start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            bleConnected = false;
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = %d", param->disconnect.reason);
            //esp_ble_gap_start_advertising(&adv_params);
			bleButton = false;
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            if (param->add_attr_tab.status != ESP_GATT_OK)
			{
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            }
            else if (param->add_attr_tab.num_handle != AQUARIUS_IDX_NB){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to AQUARIUS_IDX_NB(%d)", param->add_attr_tab.num_handle, AQUARIUS_IDX_NB);
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d\n",param->add_attr_tab.num_handle);
                memcpy(aquarius_handle_table, param->add_attr_tab.handles, sizeof(aquarius_handle_table));
                esp_ble_gatts_start_service(aquarius_handle_table[IDX_SVC_AQUARIUS]);
            }
            break; 
        }
        case ESP_GATTS_STOP_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        case ESP_GATTS_UNREG_EVT:
        case ESP_GATTS_DELETE_EVT:
        default:
            break;
    }
}


static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == heart_rate_profile_tab[idx].gatts_if) {
                if (heart_rate_profile_tab[idx].gatts_cb) {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}


static void checkAlarms()                   //check alarms to trigger water discharge as per received time points from user
{
  int32_t ADC_quant = 0;
  time(&calendarNow);
  localtime_r(&calendarNow, &timeinfo);
  char strftime_buf[64];
  // Set timezone to Indian Standard Time and print local time
  //setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
  setenv("TZ", "IST-5:30", 0);
  tzset();
  localtime_r(&calendarNow, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI("Checking Start Alarms","The current date/time in India is: %s. (%d) %2d:%2d", strftime_buf, timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);

  uint8_t i = 0;
  for(i=0; i<29; i++)
 {
  
	if(startAlarm[i].active && (timeinfo.tm_wday == startAlarm[i].day) && (timeinfo.tm_hour == startAlarm[i].hour) && (timeinfo.tm_min == startAlarm[i].minute)) 
	{
		ESP_LOGI("Alarm Match","Alarm 0 : Hour = %d Minute = %d", timeinfo.tm_hour, timeinfo.tm_min);
		fillingWater = true;
		
	//****************************code for time point miss diagnosis***************************
		check_day = timeinfo.tm_wday;
		check_hour = timeinfo.tm_hour + 1;
		if(check_hour == 23) 
			check_hour = 0;
		check_min = timeinfo.tm_min;
	//*****************************************************************************************

		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_0);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 1); 
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_0);
        
		ADC_current = get_units(20);
		

		startAlarm[i].active = 0;
		if(startAlarm[i].volume == 0)          //check if discharge volume in time point is zero  
		{
		
			if(ADC_prev != 0)
			{
		       ADC_res = ADC_prev - ADC_current;
			   write_nvs_data("ADC_res",ADC_res);
			}
			motor_open();					//Open solenoid on start alarm match
			waterVolume = 0;
			stopAlarm[i].active = 1;   //Active Stop alarm for related Start alarm
			stopAlarm[i].day = startAlarm[i].day;
			stopAlarm[i].hour = startAlarm[i].hour;
			stopAlarm[i].minute = startAlarm[i].minute + startAlarm[i].duration;
			if(stopAlarm[i].minute >=60) 
			{
			  stopAlarm[i].hour = startAlarm[i].hour + (stopAlarm[i].minute / 60);
			  stopAlarm[i].minute = stopAlarm[i].minute % 60;
			  if(stopAlarm[i].hour > 23) 
			  {
				  stopAlarm[i].hour = 0;
			  }
			}
			alarmStop = true;
		}
		else                       // If volume in time point is non-zero
		 {	
			waterVolume = startAlarm[i].volume;    
			weightAvailable = (int32_t)((float)(ADC_current + ADC_res) / Slope);      //calculate approx. water volume available in tubby
			if(waterVolume > 10000)                       //check if water volume is greater that 10000 or not(i.e tubby max capacity)
			{
			 actualVolume = waterVolume;
			 waterVolume = 10000;
			 printf("Water discharge limit is 10000mL..\nActual Water discharge volume: %d\n",actualVolume);
			}
			else
				actualVolume = 0;
			
			quantity = waterVolume;

			if((weightAvailable < waterVolume) && (gpio_get_level(GPIO_INPUT_IO_FLOATMAX) == 0))     //check water volume available in tubby to refill if less
			{
			 printf("Water insufficient to discharge...Please fill water\nWater discharge: %d\nWater available: %d\n",waterVolume,weightAvailable);
			 rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
			vTaskDelay(500 / portTICK_PERIOD_MS);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0); 
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
			water_refill();                            //water_refill() waits to fill water in tubby if water discharge > water available
			printf("Water ready to discharge...\n");
			vTaskDelay(10000 / portTICK_PERIOD_MS);
			}
            ADC_current = get_units(20); 
			if(ADC_prev != 0)
			{
		       ADC_res = ADC_prev - ADC_current;	
			   write_nvs_data("ADC_res",ADC_res);
			}

			ADC_quant = (int32_t)((float)waterVolume * Slope);
			ADC_target = ADC_current - ADC_quant;
			motor_open();
			printf("Water volume for Motor close: %d ml\nADC_current = %d\nADC_prev = %d\nADC_res = %d\nSlope = %.2f\nADC_quant = %d\nADC_target = %d\n",waterVolume,ADC_current,ADC_prev,ADC_res,Slope,ADC_quant,ADC_target);
		 }

       break;
    }
	else
		check_waterFlow();
	
  }
}

static void checkStopAlarms()              //check stop alarms to turn OFF water discharge as per received time points from user
{
  time(&calendarNow);
  localtime_r(&calendarNow, &timeinfo);
  char strftime_buf[64];

  // Set timezone to Indian Standard Time and print local time
  //setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
  setenv("TZ", "IST-5:30", 0);
  tzset();
  localtime_r(&calendarNow, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI("Checking Stop Alarms","The current date/time in India is: %s. (%d) %2d:%2d", strftime_buf, timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);

  if(alarmStop)
  {
	  uint8_t j=0;
	  for(j = 0; j < 29; j++)
	  {
		  alarmStop = false;	
		  if(stopAlarm[j].active && (timeinfo.tm_wday == stopAlarm[j].day) && (timeinfo.tm_hour == stopAlarm[j].hour) && (timeinfo.tm_min == stopAlarm[j].minute)) 
		 {
			ESP_LOGI("Alarm Match","Stopping Alarm : Hour = %d Minute = %d", timeinfo.tm_hour, timeinfo.tm_min);
			fillingWater = false;

			rtc_gpio_hold_dis(GPIO_OUTPUT_IO_0);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 0);  //rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_0);

			/*rtc_gpio_hold_dis(GPIO_OUTPUT_IO_1);
			rtc_gpio_set_level(GPIO_OUTPUT_IO_1, 0);  //rtc_gpio_set_level(GPIO_OUTPUT_IO_1, 0);
			rtc_gpio_hold_en(GPIO_OUTPUT_IO_1);*/

			motor_close();			//close solenoid on stop alarm

			ADC_current = get_units(20);
			ADC_prev = ADC_current + ADC_res;
			stopAlarm[j].active = 0;
			write_nvs_data("ADC_prev",ADC_prev);

			break;
		  }
		  else
			check_waterFlow();
		 
	  }
	}
}

void IRAM_ATTR BLE_advertisement_isr_handler(void* arg)           //ISR for BLE button interrupt
{
    uint32_t gpio_num = (uint32_t) arg;
	//vTaskDelay(200 / portTICK_PERIOD_MS); 
	 //xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
	 bleButton = true;
}

/*void IRAM_ATTR inlet_Sensor_isr_handler(void* arg)				 //ISR for flow sensor interrupt
{
    uint32_t gpio_num = (uint32_t) arg;
	fillCount++; 
	 //xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}*/

static void ADC_init()                                               //initialise hx711 ADC to receive data from weight sensors
{
	esp_err_t ret1;
	ret1 = gpio_set_direction(AD_SCK_PIN, GPIO_MODE_OUTPUT);
	if(ret1) {
          ESP_LOGI("GPIO","Could not set direction of GPIO 5");
          return;
        }
	ret1 = gpio_set_level(AD_SCK_PIN, 0);
	if(ret1){
          ESP_LOGI("GPIO","Could not set value 0 of GPIO 6");
          return;
        }
	ret1 = gpio_set_direction(AD_DATA_PIN, GPIO_MODE_INPUT);
	if(ret1) {
          ESP_LOGI("GPIO","Could not set direction of GPIO 6");
          return;
        }
		//gpio_intr_enable(AD_DATA_PIN);
		//gpio_set_intr_type(AD_DATA_PIN, GPIO_INTR_ANYEDGE);
    
}

static void start_BLE_advertising()
{
	uint8_t i = 0;
	if(bleButton && (!bleAdvertise))
	{
	 esp_ble_gap_start_advertising(&adv_params);  //Start BLE advertising
	 sendChar("UART Tx", "C");
	 rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
	 rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
	 vTaskDelay(200 / portTICK_PERIOD_MS);
	 rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
	 rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 
	 for(i=0; i<20; i++)
	 {
		if(fillingWater)
			water_discharge();
		else
			{
			checkAlarms();
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			}
		if(!bleButton)
			break;
	 }

	 esp_ble_gap_stop_advertising();              //Stop BLE advertising automatically after 20 secs/device connected
	 
	 if(bleConnected)
		 sendChar("UART Tx", "D");
	 else
		 sendChar("UART Tx", "E");
	 bleButton = false;
	}

}

void app_main()
{
	uint32_t t1,t2;
	esp_err_t ret;
	gpio_config_t io_conf_valve, io_conf_float, io_conf_sens;

	/* Initialize NVS. */
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
	  ESP_ERROR_CHECK(nvs_flash_erase());                             
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK( ret );

	ADC_init();    //initialise ADC settings     
    uart_init();   //initialise UART settings

	


	//***************************************************GPIO setting for solenoid valve pins********************************************************************************//
	io_conf_valve.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf_valve.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf_valve.pin_bit_mask = GPIO_OUTPUT_PIN_SEL_VALVE;
	//disable pull-up mode
    io_conf_valve.pull_up_en = 1;
	//disable pull-down mode
    io_conf_valve.pull_down_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf_valve);

	//***********GPIO float sensor pins*****************//
	io_conf_float.intr_type = GPIO_INTR_DISABLE; // GPIO_INTR_LOW_LEVEL;
	//bit mask of the pins, use GPIO4/5 here
	io_conf_float.pin_bit_mask = GPIO_INPUT_PIN_SEL_FLOAT;
	//set as input mode
	io_conf_float.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf_float.pull_up_en = 1;
	//disable pull-down mode
	io_conf_float.pull_down_en = 0;
	gpio_config(&io_conf_float);

	//***********GPIO setting Flow sensor pins*****************//
	/*io_conf_sens.intr_type = GPIO_PIN_INTR_POSEDGE;
	//bit mask of the pins, use GPIO4/5 here
	io_conf_sens.pin_bit_mask = GPIO_INPUT_PIN_SEL_FS;
	//set as input mode
	io_conf_sens.mode = GPIO_MODE_INPUT;
	//enable pull-up mode
	io_conf_sens.pull_up_en = 0;
	//disable pull-down mode
	io_conf_sens.pull_down_en = 1;
	gpio_config(&io_conf_sens); */

	  //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

	 //hook isr handler for specific gpio pin
    //gpio_isr_handler_add(GPIO_INPUT_IO_FLOWSENSOR1, inlet_Sensor_isr_handler, (void*) GPIO_INPUT_IO_FLOWSENSOR1);


    //xTaskCreate(&adc_task, "adc_task", 2048, NULL, 1, NULL);
  

	ret = gpio_set_direction(GPIO_INPUT_IO_CAL_BUTTON, GPIO_MODE_DEF_INPUT);
	if(ret) {
	  ESP_LOGI("GPIO","Could not set direction of GPIO 21");
	  return;
	}
	ret = gpio_set_intr_type(GPIO_INPUT_IO_CAL_BUTTON, GPIO_INTR_LOW_LEVEL);
	if(ret) {
	  ESP_LOGI("GPIO","Could not set interrupt type of GPIO 21");
	  return;
	}

	gpio_set_pull_mode(GPIO_INPUT_IO_CAL_BUTTON, GPIO_PULLUP_ONLY);

	ret = gpio_pullup_en(GPIO_INPUT_IO_CAL_BUTTON);
	if(ret) {
	  ESP_LOGI("GPIO","Could not enable pull up of GPIO 21");
	  return;
	}

	ret = gpio_set_direction(GPIO_OUTPUT_IO_BLE_BUTTON, GPIO_MODE_DEF_INPUT);
	if(ret) {
	  ESP_LOGI("GPIO","Could not set direction of GPIO 19");
	  return;
	}
	ret = gpio_set_intr_type(GPIO_OUTPUT_IO_BLE_BUTTON, GPIO_INTR_NEGEDGE);       //GPIO_INTR_LOW_LEVEL  GPIO_INTR_NEGEDGE
	if(ret) {
	  ESP_LOGI("GPIO","Could not set interrupt type of GPIO 19");
	  return;
	}

	gpio_set_pull_mode(GPIO_OUTPUT_IO_BLE_BUTTON, GPIO_PULLUP_ONLY);

	ret = gpio_pullup_en(GPIO_OUTPUT_IO_BLE_BUTTON);
	if(ret) {
	  ESP_LOGI("GPIO","Could not enable pull up of GPIO 16");
	  return;
	}
 
	 //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_OUTPUT_IO_BLE_BUTTON, BLE_advertisement_isr_handler, (void*) GPIO_OUTPUT_IO_BLE_BUTTON); 

	ret = gpio_set_direction(GPIO_OUTPUT_IO_WATER_INDICATOR, GPIO_MODE_OUTPUT);
	if(ret) 
	 {
       ESP_LOGI("GPIO","Could not set direction of GPIO 17");
        return;
      }
	ret = gpio_set_level(GPIO_OUTPUT_IO_WATER_INDICATOR, 0);
	if(ret)
	 {
       ESP_LOGI("GPIO","Could not set value 0 of GPIO 17");
        return;
      }

    switch (esp_sleep_get_wakeup_cause()) 
	{
      case ESP_SLEEP_WAKEUP_EXT1: {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask != 0) {
            int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
            printf("Wake up from User Button on GPIO %d\n", pin);
        } else {
            printf("Wake up from GPIO\n");
        }

        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        if (ret) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed", __func__);
            return;
        }

        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (ret) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed", __func__);
            return;
        }

        ret = esp_bluedroid_init();
        if (ret) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed", __func__);
            return;
        }

        ret = esp_bluedroid_enable();
        if (ret) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed", __func__);
            return;
        }

        ret = esp_ble_gatts_register_callback(gatts_event_handler);
        if (ret){
            ESP_LOGE(GATTS_TABLE_TAG, "gatts register error, error code = %x", ret);
            return;
        }

        ret = esp_ble_gap_register_callback(gap_event_handler);
        if (ret){
            ESP_LOGE(GATTS_TABLE_TAG, "gap register error, error code = %x", ret);
            return;
        }

        ret = esp_ble_gatts_app_register(ESP_APP_ID);
        if (ret){
            ESP_LOGE(GATTS_TABLE_TAG, "gatts app register error, error code = %x", ret);
            return;
        }

        esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
        if (local_mtu_ret){
            ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
        }

        const int ext_wakeup_pin_1 = 15;
        const uint64_t ext_wakeup_pin_1_mask = 1 << ext_wakeup_pin_1;
        const int ext_wakeup_pin_2 = 4;
        const uint64_t ext_wakeup_pin_2_mask = 1 << ext_wakeup_pin_2;

        ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pins GPIO%d, GPIO%d\n", ext_wakeup_pin_1, ext_wakeup_pin_2);
        esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);

		if(bleAdvertise)
           esp_ble_gap_stop_advertising();            //Stop BLE advertising

		printf("Current ADC weight value : %d\n",ADC_current);

        /*while(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 0)
		{
		  ADC_current = read_average(10);
		  printf("ADC weight value : %d\n",ADC_current);
		}*/
		rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
		//cal_offset(20);
		//ADCmin = get_units(20);
		//OFFSET = 268932;
		ADC_current = 0;

		//write_nvs_data("Offset",OFFSET);
		//write_nvs_data("Slope",Slope_int);
		//write_nvs_data("ADC_prev",ADC_prev);
		
		//Slope = 23.8716;
		//Slope_int = (int32_t)(Slope * 1000);
		//write_nvs_data("Slope",Slope_int);

		vTaskDelay(1000 / portTICK_PERIOD_MS);
		
		if(gpio_get_level(GPIO_INPUT_IO_FLOATMIN) == 1)
		{			
			ADC_prev = read_nvs_data("ADC_prev");
			OFFSET = read_nvs_data("Offset");
			Slope = (float)read_nvs_data("Slope") / (float)1000;
			ADC_res = read_nvs_data("ADC_res");
			Tubby_calibrate = 1;
		}
		else
		{
			printf("Tubby not calibrated...Press calibration button\n");	
			Tubby_calibrate = 0;      			// Tubby Calibration staus flag: Not calibrated
		}
		//vTaskDelay(1000 / portTICK_PERIOD_MS);
		rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
		rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ); 
		printf("weight OFFSET value : %d\n",OFFSET);
		printf("Scale value : %.2f\n",SCALE);
		printf("ADC reservoir limit,ADCmin : %d\nADCprev : %d\ADC_res : %d\nSlope : %.4f\n",ADCmin,ADC_prev,ADC_res,Slope);

		inlet_valve_close();   //close inlet valve default


        while (1) 
		{
			
			start_BLE_advertising();            //check BLE advertise button status
			pause_tubby();

			ADC_current = get_units(20);
			printf("ADC weight value : %d\n",ADC_current);
			checkAlarms();
			checkStopAlarms();
			printf("interrupt value : %d\n",fillCount);

			if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)				//if(cal == 1)
			{
				vTaskDelay(200 / portTICK_PERIOD_MS);
				if(gpio_get_level(GPIO_INPUT_IO_CAL_BUTTON) == 0)
				{
				printf("Calibration mode active\n");
				rtc_gpio_hold_dis(GPIO_OUTPUT_IO_BUZZ);
				rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 1);
				vTaskDelay(200 / portTICK_PERIOD_MS);
				rtc_gpio_set_level(GPIO_OUTPUT_IO_BUZZ, 0);
				rtc_gpio_hold_en(GPIO_OUTPUT_IO_BUZZ);
				calibration();
				}
			}
			
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			//sendChar("UART Tx", "a");

			//rx_task();

		  while(bleConnected || fillingWater) 
		  {
			water_discharge();
			start_BLE_advertising();            //check BLE advertise button status
			check_tpError();                    //check for time point skip
			pause_tubby();
			/*time(&calendarNow);
			localtime_r(&calendarNow, &timeinfo);
			char strftime_buf[64];

			// Set timezone to Indian Standard Time and print local time
			//setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
			setenv("TZ", "IST-5:30", 0);
			tzset();
			localtime_r(&calendarNow, &timeinfo);
			strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);*/
			//ESP_LOGI("Current Time","The current date/time in India is: %s. (%d) %2d:%2d", strftime_buf, timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min);
			//vTaskDelay(2000 / portTICK_PERIOD_MS);
			//HX711_power_up();
			checkAlarms();
			checkStopAlarms();
			//sendChar("UART Tx", "B");
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		   } 
         }

        break;
       }

      case ESP_SLEEP_WAKEUP_TIMER:  {
        ESP_LOGI("app_main","timer based wake up");
        vTaskDelay(2000 / portTICK_RATE_MS);

        checkAlarms();
        checkStopAlarms();
		water_discharge();

        const int ext_wakeup_pin_1 = 15;
        const uint64_t ext_wakeup_pin_1_mask = 1 << ext_wakeup_pin_1;
        const int ext_wakeup_pin_2 = 4;
        const uint64_t ext_wakeup_pin_2_mask = 1 << ext_wakeup_pin_2;
        ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_1);
        esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);
        printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
        esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);

        //ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_2);
        //esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);

        ESP_LOGI("Timer wakeup","Entering deep sleep\n");
		//countPrev = flowCount;
        esp_deep_sleep_start();
        //printf("Wake up from timer. Time spent in deep sleep: %dms\n", sleep_time_ms);
        break;
      }

      case ESP_SLEEP_WAKEUP_UNDEFINED:
      default:
        printf("Not a deep sleep reset\n");

        if(rtc_gpio_is_valid_gpio(2)) {
          ESP_LOGI("Power Up","Pin 2 is valid RTC GPIO");
        } else {
          ESP_LOGI("Power Up","Pin 2 is not valid RTC GPIO");
        }
        if(rtc_gpio_is_valid_gpio(GPIO_OUTPUT_IO_MOTOR)) {
          ESP_LOGI("Power Up","Pin 13 is valid RTC GPIO");
        } else {
          ESP_LOGI("Power Up","Pin 13 is not valid RTC GPIO");
        }
//*****************************Solenoid valve************************************
		if(rtc_gpio_is_valid_gpio(14)) {
          ESP_LOGI("Power Up","Pin 14 is valid RTC GPIO");
        } else {
          ESP_LOGI("Power Up","Pin 14 is not valid RTC GPIO");
        }
        if(rtc_gpio_is_valid_gpio(27)) {
          ESP_LOGI("Power Up","Pin 27 is valid RTC GPIO");
        } else {
          ESP_LOGI("Power Up","Pin 27 is not valid RTC GPIO");
        }
//*******************************************************************************
        ret = rtc_gpio_init(GPIO_OUTPUT_IO_0);
        if(ret) {
          ESP_LOGI("GPIO","Could not initialize GPIO 2");
          return;
        }
        ret = rtc_gpio_set_direction(GPIO_OUTPUT_IO_0, RTC_GPIO_MODE_OUTPUT_ONLY);
        if(ret) {
          ESP_LOGI("GPIO","Could not set direction of GPIO 2");
          return;
        }

        ret = rtc_gpio_pullup_en(GPIO_OUTPUT_IO_0);
        if(ret) {
          ESP_LOGI("GPIO","Could not enable pull up of GPIO 2");              
          return;
        }
     
        ret = rtc_gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        if(ret) {
          ESP_LOGI("GPIO","Could not set level of GPIO 2");
          return;
        }
        ret = rtc_gpio_hold_en(GPIO_OUTPUT_IO_0);
        if(ret) {
          ESP_LOGI("GPIO","Could not enable hold of GPIO 2");
          return;
        }

		ret = rtc_gpio_init(GPIO_OUTPUT_IO_BUZZ);
        if(ret) {
          ESP_LOGI("GPIO","Could not initialize GPIO 14");
          return;
        }
        ret = rtc_gpio_set_direction(GPIO_OUTPUT_IO_BUZZ, RTC_GPIO_MODE_OUTPUT_ONLY);
        if(ret) {
          ESP_LOGI("GPIO","Could not set direction of GPIO 14");
          return;
        }
        

//************************************************* for Motor ***************************************************************************************************************//

		ret = rtc_gpio_init(GPIO_OUTPUT_IO_MOTOR);
        if(ret) {
          ESP_LOGI("GPIO","Could not initialize GPIO 13");
          return;
        }
        ret = rtc_gpio_set_direction(GPIO_OUTPUT_IO_MOTOR, RTC_GPIO_MODE_OUTPUT_ONLY);
        if(ret) {
          ESP_LOGI("GPIO","Could not set direction of GPIO 13");
          return;
        }
        ret = rtc_gpio_pullup_en(GPIO_OUTPUT_IO_MOTOR);
        if(ret) {
          ESP_LOGI("GPIO","Could not enable pull up of GPIO 13");
          return;
        }
        ret = rtc_gpio_set_level(GPIO_OUTPUT_IO_MOTOR, 1);
        if(ret) {
          ESP_LOGI("GPIO","Could not set level of GPIO 13");
          return;
        }
        ret = rtc_gpio_hold_en(GPIO_OUTPUT_IO_MOTOR);
        if(ret) {
          ESP_LOGI("GPIO","Could not enable hold of GPIO 13");
          return;
        }

		//gpio_pulldown_dis(GPIO_INPUT_IO_CAL_BUTTON);

//****************************************************************************************************************************************************************************//

        const int ext_wakeup_pin_1 = 15;
        const uint64_t ext_wakeup_pin_1_mask = 1 << ext_wakeup_pin_1;
        const int ext_wakeup_pin_2 = 4;
        const uint64_t ext_wakeup_pin_2_mask = 1 << ext_wakeup_pin_2;

        ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_1);
        esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ALL_LOW);
        printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
        esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);

        //ESP_LOGI("External Interrupts","Enabling EXT1 wakeup on pin GPIO%d\n", ext_wakeup_pin_2);
        //esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);

        ESP_LOGI("First boot","Entering deep sleep\n");
		//countPrev = flowCount;
        esp_deep_sleep_start();


    }
}
