#include <stdio.h>
#include "time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"
extern "C" {
    #include "u8g2.h"
    #include "u8g2_esp32_hal.h"
}
#include "wifi.h"
#include "ntp.h"

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_DISPLAY  5     /* Time ESP32 will show the time (in seconds) */
#define TOUCH_THRESH 400
#define TOUCH_PAD TOUCH_PAD_NUM4

#define BATTERY_ADC ADC2_CHANNEL_3
#define BATTERY_CRITICAL 2400
#define BATTERY_MIN 2700
#define BATTERY_MAX 3400

#define GROUND_GPIO (gpio_num_t)25
#define POWER_GPIO (gpio_num_t)32
#define SCL (gpio_num_t)12
#define SDA (gpio_num_t)4

extern "C" {
    void app_main(void);
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    int battery_level = 0;
    adc2_config_channel_atten(BATTERY_ADC, ADC_ATTEN_2_5db);
    int tmp;
    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(adc2_get_raw(BATTERY_ADC, ADC_WIDTH_12Bit, &tmp));
        battery_level += tmp;
    }
    battery_level /= 4;
    if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        battery_level += 100; // Compensate for weirdness on boot
    }
    if (battery_level <= BATTERY_CRITICAL) { // Dangerously low
        esp_deep_sleep_start();
    }
    printf("Got battery reading %d\n", battery_level);
    int battery_scaled = 0;
    bool battery_critical = false;
    if (battery_level > BATTERY_MIN) {
        battery_scaled = (battery_level - BATTERY_MIN) * 128 / (BATTERY_MAX-BATTERY_MIN);
    } else {
        battery_scaled = (battery_level - BATTERY_CRITICAL) * 128 / (BATTERY_MIN-BATTERY_CRITICAL);
        battery_critical = true;
    }

    if (time(0) == 0) { // If time is not set (not a wake from sleep)
        bluWiFi.init();
        bluWiFi.connect("ssid", "pass");
        int c = 100;
        while (bluWiFi.state & bluWiFi.bw_connecting) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            if (--c <= 0) {
                bluWiFi.state = (bluWiFiClass::State)(bluWiFi.state & ~bluWiFi.bw_connecting);
            }
        }
        if (bluWiFi.state & bluWiFi.bw_connected) {
            printf("WiFi connected\n");

            ntp.getTime("216.239.35.0"); // time1.google.com
        }

        bluWiFi.disconnect();
        bluWiFi.deinit();
    }

    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1); //Set the timezone
    tzset(); //calculate offset and magically tell other functions what the timezone to use it
    time_t now = time(0); //Get the current time.
    struct tm timeinfo = *localtime(&now);
    char* time_string;
    if (timeinfo.tm_year < 100) {
        time_string = (char*)"Wifi Err";
    } else {
        char time_buf[10];
        strftime(time_buf, sizeof(time_buf), "%I:%M", &timeinfo);
        time_string = time_buf;
        if (*time_string == '0') {
            time_string += 1;
        }
    }
    printf("Time is %s\n", time_string);

    gpio_pad_select_gpio(POWER_GPIO);
    gpio_set_direction(POWER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(POWER_GPIO, 1);
    gpio_pad_select_gpio(GROUND_GPIO);
    gpio_set_direction(GROUND_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(GROUND_GPIO, 0);

    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.sda = SDA;
    u8g2_esp32_hal.scl = SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_t u8g2;
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure
    u8x8_SetI2CAddress(&u8g2.u8x8,0x78);

    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    u8g2_SetContrast(&u8g2, 0);
    u8g2_SetPowerSave(&u8g2, 0); // wake up display

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_inr16_mr);
    int x = (u8g2_GetDisplayWidth(&u8g2) - u8g2_GetStrWidth(&u8g2, time_string)) / 2;
    int y = (u8g2_GetDisplayHeight(&u8g2) + u8g2_GetMaxCharHeight(&u8g2)) / 2;
    u8g2_DrawStr(&u8g2, x, y, time_string);

    u8g2_DrawHLine(&u8g2, 0, battery_critical ? 63 : 0, battery_scaled);
    char battery_string[11];
    sprintf(battery_string, "%d", battery_level);
    u8g2_SetFont(&u8g2, u8g2_font_tom_thumb_4x6_tr);
    x = (u8g2_GetDisplayWidth(&u8g2) - u8g2_GetStrWidth(&u8g2, battery_string)) / 2;
    y += 10;
    u8g2_DrawStr(&u8g2, x, y, battery_string);

    u8g2_SendBuffer(&u8g2);

    gpio_hold_en(POWER_GPIO);
    gpio_hold_en(GROUND_GPIO);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup(TIME_TO_DISPLAY * uS_TO_S_FACTOR);
    esp_light_sleep_start();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

    u8g2_SetPowerSave(&u8g2, 1); // sleep display
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(POWER_GPIO);
    gpio_hold_dis(GROUND_GPIO);

    touch_pad_init();
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_set_meas_time(65535, 16383);
    touch_pad_config(TOUCH_PAD, TOUCH_THRESH);
    esp_sleep_enable_touchpad_wakeup();

    printf("Sleeping!\n");
    fflush(stdout);
    esp_deep_sleep_start();
}
