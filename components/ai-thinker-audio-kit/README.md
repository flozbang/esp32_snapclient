# AI-Thinker ESP32 Audio Kit custom ESP-ADF board

This component is derived from the currently working modified ESP-ADF `lyrat_v4_3` board implementation.

The first version intentionally keeps the working LyraT-derived behavior unchanged and only moves the board definition into the application repository.

## Pin configuration

| Function | GPIO |
|---|---:|
| ES8388 I2C SDA | 33 |
| ES8388 I2C SCL | 32 |
| I2S MCLK | 0 |
| I2S BCLK | 27 |
| I2S WS/LRCK | 25 |
| I2S DATA OUT | 26 |
| I2S DATA IN | 35 |
| PA ENABLE | 21 |
| Headphone detect | 39 |
| Green LED | 22 |

## Installation

Place this directory in the project's `components` directory and remove/rename the old `ai-thinker-esp32-a1s` component so that only one custom board implementation is present.

In ESP-ADF board configuration select `Custom audio board` instead of `ESP-LyraT V4.3`.

Then rebuild from a clean configuration/build as appropriate for the project.
