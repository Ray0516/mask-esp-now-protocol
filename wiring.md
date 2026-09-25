# ESP32-CAM manager wiring

Applies to the AI Thinker-style ESP32-CAM running the TFT manager firmware in the parent project, with the camera and microSD unused. The TFT is the 1.8-inch 128 × 160 ST7735S SPI module.

## TFT to ESP32-CAM

| TFT module label | ESP32-CAM connection | Notes |
|---|---|---|
| VCC | 3V3 | Use 3.3 V unless the exact display breakout explicitly documents a regulated 5 V input. |
| GND | GND | Common ground. |
| SCL / SCK / CLK | GPIO14 | SPI clock. |
| SDA / MOSI / DIN | GPIO13 | SPI data from ESP32 to display. Do not connect MISO. |
| CS / SS | GPIO15 | Chip select; this is also an ESP32 boot-strapping pin. Do not add a pull-down. |
| DC / A0 | GPIO2 | Data/command; also a boot-strapping pin. Do not add an external pull-up or pull-down. |
| RES / RESET | 3V3 | Firmware uses software reset (`TFT_RST = -1`). |
| BL / LED | 3V3, only if the module breakout permits | Check its silk/manual; use a series resistor if the breakout has no backlight resistor. |

The manager code's matching constants are `TFT_CS=15`, `TFT_DC=2`, `TFT_RST=-1`, `TFT_MOSI=13`, and `TFT_SCLK=14`. The TFT uses SPI mode; do not wire its SDA pin to I2C SDA.

## Buttons

Each button is wired between its GPIO and GND. The firmware uses `INPUT_PULLUP`, so the unpressed state is HIGH and pressing the button reads LOW.

| Function | ESP32-CAM GPIO | Other button contact | Caution |
|---|---:|---|---|
| Previous page | GPIO0 | GND | Boot strap: do not hold while resetting/powering on, or the board may enter download mode. |
| Next page / hold for pairing | GPIO3 (U0RXD) | GND | UART0 RX: unplug the button lead while uploading over serial; it can interfere with flashing and serial input. |

GPIO4 is intentionally left alone for the ESP32-CAM flash LED circuit. GPIO1 (U0TXD) is not used by the buttons.

## SD card conflict

The AI Thinker ESP32-CAM SD_MMC pins are CLK=14, CMD=15, D0=2, D1=4, D2=12, D3=13. The TFT occupies GPIO14, GPIO15, GPIO2, and GPIO13. SD_MMC 1-bit still requires GPIO14/15/2, so the microSD slot cannot be used with this wiring. Keep GPIO4 free as well; it is connected to the onboard flash LED.

## Mask transmitter wiring

ESP-NOW uses radio; there is no wire between the mask transmitter and manager. Power each ESP32 node appropriately and connect its sensor's SDA/SCL/data/power pins according to that exact sensor breakout's datasheet. This project has no chosen sensor model or transmitter-board model, so sensor GPIO numbers are deliberately not hard-coded here. Avoid ESP32 boot-strapping pins (GPIO0, GPIO2, GPIO5, GPIO12, GPIO15) for circuits that drive a fixed level during reset, and never feed 5 V logic into ESP32 GPIOs.

The example sender has no display or button connections. Configure its `DEVICE_ID`, `DEVICE_NAME`, `MANAGER_CHANNEL`, and the manager MAC in `mask_node_sender.ino`; both radios must use the same channel for pairing.
