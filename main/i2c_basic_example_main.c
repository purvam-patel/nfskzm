
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

static const char *TAG = "example";

#define I2C_MASTER_SCL_IO           18       /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           5       /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0                   /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ          100000                      /*!< I2C master clock frequency (100kHz) */
#define I2C_MASTER_TX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define RC522_RST_IO                 13         /*!< GPIO for RC522 Reset. Changed from 12 to avoid boot issues. */
#define RC522_SENSOR_ADDR           0x28        /*!< Change this if Scanner found a different address */
#define RC522_VERSION_REG_ADDR      0x37        /*!< Address of the Version register */

/**
 * @brief Read a sequence of bytes from RC522 sensor registers
 */
static esp_err_t rc522_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS);
}

/**
 * @brief Write a byte to a RC522 sensor register
 */
/*
static esp_err_t rc522_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS);
}
*/

/**
 * @brief i2c master initialization
 */
static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RC522_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

static void i2c_scanner(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    int devices_found = 0;
    for (uint16_t address = 1; address < 127; address++) {
        if (i2c_master_probe(bus_handle, address, 50) == ESP_OK) {
            ESP_LOGI(TAG, " - Found device at: 0x%02X", address);
            devices_found++;
        }
    }
    ESP_LOGI(TAG, "Scan complete. Found %d devices.", devices_found);
    if (devices_found == 0) {
        ESP_LOGE(TAG, "STOP! No I2C devices found.");
        ESP_LOGW(TAG, "CRITICAL: Standard RC522 modules are SPI ONLY. You MUST cut the EA trace and wire EA to 3.3V to enable I2C.");
    }
}

void app_main(void)
{
    uint8_t data[2];

    /* Initialize RC522 Reset Pin */
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << RC522_RST_IO);
    gpio_config(&io_conf);
    /* Perform Hard Reset */
    gpio_set_level(RC522_RST_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(100)); // Increased delay for stability
    gpio_set_level(RC522_RST_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(150)); // Wait for chip to wake up

    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");

    i2c_scanner(bus_handle);

    /* Probe the device to check if it is connected and responding */
    esp_err_t err = i2c_master_probe(bus_handle, RC522_SENSOR_ADDR, I2C_MASTER_TIMEOUT_MS);
    if (err == ESP_OK) {
        /* Read the RC522 Version register (0x37). Expected: 0x90, 0x91, 0x92 (or 0x12 for clones) */
        /* Read the RC522 Version register (0x37). Expected: 0x90, 0x91, 0x92 (or 0x12, 0xB2 for clones) */
        ESP_ERROR_CHECK(rc522_register_read(dev_handle, RC522_VERSION_REG_ADDR, data, 1));
        ESP_LOGI(TAG, "RC522 Version Register = 0x%02X", data[0]);
    } else {
        ESP_LOGE(TAG, "I2C Probe failed: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Device not responding. If you have not modified the PCB for I2C, this will never work.");
    }

    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
