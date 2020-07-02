/**
 * Pin assignment:
 * - i2c:
 *    GPIO12: SDA
 *    GPIO14: SDL
 * - no need to add external pull-up resistors.
 */

#include <stdio.h>
#include "driver/i2c.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_log.h"

#include "max30100/max30100.h"
#include "ili9341.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FRQ 100000
#define I2C_PORT I2C_NUM_0

static const char *TAG = "ILI9341";

FontxFile fx16G[2];
FontxFile fx24G[2];
FontxFile fx32G[2];
TFT_t dev;

max30100_config_t max30100 = {};

static void SPIFFS_Directory(char * path) {
  DIR* dir = opendir(path);
  assert(dir != NULL);
  while (true) {
    struct dirent*pe = readdir(dir);
    if (!pe) break;
    ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
  }
  closedir(dir);
}

esp_err_t i2c_master_init(i2c_port_t i2c_port){
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.scl_io_num = I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FRQ;
    i2c_param_config(i2c_port, &conf);
    return i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
}

uint8_t display_buffer[48];
void get_bpm(void* param) {

    max30100_data_t result = {};
    while(true) {
        //Update sensor, saving to "result"
        ESP_ERROR_CHECK(max30100_update(&max30100, &result));
        if(result.pulse_detected) {
            // printf("BEAT\n");
            lcdFillScreen(&dev, BLACK);
            printf("BPM: %f | SpO2: %f%%\n", result.heart_bpm, result.spO2);
            sprintf((char *)display_buffer, "BPM: %2d", (int)result.heart_bpm);
            lcdDrawString(&dev, fx24G, 10, 50, (uint8_t *)display_buffer, RED);
            sprintf((char *)display_buffer, "SpO2: %2d%%\n",(int)result.spO2);
            lcdDrawString(&dev, fx24G, 10, 78, (uint8_t *)display_buffer, RED);
        }
        //Update rate: 100Hz
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}

void app_main()
{
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 8,
    .format_if_mount_failed =true
  };

  // Use settings defined above toinitialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is anall-in-one convenience function.
  esp_err_t ret =esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
    }
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total,&used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
  }

  SPIFFS_Directory("/spiffs/");

  spi_master_init(&dev);
  lcdInit(&dev);
  lcdFillScreen(&dev, BLACK);

  // set font file
  InitFontx(fx16G,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic
  InitFontx(fx24G,"/spiffs/ILGH24XB.FNT",""); // 12x24Dot Gothic
  InitFontx(fx32G,"/spiffs/ILGH32XB.FNT",""); // 16x32Dot Gothic


  // lcdDrawString(&dev, fx16G, 10, 10, (uint8_t *)"hello world", RED);

  //Init I2C_NUM_0
  ESP_ERROR_CHECK(i2c_master_init(I2C_PORT));
  //Init sensor at I2C_NUM_0
  ESP_ERROR_CHECK(max30100_init( &max30100, I2C_PORT,
                 MAX30100_DEFAULT_OPERATING_MODE,
                 MAX30100_DEFAULT_SAMPLING_RATE,
                 MAX30100_DEFAULT_LED_PULSE_WIDTH,
                 MAX30100_DEFAULT_IR_LED_CURRENT,
                 MAX30100_DEFAULT_START_RED_LED_CURRENT,
                 MAX30100_DEFAULT_MEAN_FILTER_SIZE,
                 MAX30100_DEFAULT_PULSE_BPM_SAMPLE_SIZE,
                 true, false ));

  //Start test task
  xTaskCreate(get_bpm, "Get BPM", 8192, NULL, 1, NULL);
}