# Splines Module — ESP32-S3 Firmware Template

Barebones project template for new firmware targeting the Splines Eurorack module hardware.
Demonstrates all on-board peripherals with a "Hello World" display and live sensor readout.

## Hardware

| Component | Part | Interface | Notes |
|-----------|------|-----------|-------|
| MCU | ESP32-S3 Wroom-1U **N8R8** | — | 8 MB Flash, 8 MB **Octal** PSRAM @ **1.8 V** |
| Display | Waveshare 1.83" ST7789P REV2 | SPI3 @ 80 MHz | 284×240 px (controller 320×240) |
| DAC | PCM5100A | I2S @ 192 kHz | Stereo: L = V/oct S&H, R = waveform |
| ADC | MCP3208 | SPI2 @ 1 MHz | 8 ch, 12-bit, single-ended |
| F-RAM | FM25V10 | SPI2 @ 40 MHz | 1 Mbit (128 KB), 3-byte addressing |
| Encoders | — | PCNT hardware | 2× quadrature, 24-detent |
| Buttons | — | GPIO (active-low) | Hardware RC on board, no debounce needed |
| Gate | — | GPIO38 | 0/3.3 V digital out |

## Pin Map

### Buttons & Encoders

| Signal | GPIO | Notes |
|--------|------|-------|
| BTN1 | 6 | Active-low, hardware RC |
| BTN2 | 5 | Active-low, hardware RC |
| BTN3 | 4 | Active-low, hardware RC |
| ENC1_A | 7 | Quadrature A |
| ENC1_B | 15 | Quadrature B |
| ENC1_SW | 0 | Encoder 1 pushbutton |
| ENC2_A | 17 | Quadrature A |
| ENC2_B | 18 | Quadrature B |
| ENC2_SW | 16 | Encoder 2 pushbutton |

### Shared SPI Bus (SPI2_HOST)

| Signal | GPIO | Notes |
|--------|------|-------|
| MOSI | 8 | Shared |
| MISO | 9 | Shared |
| SCK | 43 | Shared — freed by using USB Serial JTAG console |
| ADC_CS | 47 | MCP3208 chip select |
| FRAM_CS | 48 | FM25V10 chip select |

### LCD (SPI3_HOST — dedicated)

| Signal | GPIO | Notes |
|--------|------|-------|
| MOSI | 11 | LCD only |
| SCK | 12 | LCD only |
| CS | 10 | |
| DC | 13 | Data/Command |
| RST | 21 | |
| BL | 14 | Backlight PWM — **LEDC channel 0** (taken by LovyanGFX) |

### I2S DAC (PCM5100A)

| Signal | GPIO | Notes |
|--------|------|-------|
| MCLK | 42 | 256× oversampling clock |
| BCK | 41 | Bit clock |
| WS | 39 | Word select (LRCK) |
| DATA | 40 | Data to DAC |

### Misc

| Signal | GPIO | Notes |
|--------|------|-------|
| GATE_OUT | 38 | Digital gate output |
| USB_D- | 19 | USB device (do not use as GPIO when USB active) |
| USB_D+ | 20 | USB device |
| I2C_SDA | 1 | Available, not currently used |
| I2C_SCL | 2 | Available, not currently used |

## Libraries

### LovyanGFX (required)

LovyanGFX is managed as a local component (not via the component registry) because it requires version pinning.

**Setup:** Copy the `components/LovyanGFX` directory from the Splines project into your project root:

```
your_project/
├── components/
│   └── LovyanGFX/     ← copy from Splines/components/LovyanGFX
├── main/
└── CMakeLists.txt
```

The `main/CMakeLists.txt` already lists `LovyanGFX` in `REQUIRES`. No other steps needed.

### ESP-IDF Components Used

All from the ESP-IDF v5.5.2 standard library — no additional downloads required:

| Component | Used for |
|-----------|---------|
| `driver` | GPIO, SPI master, I2S, PCNT, LEDC |
| `esp_timer` | Microsecond timestamps (`esp_timer_get_time()`) |
| `esp_psram` | PSRAM heap management |
| `esp_task_wdt` | Watchdog disable on Core 0 |
| `fatfs` + `wear_levelling` | FAT filesystem on flash (if needed) |
| `vfs` | Virtual filesystem layer |

## Build

```bash
# 1. Load ESP-IDF environment (required in every new terminal)
source ./setup_env.sh

# 2. Build
idf.py build

# 3. Flash and open serial monitor
idf.py flash monitor

# Exit monitor: Ctrl+]
```

**If CMake cache is stale:**
```bash
rm -f build/CMakeCache.txt && idf.py build
```

**Full clean:**
```bash
idf.py fullclean && idf.py build
```

## Critical Configuration

### PSRAM — do not change these

The N8R8 module has PSRAM wired for **Octal mode at 1.8 V**. Any deviation causes hard panics:

```
CONFIG_SPIRAM_MODE_OCT=y    # MUST be OCT — QIO will crash
CONFIG_SPIRAM_SPEED_80M=y   # 80 MHz validated stable
```

Wrong PSRAM config symptom: `Guru Meditation Error: Core 0 panic'ed (Load access fault)` immediately after boot.

### SPI buses

Two completely separate SPI buses:

- **SPI3_HOST** — LCD only. LovyanGFX owns and manages this bus. Do not add other devices to it.
- **SPI2_HOST** — ADC + F-RAM shared. Both are added as separate `spi_device_handle_t`. If accessing from multiple tasks, protect with a `SemaphoreHandle_t` mutex.

### Console / GPIO43 conflict

GPIO43 is used as the SPI2 clock (`SPI_SCK`). This conflicts with the default UART0 TX (GPIO43).
`sdkconfig.defaults` switches the console to **USB Serial JTAG**, which has no pin conflict:

```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

Do not switch back to UART0 without rerouting the SPI clock.

### LEDC channel 0 — taken by LovyanGFX

The LCD backlight is driven by `lgfx::Light_PWM` using LEDC channel 0. If you need hardware PWM for another output (e.g., audio-rate gate), use `LEDC_CHANNEL_1` or higher.

## Concurrency Model

```
Core 0 ── dsp_task  (priority 24, pinned, watchdog DISABLED inside task)
           │
           └── I2S write loop: fills DMA buffer, blocks on i2s_channel_write()
               ~5.2 µs budget per stereo sample at 192 kHz

Core 1 ── ui_task   (priority 3, pinned, watchdog ENABLED)
           │
           └── ~50 FPS display update loop; reads encoders/buttons/ADC each frame
```

**Cross-core communication:** Use `volatile` for simple flags/values. For larger structs, use double-buffering with an atomic swap pointer.

**Never call `vTaskDelay()` inside `dsp_task`.** The `i2s_channel_write()` call blocks for exactly one buffer duration — that is the timing loop.

## Memory

| Attribute | Location | Use for |
|-----------|----------|---------|
| `DRAM_ATTR` | Internal SRAM (~512 KB total) | DSP buffers, ISR data, SPI tx/rx arrays |
| `EXT_RAM_ATTR` | PSRAM (8 MB) | Large lookup tables, font caches |
| `sprite.setPsram(true)` | PSRAM | LovyanGFX sprites — call before `createSprite()` |

PSRAM has ~8× higher latency than internal SRAM. Keep DSP hot-path data in SRAM.

## Display Notes

The ST7789P controller is 320×240 but the physical glass is 284×240. Center the image by using these `pushSprite()` X offsets:

| Sprite size | `pushSprite(x, y)` |
|-------------|-------------------|
| 284×240 (full screen) | `x = 28` |
| 284×22 (top/bottom strip) | `x = 18` |

**Color quirk:** The main display object uses RGB order; sprites sometimes appear with R and B channels swapped compared to direct display draws. Test empirically. The Splines project defines corrected `_SPRITE` color constants in `lcd_init.h`.

## F-RAM Notes

The FM25V10 is a ferroelectric RAM — no erase cycle, byte-addressable, up to 40 MHz, infinite write endurance.

- Addressing is 17-bit (0x00000–0x1FFFF for 128 KB)
- Every write must be preceded by a `WREN` (0x06) command on its own CS transaction
- Reads do not require WREN
- No busy/ready polling needed — writes complete within the SPI transaction

## ADC Notes

The MCP3208 is a 12-bit successive-approximation ADC:

- Returns values 0–4095 for input range 0 V to Vref (3.3 V)
- Single-ended mode: 8 independent channels (CH0–CH7)
- CS must pulse (deassert then reassert) between channel reads
- Use polling SPI transmit (`spi_device_polling_transmit`) for the lowest latency

## Encoder Notes

PCNT counts quadrature at 4× resolution. A 24-detent encoder produces 96 counts per revolution. Divide the raw count by 4 to get detent steps:

```cpp
int raw = 0;
pcnt_unit_get_count(pcnt_enc1, &raw);
int detents = raw / 4;
```

For smooth fractional tracking, keep a remainder and accumulate:

```cpp
int delta_raw = raw - prev_raw;
detent_accum += delta_raw;
int steps = detent_accum / 4;
detent_accum %= 4;
```
