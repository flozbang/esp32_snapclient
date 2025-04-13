# Work in Process Snapcast Client for ESP32 based on ESP-ADF

### ESP32 Audio Client for [Snapcast](https://github.com/badaix/snapcast)


## Features
- only FLAC decodeing is supported at this moment
- mqtt support for home automation
- web interface for wifi, mqtt and audio setup
- 10 band equalizer
- full ESP-ADF support

## Description

I use the ESP32 Audio Kit from [AI-Thinker](https://github.com/Ai-Thinker-Open/ESP32-A1S-AudioKit) with the ES8388 audio codec for developing.
At the moment only this board is supported.
FLAC decoding, mqtt support, equalizer and web interface are working.
Audio sync is working but has still some flaws for example when new song is playing.


## Code base

The code is loosly based on [play_mp3](https://github.com/espressif/esp-adf/tree/master/examples/get-started/play_mp3)
example from the [esp-adf](https://github.com/espressif/esp-adf) framework and
the [snapclient](https://github.com/jorgenkraghjakobsen/snapclient)
implementation made by [@jorgenkraghjakobsen](https://github.com/jorgenkraghjakobsen)
