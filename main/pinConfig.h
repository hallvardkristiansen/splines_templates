// ================== Pin Config ==================
// ESP32-S3 Wroom-1U N8R8 — Splines Eurorack Module
//
// SPI Bus Layout:
//   SPI2_HOST (HSPI): ADC + F-RAM (shared, use mutex)
//     MOSI=8  MISO=9  SCK=43  ADC_CS=47  FRAM_CS=48
//   SPI3_HOST (VSPI): LCD only (dedicated, LovyanGFX-managed)
//     MOSI=11 SCK=12  CS=10   DC=13  RST=21  BL=14

// Buttons (active-low, hardware pull-ups via RC)
#define BTN1        6
#define BTN2        5
#define BTN3        4

// Encoders (quadrature, hardware PCNT)
#define ENC1_A      7
#define ENC1_B      15
#define ENC1_SW     0
#define ENC2_A      17
#define ENC2_B      18
#define ENC2_SW     16

// Shared SPI bus (ADC + F-RAM)
#define SPI_MOSI    8
#define SPI_MISO    9
#define SPI_SCK     43
#define ADC_CS      47   // MCP3208 chip select
#define FRAM_CS     48   // FM25V10 chip select

// USB D+/D- (do not use as GPIO when USB is active)
#define USB_DN      19
#define USB_DP      20

// I2C (available, not currently used)
#define I2C_SDA     1
#define I2C_SCL     2
#define I2C_INT     44

// LCD (SPI3_HOST — dedicated bus, LovyanGFX manages these)
#define LCD_CS_PIN  10
#define LCD_MOSI_PIN 11
#define LCD_SCK_PIN 12
#define LCD_DC_PIN  13
#define LCD_BL_PIN  14   // Backlight PWM (LEDC channel 0, managed by LovyanGFX)
#define LCD_RST_PIN 21

// I2S DAC (PCM5100A)
// MCLK=256× oversampling clock (not required by PCM5100 but useful for sync)
#define I2S_SCK     42   // MCLK
#define I2S_BCK     41   // Bit clock
#define I2S_DATA    40   // Data out
#define I2S_WS      39   // Word select (L/R clock)

// Gate output
// NOTE: LovyanGFX uses LEDC channel 0 for backlight.
//       If using hardware PWM (LEDC) for gate, start from LEDC_CHANNEL_1.
#define GATE_OUT    38
