# Firmware

The ESP32 sketch is in [`smart_home/smart_home.ino`](smart_home/smart_home.ino).

## Before you upload it to the board

1. Install the **ESP32 board package** (Espressif) and the **SinricPro** library with Arduino IDE.
2. In `smart_home/`, copy `secrets.h.example` to `secrets.h` and fill in your values.
3. Open the top of `smart_home.ino` and change the **pins** and **settings** to match your wiring:
   - `RELAY_ACTIVE_LOW` - set to `false` if your relay switches ON with a HIGH signal.
   - `DARK_IS_HIGH` - set to `false` if your LDR reading goes down when it gets dark.
   - `DARK_ON_LEVEL` and `BRIGHT_OFF_LEVEL` - choose them using the values printed in the Serial Monitor.
4. Select **ESP32 Dev Module**, choose the COM port and click **Upload**.

## Choosing the light levels

With `PRINT_LDR` set to `true`, the Serial Monitor shows the darkness level every 2 seconds.
Note the value in a bright room and in a dark room, and set `BRIGHT_OFF_LEVEL` and `DARK_ON_LEVEL`
between them, with a gap so the light does not flicker.

## Keep your keys safe

`secrets.h` is listed in `.gitignore`, so it is not uploaded to GitHub.
If a key was uploaded by mistake, delete it in the Sinric Pro dashboard and create a new one.
