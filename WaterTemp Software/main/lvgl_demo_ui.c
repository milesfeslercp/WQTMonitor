/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"

#define EXAMPLE_ONEWIRE_BUS_GPIO    18
#define EXAMPLE_ONEWIRE_MAX_DS18B20 3

#define EXAMPLE_MAX_CHAR_SIZE    128

static const char *TAG = "example";

#define MOUNT_POINT "/sdcard"

// Pin assignments can be set in menuconfig, see "SD SPI Example Configuration" menu.
// You can also change the pin assignments here by changing the following 4 lines.
#define PIN_NUM_MISO  CONFIG_EXAMPLE_PIN_MISO
#define PIN_NUM_MOSI  CONFIG_EXAMPLE_PIN_MOSI
#define PIN_NUM_CLK   CONFIG_EXAMPLE_PIN_CLK
#define PIN_NUM_CS    CONFIG_EXAMPLE_PIN_CS

void example_lvgl_demo_ui_1(lv_disp_t *disp_1)
{
    uint8_t test = 11;
    lv_obj_t *scr_1 = lv_disp_get_scr_act(disp_1);
    lv_obj_t *label_1 = lv_label_create(scr_1);
    lv_label_set_long_mode(label_1, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    //lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_label_set_text_fmt(label_1, "display 1\n %d", test);
    
    /* Size of the screen (if you use rotation 90 or 270, please set disp->driver->ver_res) */
    lv_obj_set_width(label_1, disp_1->driver->hor_res);
    lv_obj_align(label_1, LV_ALIGN_TOP_MID, 0, 0);
}

void example_lvgl_demo_ui_2(lv_disp_t *disp_2)
{
    uint16_t test_2 = 21;
    lv_obj_t *scr_2 = lv_disp_get_scr_act(disp_2);
    lv_obj_t *label_2 = lv_label_create(scr_2);
    lv_label_set_long_mode(label_2, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    //lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_label_set_text_fmt(label_2, "display 2\n%d", test_2);
    
    /* Size of the screen (if you use rotation 90 or 270, please set disp->driver->ver_res) */
    lv_obj_set_width(label_2, disp_2->driver->hor_res);
    lv_obj_align(label_2, LV_ALIGN_TOP_MID, 0, 0);
}

static esp_err_t s_example_write_file(const char *path, char *data)
{
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, "a+");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    fprintf(f, data);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

static esp_err_t s_example_read_file(const char *path)
{
    ESP_LOGI(TAG, "Reading file %s", path);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[EXAMPLE_MAX_CHAR_SIZE];
    fgets(line, sizeof(line), f);
    fclose(f);

    // strip newline
    char *pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    return ESP_OK;
}

esp_err_t ret;
sdmmc_card_t *card;
const char mount_point[] = MOUNT_POINT;
sdmmc_host_t host = SDSPI_HOST_DEFAULT();

void sdcard_init(void){

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    
    host.max_freq_khz = 10000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    
    
}
void temp_init()
{
    // install new 1-wire bus
    onewire_bus_handle_t bus;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
    ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d", EXAMPLE_ONEWIRE_BUS_GPIO);

    int ds18b20_device_num = 0;
    ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result = ESP_OK;

    // create 1-wire device iterator, which is used for device search
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "Device iterator created, start searching...");
    do {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK) { // found a new device, let's check if we can upgrade it to a DS18B20
            ds18b20_config_t ds_cfg = {};
            if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]) == ESP_OK) {
                ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, next_onewire_device.address);
                ds18b20_device_num++;
                if (ds18b20_device_num >= EXAMPLE_ONEWIRE_MAX_DS18B20) {
                    ESP_LOGI(TAG, "Max DS18B20 number reached, stop searching...");
                    break;
                }
            } else {
                ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);

    // set resolution for all DS18B20s
    for (int i = 0; i < ds18b20_device_num; i++) {
        // set resolution
        ESP_ERROR_CHECK(ds18b20_set_resolution(ds18b20s[i], DS18B20_RESOLUTION_12B));
    }

    // get temperature from sensors one by one
    float temperature;
    double tempdouble[ds18b20_device_num];

    lv_obj_t *scr_1;
    lv_obj_t *label_1;
    scr_1 = lv_disp_get_scr_act(disp_1);
    label_1= lv_label_create(scr_1);
    lv_obj_t *scr_2;
    lv_obj_t *label_2;
    scr_2 = lv_disp_get_scr_act(disp_2);
    label_2= lv_label_create(scr_2);
    
    int64_t current_time;
    int hours, mins, secs;
    char f_time[EXAMPLE_MAX_CHAR_SIZE];
    sdcard_init();

    const char *file_data = MOUNT_POINT"/tempdata.csv";
    char data[EXAMPLE_MAX_CHAR_SIZE];
    snprintf(data, EXAMPLE_MAX_CHAR_SIZE, "\nTime, Probe 1 Temp, Probe 2 Temp, Probe 3 Temp");
    ret = s_example_write_file(file_data, data);

    double p0_min = 200, p0_max = 0, p1_min = 200, p1_max = 0, p2_min = 200, p2_max = 0;
    if (ret != ESP_OK) {
        return;
    }
    
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");

    //deinitialize the bus after all devices are removed
    spi_bus_free(host.slot);
    char printed_data_1[EXAMPLE_MAX_CHAR_SIZE];
    char printed_data_2[EXAMPLE_MAX_CHAR_SIZE];
    char probe_data[EXAMPLE_MAX_CHAR_SIZE];
    bool unmounted = false;
    int count_till_save = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if(unmounted == true){
            // lv_label_set_text_fmt(label_1, "Safe to Remove");
            vTaskDelay(2000);

        }
        for (int i = 0; i < ds18b20_device_num; i ++) {
            ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20s[i]));
            ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
            ESP_LOGI(TAG, "temperature read from DS18B20[%d]: %.2fC", i, temperature*1.8+32);
            tempdouble[i] = (double) temperature*1.8+32;
            }
            esp_timer_get_time();
        if (1==1) {
                current_time = esp_timer_get_time()/1000000;

                hours = (current_time/3600); 
                mins = (current_time -(3600*hours))/60;
                secs = (current_time -(3600*hours)-(mins*60));
                
                // Print the time in format H:M:S
                snprintf(f_time, EXAMPLE_MAX_CHAR_SIZE, "H:M:S - %d:%d:%d\n",hours,mins,secs);
                switch (ds18b20_device_num)
                {
                case 1:
                    snprintf(printed_data_1, EXAMPLE_MAX_CHAR_SIZE, "Probe 0: %.1f", tempdouble[0]);
                    snprintf(probe_data, EXAMPLE_MAX_CHAR_SIZE, "\n%d:%d:%d,%.1f,%p,%p", hours,mins,secs, tempdouble[0], NULL, NULL);
                    if(tempdouble[0] < p0_min){p0_min = tempdouble[0];}
                    if(tempdouble[0] > p0_max){p0_max = tempdouble[0];}
                    break;
                case 2:
                    snprintf(printed_data_1, EXAMPLE_MAX_CHAR_SIZE, "Probe 0: %.1fC\nProbe 1: %.1fC", tempdouble[0], tempdouble[1]);
                    snprintf(probe_data, EXAMPLE_MAX_CHAR_SIZE, "\n%d:%d:%d,%.1f,%.1f,%p", hours,mins,secs, tempdouble[0], tempdouble[1], NULL);
                    if(tempdouble[0] < p0_min){p0_min = tempdouble[0];} if(tempdouble[1] < p1_min){p1_min = tempdouble[1];}
                    if(tempdouble[0] > p0_max){p0_max = tempdouble[0];} if(tempdouble[1] > p1_max){p1_max = tempdouble[1];}
                    break;
                case 3:
                    snprintf(printed_data_1, EXAMPLE_MAX_CHAR_SIZE, "Probe 0: %.1fC\nProbe 1: %.1fC\nProbe 2: %.1fC", tempdouble[0], tempdouble[1],tempdouble[2]);
                    snprintf(probe_data, EXAMPLE_MAX_CHAR_SIZE, "\n%d:%d:%d,%.1f,%.1f,%.1f", hours,mins,secs, tempdouble[0], tempdouble[1], tempdouble[2]);
                    if(tempdouble[0] < p0_min){p0_min = tempdouble[0];} if(tempdouble[1] < p1_min){p1_min = tempdouble[1];} if(tempdouble[2] < p2_min){p2_min = tempdouble[2];}
                    if(tempdouble[0] > p0_max){p0_max = tempdouble[0];} if(tempdouble[1] > p1_max){p1_max = tempdouble[1];} if(tempdouble[2] > p2_max){p2_max = tempdouble[2];}
                    break;
                default:
                    snprintf(printed_data_1, EXAMPLE_MAX_CHAR_SIZE, "No probe");
                    break;
                }
                snprintf(printed_data_2, EXAMPLE_MAX_CHAR_SIZE, "P0: %.1f, %.1f\nP1: %.1f, %.1f\nP2: %.1f, %.1f", p0_min, p0_max, p1_min, p1_max, p2_min, p2_max);

                lv_label_set_text_fmt(label_1, printed_data_1);
                lv_label_set_text_fmt(label_2, printed_data_2);
                snprintf(data, EXAMPLE_MAX_CHAR_SIZE, probe_data);
                ret = s_example_write_file(file_data, data);
                if (ret != ESP_OK) {
                    return;
                }
                
                ESP_LOGI(TAG, "printing to display");
                lv_label_set_long_mode(label_1, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
                lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
                lv_label_set_text(label_1, "");
                lv_label_set_text_fmt(label_1, live_output);
                
                /* Size of the screen (if you use rotation 90 or 270, please set disp->driver->ver_res) */
                lv_obj_set_width(label_1, disp_1->driver->hor_res);
                lv_obj_align(label_1, LV_ALIGN_TOP_MID, 0, 0);
                lv_obj_set_width(label_2, disp_2->driver->hor_res);
                lv_obj_align(label_2, LV_ALIGN_TOP_MID, 0, 0);
                lvgl_port_unlock();
        }

        if(count_till_save == 10){
            esp_vfs_fat_sdcard_unmount(mount_point, card);
            ESP_LOGI(TAG, "Card unmounted");
            unmounted = true;
        }
    }
}
