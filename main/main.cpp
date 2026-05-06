/*!
 * @file main.cpp
 *
 * Splines Module — "Hello World" template
 *
 * Demonstrates all on-board peripherals:
 *   - LCD (LovyanGFX, SPI3, 80 MHz)
 *   - Encoders ENC1 / ENC2 (PCNT hardware quadrature)
 *   - Buttons BTN1 / BTN2 / BTN3 (active-low GPIO)
 *   - ADC MCP3208 (SPI2, 1 MHz, 8-channel 12-bit)
 *   - DAC PCM5100A (I2S at 192 kHz, stereo, silence)
 *   - F-RAM FM25V10 (SPI2, 40 MHz, write/read test)
 *   - Gate output GPIO38 (1 Hz toggle)
 *
 * Concurrency model:
 *   Core 0 — dsp_task  (priority 24, watchdog DISABLED)
 *   Core 1 — ui_task   (priority  3, watchdog enabled)
 *
 * Build:
 *   source ./setup_env.sh
 *   idf.py build flash monitor
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "driver/pcnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#include "pinConfig.h"
#include "lgfx_config.h"

static const char* TAG = "template";

// ============================================================
// Shared state (volatile for cross-core visibility)
// ============================================================
static volatile int     enc1_count   = 0;
static volatile int     enc2_count   = 0;
static volatile bool    btn1         = false;
static volatile bool    btn2         = false;
static volatile bool    btn3         = false;
static volatile uint16_t adc_ch0    = 0;   // Raw 12-bit ADC value from channel 0

// ============================================================
// Peripheral handles
// ============================================================
static pcnt_unit_handle_t  pcnt_enc1 = NULL;
static pcnt_unit_handle_t  pcnt_enc2 = NULL;
static spi_device_handle_t spi_adc   = NULL;
static spi_device_handle_t spi_fram  = NULL;
static i2s_chan_handle_t   i2s_tx    = NULL;

// I2S DMA buffer (in fast internal SRAM)
#define I2S_SAMPLE_RATE  192000
#define I2S_BUF_SAMPLES  256
static DRAM_ATTR int16_t i2s_buf[I2S_BUF_SAMPLES * 2];  // stereo: L, R, L, R ...

// ============================================================
// Encoder init (PCNT hardware quadrature)
// ============================================================
// Encoders are 24-detent quadrature. PCNT counts at 4× resolution,
// so divide pcnt_unit_get_count() by 4 to get detent steps.
static esp_err_t init_encoders(void) {
    auto init_one = [](pcnt_unit_handle_t* out, int pin_a, int pin_b) {
        pcnt_unit_config_t cfg = { .low_limit = -32768, .high_limit = 32767 };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, out));

        pcnt_chan_config_t ch_a = { .edge_gpio_num = pin_a, .level_gpio_num = pin_b };
        pcnt_chan_config_t ch_b = { .edge_gpio_num = pin_b, .level_gpio_num = pin_a };
        pcnt_channel_handle_t ha, hb;
        pcnt_new_channel(*out, &ch_a, &ha);
        pcnt_new_channel(*out, &ch_b, &hb);

        pcnt_channel_set_edge_action(ha, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(ha, PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
        pcnt_channel_set_edge_action(hb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
        pcnt_channel_set_level_action(hb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

        pcnt_unit_enable(*out);
        pcnt_unit_start(*out);
    };

    init_one(&pcnt_enc1, ENC1_A, ENC1_B);
    init_one(&pcnt_enc2, ENC2_A, ENC2_B);
    ESP_LOGI(TAG, "Encoders initialized");
    return ESP_OK;
}

// ============================================================
// Button init (active-low, hardware RC on board)
// ============================================================
static esp_err_t init_buttons(void) {
    const int pins[] = { BTN1, BTN2, BTN3, ENC1_SW, ENC2_SW };
    for (int p : pins) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << p,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   // hardware RC present, SW pullup as backup
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }
    ESP_LOGI(TAG, "Buttons initialized");
    return ESP_OK;
}

// ============================================================
// Shared SPI2 bus init (ADC + F-RAM)
// ============================================================
// Both devices share SPI2_HOST. Add them as separate spi_device handles.
// Use spi_device_acquire_bus() / spi_device_release_bus() when switching
// between 1 MHz (ADC) and 40 MHz (F-RAM) if doing concurrent access from
// multiple tasks; for single-task access polling_transmit is safe without a mutex.
static esp_err_t init_spi2_bus(void) {
    spi_bus_config_t bus = {
        .mosi_io_num   = SPI_MOSI,
        .miso_io_num   = SPI_MISO,
        .sclk_io_num   = SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    // MCP3208 — 1 MHz
    spi_device_interface_config_t adc_dev = {
        .mode            = 0,
        .clock_speed_hz  = 1000000,
        .spics_io_num    = ADC_CS,
        .queue_size      = 1
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &adc_dev, &spi_adc));

    // FM25V10 — 40 MHz (max rated)
    spi_device_interface_config_t fram_dev = {
        .mode            = 0,
        .clock_speed_hz  = 40000000,
        .spics_io_num    = FRAM_CS,
        .queue_size      = 1
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &fram_dev, &spi_fram));

    ESP_LOGI(TAG, "SPI2 bus initialized (ADC @ 1 MHz, F-RAM @ 40 MHz)");
    return ESP_OK;
}

// ============================================================
// MCP3208 ADC read (single-ended, channel 0–7)
// ============================================================
static uint16_t adc_read(uint8_t channel) {
    if (channel > 7) return 0;
    static DRAM_ATTR uint8_t tx[3], rx[3];
    tx[0] = 0x06 | (channel >> 2);
    tx[1] = (channel & 0x03) << 6;
    tx[2] = 0x00;
    spi_transaction_t t = {};
    t.length    = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(spi_adc, &t);
    return ((rx[1] & 0x0F) << 8) | rx[2];
}

// ============================================================
// FM25V10 F-RAM read/write (3-byte addressing)
// ============================================================
// FM25V10 command bytes
#define FRAM_CMD_WREN  0x06   // Write enable (must precede every write)
#define FRAM_CMD_WRITE 0x02
#define FRAM_CMD_READ  0x03

static void fram_write_enable(void) {
    uint8_t cmd = FRAM_CMD_WREN;
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(spi_fram, &t);
}

// Write a single byte to a 17-bit address (max 0x1FFFF for 1Mbit device)
static void fram_write_byte(uint32_t addr, uint8_t data) {
    fram_write_enable();
    uint8_t tx[5] = {
        FRAM_CMD_WRITE,
        (uint8_t)((addr >> 16) & 0x01),
        (uint8_t)((addr >>  8) & 0xFF),
        (uint8_t)( addr        & 0xFF),
        data
    };
    spi_transaction_t t = {};
    t.length    = 5 * 8;
    t.tx_buffer = tx;
    spi_device_polling_transmit(spi_fram, &t);
}

static uint8_t fram_read_byte(uint32_t addr) {
    uint8_t tx[5] = {
        FRAM_CMD_READ,
        (uint8_t)((addr >> 16) & 0x01),
        (uint8_t)((addr >>  8) & 0xFF),
        (uint8_t)( addr        & 0xFF),
        0x00
    };
    uint8_t rx[5] = {};
    spi_transaction_t t = {};
    t.length    = 5 * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(spi_fram, &t);
    return rx[4];
}

// ============================================================
// PCM5100A I2S init (192 kHz, 16-bit stereo, silent)
// ============================================================
// PCM5100A is I2S slave — ESP32-S3 is master.
// Left channel  = CV output (V/oct or S&H)
// Right channel = waveform output
// MCLK (I2S_SCK, GPIO42) is optional for PCM5100 but needed for some clock modes.
static esp_err_t init_i2s(void) {
    i2s_chan_config_t chan_cfg = {
        .id           = I2S_NUM_0,
        .role         = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = I2S_BUF_SAMPLES,
        .auto_clear   = true
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = I2S_SAMPLE_RATE,
            .clk_src        = I2S_CLK_SRC_DEFAULT,  // PLL_F240M
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_SCK,    // GPIO 42 — MCLK
            .bclk = (gpio_num_t)I2S_BCK,    // GPIO 41 — Bit clock
            .ws   = (gpio_num_t)I2S_WS,     // GPIO 39 — Word select
            .dout = (gpio_num_t)I2S_DATA,   // GPIO 40 — Data to DAC
            .din  = (gpio_num_t)-1,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx));

    ESP_LOGI(TAG, "I2S DAC initialized at %d Hz stereo", I2S_SAMPLE_RATE);
    return ESP_OK;
}

// ============================================================
// Gate output init
// ============================================================
static esp_err_t init_gate(void) {
    // Drive low immediately — GPIO38 floats until configured.
    gpio_set_direction((gpio_num_t)GATE_OUT, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)GATE_OUT, 0);
    return ESP_OK;
}

// ============================================================
// DSP task — Core 0, priority 24
// ============================================================
// Replace the silence loop with your audio generation logic.
// Budget: ~5.2 µs per stereo sample at 192 kHz.
// Rules:
//   - No FreeRTOS blocking calls (no vTaskDelay, no xQueueReceive)
//   - No heap allocation
//   - No tanhf() or other slow math in the hot path
//   - Use DRAM_ATTR for any buffers accessed here
static void dsp_task(void* param) {
    // Disable watchdog on Core 0 (DSP core must never yield)
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

    size_t written = 0;
    while (1) {
        // Fill buffer with silence (0V on both channels)
        memset(i2s_buf, 0, sizeof(i2s_buf));

        // ── Your DSP code goes here ──────────────────────────────
        // For a 440 Hz sine wave on the right channel:
        //   static uint32_t phase = 0;
        //   for (int i = 0; i < I2S_BUF_SAMPLES; i++) {
        //       float v = sinf(2.0f * M_PI * phase / (float)I2S_SAMPLE_RATE);
        //       phase = (phase + 440) % I2S_SAMPLE_RATE;
        //       i2s_buf[i * 2 + 0] = 0;                    // L: CV (silence)
        //       i2s_buf[i * 2 + 1] = (int16_t)(v * 32767); // R: waveform
        //   }
        // ────────────────────────────────────────────────────────

        // Write to I2S DMA — blocks until DMA accepts the buffer
        i2s_channel_write(i2s_tx, i2s_buf, sizeof(i2s_buf), &written, portMAX_DELAY);
    }
}

// ============================================================
// UI task — Core 1, priority 3
// ============================================================
static LGFX_Sprite sprite(&display);   // Full-screen sprite for flicker-free rendering

static void ui_task(void* param) {
    // ── Init display ────────────────────────────────────────────
    display.init();
    display.setRotation(3);             // Landscape, USB connector on left
    display.setBrightness(128);         // ~50% brightness
    display.fillScreen(TFT_BLACK);

    // Sprite in PSRAM for off-screen rendering
    sprite.setPsram(true);
    sprite.createSprite(284, 240);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.setTextSize(1);

    // ── Gate toggle state ───────────────────────────────────────
    bool gate_state     = false;
    int64_t last_toggle = esp_timer_get_time();

    // ── PCNT edge tracking (for divide-by-4) ────────────────────
    int raw1_prev = 0, raw2_prev = 0;
    int det1 = 0, det2 = 0;   // Detent counts (divide-by-4)

    while (1) {
        int64_t now = esp_timer_get_time();

        // ── Read encoders ───────────────────────────────────────
        int raw1 = 0, raw2 = 0;
        pcnt_unit_get_count(pcnt_enc1, &raw1);
        pcnt_unit_get_count(pcnt_enc2, &raw2);
        det1 += (raw1 - raw1_prev) / 4;
        det2 += (raw2 - raw2_prev) / 4;
        raw1_prev = raw1 - ((raw1 - raw1_prev) % 4);  // keep remainder
        raw2_prev = raw2 - ((raw2 - raw2_prev) % 4);
        enc1_count = det1;
        enc2_count = det2;

        // ── Read buttons (active-low) ───────────────────────────
        btn1 = !gpio_get_level((gpio_num_t)BTN1);
        btn2 = !gpio_get_level((gpio_num_t)BTN2);
        btn3 = !gpio_get_level((gpio_num_t)BTN3);

        // ── Read ADC channel 0 (once per frame) ────────────────
        adc_ch0 = adc_read(0);

        // ── Toggle gate at 1 Hz ─────────────────────────────────
        if (now - last_toggle >= 500000) {   // 500 ms
            gate_state = !gate_state;
            gpio_set_level((gpio_num_t)GATE_OUT, gate_state ? 1 : 0);
            last_toggle = now;
        }

        // ── Render ──────────────────────────────────────────────
        sprite.fillSprite(TFT_BLACK);

        // Title
        sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString("Hello World", 142, 10, 4);

        // Encoders
        char buf[64];
        sprite.setTextColor(TFT_WHITE, TFT_BLACK);
        sprite.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "ENC1: %d", enc1_count);
        sprite.drawString(buf, 20, 60, 2);
        snprintf(buf, sizeof(buf), "ENC2: %d", enc2_count);
        sprite.drawString(buf, 20, 80, 2);

        // Buttons
        snprintf(buf, sizeof(buf), "BTN1:%s  BTN2:%s  BTN3:%s",
                 btn1 ? "ON" : "--", btn2 ? "ON" : "--", btn3 ? "ON" : "--");
        sprite.drawString(buf, 20, 110, 2);

        // ADC
        // 12-bit (0–4095) → voltage: value * 3.3 / 4095 (MCP3208 Vref = 3.3 V)
        float volts = adc_ch0 * 3.3f / 4095.0f;
        snprintf(buf, sizeof(buf), "ADC ch0: %d  (%.2f V)", adc_ch0, volts);
        sprite.drawString(buf, 20, 140, 2);

        // Gate
        sprite.setTextColor(gate_state ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
        snprintf(buf, sizeof(buf), "Gate: %s", gate_state ? "HIGH" : "low");
        sprite.drawString(buf, 20, 170, 2);

        // Push sprite to display.
        // X offset 0 centres the 284-px sprite on the 320-px controller (28px each side).
        // Adjust the X offset here if the image appears shifted:
        //   Top/bottom segments: pushSprite(18, y_offset)
        //   Full-screen sprite : pushSprite(28, 0)
        sprite.pushSprite(28, 0);

        vTaskDelay(pdMS_TO_TICKS(20));   // ~50 FPS
    }
}

// ============================================================
// app_main — runs on Core 1 before FreeRTOS scheduler
// ============================================================
extern "C" void app_main(void) {
    // Drive gate low before anything else — GPIO38 floats during boot
    init_gate();

    ESP_LOGI(TAG, "=== Splines Module Template ===");

    // Initialize peripherals
    ESP_ERROR_CHECK(init_buttons());
    ESP_ERROR_CHECK(init_encoders());
    ESP_ERROR_CHECK(init_spi2_bus());
    ESP_ERROR_CHECK(init_i2s());

    // ── F-RAM self-test ─────────────────────────────────────────
    const uint32_t test_addr = 0x000000;
    const uint8_t  test_val  = 0xA5;
    fram_write_byte(test_addr, test_val);
    uint8_t readback = fram_read_byte(test_addr);
    if (readback == test_val) {
        ESP_LOGI(TAG, "F-RAM OK: wrote 0x%02X, read 0x%02X", test_val, readback);
    } else {
        ESP_LOGE(TAG, "F-RAM FAIL: wrote 0x%02X, read 0x%02X", test_val, readback);
    }

    // ── Spawn tasks ─────────────────────────────────────────────
    // DSP task: Core 0, highest priority, watchdog disabled inside task
    xTaskCreatePinnedToCore(
        dsp_task,   // function
        "dsp",      // name (visible in monitor)
        4096,       // stack bytes
        NULL,       // parameter
        24,         // priority (24 = highest practical)
        NULL,       // handle (not needed)
        0           // core 0
    );

    // UI task: Core 1, low priority
    xTaskCreatePinnedToCore(
        ui_task,
        "ui",
        8192,       // larger stack — LovyanGFX needs headroom
        NULL,
        3,
        NULL,
        1           // core 1
    );

    // app_main can return; the scheduler takes over
}
