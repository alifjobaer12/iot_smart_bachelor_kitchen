# ESP32 Smart Kitchen IoT System

An ESP32-based smart kitchen prototype that combines environmental monitoring, safety automation, touchless appliances, meal information, and cloud communication in one system.

The firmware reads temperature, humidity, gas, flame, motion, and distance sensors; controls relays, lights, a buzzer, and two servos; displays live information on an ST7789 TFT; and communicates with a web backend over Wi-Fi.

> [!IMPORTANT]
> This repository contains prototype firmware, not a certified fire, gas, or life-safety system. Do not rely on it as the only protection against fire, gas leaks, or other hazards.

## Table of contents

- [Main features](#main-features)
- [How the system works](#how-the-system-works)
- [Hardware requirements](#hardware-requirements)
- [Complete wiring](#complete-wiring)
- [Power and electrical safety](#power-and-electrical-safety)
- [Software requirements](#software-requirements)
- [Installation and upload](#installation-and-upload)
- [Configuration](#configuration)
- [Display and controls](#display-and-controls)
- [Automation rules](#automation-rules)
- [Backend API contract](#backend-api-contract)
- [Project structure](#project-structure)
- [Serial monitoring](#serial-monitoring)
- [Troubleshooting](#troubleshooting)
- [Known limitations and security notes](#known-limitations-and-security-notes)

## Main features

- Displays meal menus and meal counts for breakfast, lunch, and dinner.
- Displays live temperature, humidity, and gas readings.
- Fetches network time using NTP and shows Dhaka time (UTC+6).
- Shows the current Wi-Fi connection status.
- Detects flame and high gas readings as emergency conditions.
- Activates an alarm buzzer and emergency servo during an emergency.
- Turns on the exhaust fan when the temperature is high or gas is detected.
- Activates a water-pump relay when a hand is near the tap or flame is detected.
- Opens a touchless dustbin using an ultrasonic sensor and servo.
- Turns on a light when motion is detected and turns it off after one minute without motion.
- Sends sensor readings to a backend every 60 seconds.
- Lets a user send a `cooking_done` event with a physical button.
- Continues its local hardware automation if Wi-Fi is unavailable.

## How the system works

The firmware divides work between the ESP32's two processor cores:

| Execution context | Responsibility |
| --- | --- |
| Core 1 (`loop`) | Reads sensors, evaluates safety conditions, drives relays, servos, the light, and the buzzer, and prints readings to Serial. |
| Core 0 (`TaskUI_Network`) | Manages Wi-Fi, NTP time, the TFT interface, button input, and backend API requests. |

Sensor values shared between the two cores are protected by a FreeRTOS mutex. This prevents the display/network task from reading temperature, humidity, gas, or emergency state while the hardware loop is updating them.

Normal display pages are overridden by a red warning screen whenever the flame sensor or gas logic reports an emergency. Once the emergency clears, the firmware returns to the previously selected normal page.

## Hardware requirements

### Controller and user interface

- ESP32 development board
- 2.8-inch ST7789 SPI TFT display, 320 × 240 pixels
- 2 momentary push buttons
- 2 × 10 kΩ resistors for button pull-downs

### Sensors

- DHT11 temperature and humidity sensor
- MQ-2 gas/smoke sensor module
- Flame sensor module
- PIR motion sensor
- 2 ultrasonic distance sensors, one for the tap and one for the dustbin

### Actuators

- 2 relay channels, for the tap/water pump and exhaust fan
- LED or suitable lighting module
- Active buzzer
- 2 servo motors, for the dustbin and emergency mechanism
- Water pump/valve, fan, and any mechanical parts appropriate to the prototype
- External regulated power supply suitable for the relays, servos, pump, and fan

## Complete wiring

The following table reflects the pin definitions in [`code/code.ino`](code/code.ino).

### ST7789 SPI display

| Display pin | ESP32 connection | Purpose |
| --- | --- | --- |
| VCC | 3.3 V | Display logic power |
| GND | GND | Ground |
| CS | GPIO 5 | SPI chip select |
| RESET / RES | GPIO 15 | Display reset |
| DC / RS | GPIO 2 | Data/command selection |
| SDA / SDI / MOSI | GPIO 23 | SPI data from ESP32 |
| SCL / SCK | GPIO 18 | SPI clock |
| BLK / LED | 3.3 V | Backlight always on |

The firmware does not use MISO or touch-controller pins. The display is initialized as `240 × 320` and rotated into landscape orientation.

### Push buttons

| Function | ESP32 pin | Required connection |
| --- | --- | --- |
| Change display page | GPIO 34 | Button to 3.3 V and external 10 kΩ pull-down to GND |
| Cooking completed | GPIO 35 | Button to 3.3 V and external 10 kΩ pull-down to GND |

GPIO 34 and GPIO 35 are input-only pins and do not provide internal pull-up or pull-down resistors. The external resistors are required to prevent floating input values.

### Sensors

| Sensor/function | ESP32 pin | Direction |
| --- | --- | --- |
| PIR motion output | GPIO 36 (VP) | Input |
| Flame sensor digital output | GPIO 39 (VN) | Input |
| MQ-2 analog output | GPIO 32 | Analog input |
| DHT11 data | GPIO 33 | Digital input |
| Tap ultrasonic TRIG | GPIO 25 | Output |
| Tap ultrasonic ECHO | GPIO 26 | Input |
| Dustbin ultrasonic TRIG | GPIO 27 | Output |
| Dustbin ultrasonic ECHO | GPIO 14 | Input |

### Actuators

| Component/function | ESP32 pin | Direction |
| --- | --- | --- |
| Tap/water-pump relay | GPIO 12 | Output |
| Exhaust-fan relay | GPIO 4 | Output |
| Motion-controlled light | GPIO 16 | Output |
| Active buzzer | GPIO 17 | Output |
| Dustbin servo signal | GPIO 21 | PWM output |
| Emergency servo signal | GPIO 22 | PWM output |

## Power and electrical safety

1. Do not power motors, servos, pumps, fans, or relay coils directly from the ESP32's 3.3 V pin. Use a separate regulated supply with enough current for the connected loads.
2. Connect the external supply ground to ESP32 GND so that all signal voltages have the same reference.
3. ESP32 GPIO pins are **not 5 V tolerant**. If an ultrasonic sensor's ECHO pin outputs 5 V, use a voltage divider or logic-level converter before the ESP32 input.
4. Confirm that sensor outputs and relay inputs are compatible with 3.3 V logic.
5. Use a current-limiting resistor and a suitable driver if GPIO 16 controls more than a small LED.
6. Use relay isolation, flyback protection, suitable fuses, and correctly rated wiring for inductive or high-current loads.
7. Keep mains voltage away from breadboards and exposed low-voltage wiring. Mains wiring should be completed only by someone qualified to do it safely.
8. GPIO 12 is an ESP32 boot-strapping pin. Some relay modules can force it to the wrong level during startup and prevent booting. If that occurs, isolate the relay input or redesign the pin assignment before deploying the circuit.

## Software requirements

### Development tools

- Arduino IDE 2.x or Arduino CLI
- Espressif ESP32 board package
- A data-capable USB cable and the correct serial driver for the ESP32 board

### Arduino libraries

Install these libraries through **Arduino IDE → Tools → Manage Libraries**:

| Library | Header used by the firmware | Notes |
| --- | --- | --- |
| ArduinoJson | `ArduinoJson.h` | Parses meal data returned by the backend. The code uses the ArduinoJson 6 `DynamicJsonDocument` API. |
| Adafruit GFX Library | `Adafruit_GFX.h` | Graphics primitives and text rendering. |
| Adafruit ST7735 and ST7789 Library | `Adafruit_ST7789.h` | ST7789 display driver. |
| DHT sensor library by Adafruit | `DHT.h` | Reads the DHT11. |
| Adafruit Unified Sensor | Indirect dependency | Common dependency of the DHT library. |
| ESP32Servo | `ESP32Servo.h` | Generates the two servo control signals. |

`WiFi`, `WiFiClientSecure`, `HTTPClient`, `SPI`, `time`, and FreeRTOS support are supplied by the ESP32 Arduino core.

## Installation and upload

1. Clone or download this repository.
2. Open [`code/code.ino`](code/code.ino) in Arduino IDE.
3. Install the ESP32 board package and all libraries listed above.
4. In **Tools → Board**, select the ESP32 board that matches your hardware. For a common generic board, use **ESP32 Dev Module**.
5. Select the correct serial port.
6. Copy `code/secrets.example.h` to `code/secrets.h`, then enter the Wi-Fi details as described in the next section.
7. Complete and verify the wiring before applying actuator power.
8. Compile the sketch with **Verify**.
9. Upload it to the ESP32.
10. Open Serial Monitor at **115200 baud** and reset the board.

If uploading does not begin automatically, hold the board's **BOOT** button while the IDE starts the upload, then release it when writing begins. The exact procedure depends on the ESP32 board.

## Configuration

General configuration values are near the top of [`code/code.ino`](code/code.ino). Private Wi-Fi settings are kept separately in `code/secrets.h`.

### Wi-Fi

1. Copy [`code/secrets.example.h`](code/secrets.example.h) to `code/secrets.h`.
2. Open the new `code/secrets.h` file.
3. Set `ssid` and `password` to the network the ESP32 should use.
4. Set `apiGetMeals`, `apiPostSensors`, and `apiPostCooking` to the HTTPS endpoints from your backend deployment.
5. Keep `code/secrets.h` private. It is excluded by [`.gitignore`](.gitignore) and must not be committed.

A 2.4 GHz Wi-Fi network is normally required by ESP32 boards. The public example file contains placeholders and is safe to commit.

> [!CAUTION]
> Credentials previously stored directly in the sketch must be treated as exposed if an earlier copy or commit was shared. Change that Wi-Fi password before publishing the repository. Removing a password in a later commit does not remove it from Git history.

### Backend URLs

The local `code/secrets.h` file defines these backend endpoints:

| Operation | Endpoint |
| --- | --- |
| Fetch meal information | `apiGetMeals` |
| Send sensor readings | `apiPostSensors` |
| Send cooking-completed event | `apiPostCooking` |

Real endpoint URLs are intentionally excluded from Git. Anyone cloning the project must copy `code/secrets.example.h` to `code/secrets.h` and replace the example domain with their backend domain.

### Thresholds and timings

| Setting | Current value | Effect |
| --- | ---: | --- |
| `GAS_THRESHOLD` | `15000` | A reading above this value is treated as a gas leak. See the limitation below. |
| `TEMP_THRESHOLD` | `32.0 °C` | Turns on the exhaust fan at or above this temperature. |
| `DISTANCE_THRESHOLD` | `15 cm` | Activates the tap or opens the dustbin below this distance. |
| `LED_DELAY` | `60000 ms` | Turns the light off after one minute without detected motion. |
| Sensor/backend interval | `60000 ms` | Posts readings and refreshes meals once per minute while connected. |
| Page-button debounce | `300 ms` | Prevents repeated page changes from one press. |
| Cooking-button debounce | `1000 ms` | Prevents repeated cooking events from one press. |

MQ-2 readings vary by sensor, supply voltage, warm-up time, load resistor, environment, and ADC configuration. Calibrate the threshold using measurements from the actual circuit; do not treat a raw ADC value as a calibrated gas concentration.

## Display and controls

### Startup

The display shows `Connecting to Wi-Fi...` while the firmware waits for up to approximately 10 seconds. If connection fails, local automation and the display continue in offline mode. The network task keeps attempting to reconnect.

### Meal page

The default page displays:

- Date and time
- Wi-Fi status (`WIFI OK` or `NO WIFI`)
- Breakfast, lunch, and dinner counts
- Menu text for each meal
- Total meal count

### Sensor page

The sensor page displays:

- Date and time
- Wi-Fi status
- Temperature in degrees Celsius
- Relative humidity percentage
- Raw MQ-2 gas ADC reading

### Warning page

When flame or gas is detected, the normal pages are replaced by a red `WARNING! FIRE/GAS DETECTED` screen. The page selector does not override this safety screen.

### Buttons

- Press **Page Toggle** to switch between the meal page and sensor page.
- Press **Cooking Done** to send `{"status":"cooking_done"}` to the backend. This requires an active Wi-Fi connection.

## Automation rules

| Condition | Automatic response |
| --- | --- |
| Tap distance is below 15 cm | Tap/water-pump relay turns on. |
| Flame is detected | Tap/water-pump relay turns on. |
| Dustbin distance is below 15 cm | Dustbin servo moves to 90°; otherwise it returns to 0°. |
| PIR motion is detected | Light turns on and the one-minute off timer restarts. |
| No motion for one minute | Light turns off. |
| Temperature is at least 32 °C | Exhaust-fan relay turns on. |
| Gas reading is above the gas threshold | Exhaust fan, buzzer, warning screen, and emergency servo activate. |
| Flame is detected | Buzzer, warning screen, and emergency servo activate. |
| No flame or gas emergency | Buzzer turns off and emergency servo returns to 0°. |

The relay logic is written as active HIGH. If a connected relay module is active LOW, its physical behavior will be inverted unless the firmware or interface circuitry is adapted.

The flame input is also interpreted as active HIGH. Many flame sensor modules are active LOW, so verify the module's output before testing the emergency behavior.

## Backend API contract

### Fetch meals

Request:

```http
GET /api/meal
```

Expected JSON response:

```json
{
  "bfast_count": 12,
  "lunch_count": 20,
  "dinner_count": 18,
  "bfast_menu": "Egg and bread",
  "lunch_menu": "Rice and chicken",
  "dinner_menu": "Rice and vegetables"
}
```

Missing numeric fields default to `0`, and missing menu fields default to `"-"`. The firmware calculates `totalCount` locally by adding the three counts. The JSON document capacity is 1024 bytes, so unusually large responses may fail to parse.

### Post sensor readings

Request:

```http
POST /api/sensors
Content-Type: application/json
```

Body:

```json
{
  "temp": 29.5,
  "humidity": 68.0,
  "gas": 1240
}
```

The firmware sends this request approximately every 60 seconds while Wi-Fi is connected. `gas` is a raw ADC reading, not a concentration such as ppm.

### Post cooking status

Request:

```http
POST /api/kitchen
Content-Type: application/json
```

Body:

```json
{
  "status": "cooking_done"
}
```

The response body is not processed. For POST requests, only the HTTP response code is printed to Serial.

## Project structure

```text
iot_project/
├── .gitignore         # Excludes credentials and generated files
├── README.md          # Main project documentation
├── WIRING.md          # Short wiring reference
└── code/
    ├── code.ino          # Current ESP32 firmware
    ├── secrets.example.h # Safe credential template (commit this)
    └── secrets.h         # Local credentials (ignored; do not commit)
```

## Serial monitoring

Use **115200 baud**. The firmware prints:

- Hardware and network startup progress
- The assigned local IP address after connecting
- Sensor readings every two seconds
- Emergency state changes
- Button events and display page changes
- API success, failure, and HTTP response codes

Example sensor line:

```text
[Core 1] Temp: 29.5C | Hum: 68.0% | Gas: 1240 | Fire: 0 | Tap Dist: 32cm | Bin Dist: 45cm
```

An ultrasonic timeout is represented internally as `999 cm`, which prevents the associated tap or bin action from triggering.

## Troubleshooting

### The ESP32 does not boot or repeatedly resets

- Disconnect servos, pumps, fans, and relays, then test the ESP32 by itself.
- Use an external supply with adequate current for actuators.
- Confirm that external power and ESP32 grounds are connected.
- Check whether the GPIO 12 relay input is interfering with the ESP32 boot-strapping level.
- Look for brownout messages in Serial Monitor.

### The display is blank or corrupted

- Confirm the display driver is ST7789 and the module is wired for SPI.
- Verify CS, DC, RESET, MOSI, and SCK against the wiring table.
- Confirm that the module accepts 3.3 V on VCC; check the module documentation before applying another voltage.
- Check the backlight connection.
- Confirm that both Adafruit GFX and the ST7789 library are installed.

### Buttons change state randomly

- Install the required external 10 kΩ pull-down resistors on GPIO 34 and GPIO 35.
- Keep button wires short and ensure there is a common ground.

### Ultrasonic distance always reads 999 cm

- Check TRIG and ECHO orientation and power.
- Ensure the sensor and ESP32 share ground.
- Use safe level shifting on a 5 V ECHO output.
- Confirm that the object is inside the sensor's usable range and is easy to reflect ultrasound from.

### Temperature or humidity does not update

- Verify the DHT type is DHT11 and the data wire is connected to GPIO 33.
- Add the pull-up resistor required by a bare DHT11 sensor, if the module does not already include one.
- DHT11 devices update slowly. The main loop polls faster than the sensor's normal rate, so failed reads can occur; the firmware retains the last valid shared value.

### Wi-Fi does not connect

- Confirm the SSID and password.
- Use a 2.4 GHz network.
- Move the ESP32 closer to the access point.
- Check Serial Monitor for connection status.
- The first attempt times out after about 10 seconds, but reconnection attempts continue in the background.

### API data does not appear

- Confirm Wi-Fi shows as connected.
- Test each endpoint independently and verify its JSON field names match this README.
- Check the HTTP response code in Serial Monitor.
- Keep the meal response below the parser's 1024-byte document capacity.
- Confirm the server accepts the request body and `Content-Type: application/json`.

### Emergency mode never activates for gas

On a standard ESP32 Arduino configuration, `analogRead()` normally produces values from `0` to `4095`, while the current `GAS_THRESHOLD` is `15000`. Therefore, the gas condition cannot normally become true with the current settings. This README documents the behavior exactly as the code is written; calibrate and correct the threshold in the firmware before relying on gas detection.

## Known limitations and security notes

- Wi-Fi credentials and backend URLs are stored in the ignored `code/secrets.h` file. Never force-add that file, and rotate any credentials that were exposed in an earlier copy or commit.
- HTTPS uses `client.setInsecure()`, which encrypts traffic but does not verify the server certificate. This leaves requests vulnerable to server impersonation. Production firmware should validate a CA certificate or certificate/public-key pin.
- The configured gas threshold of `15000` is above the usual 12-bit ESP32 ADC maximum of `4095`, so gas emergencies normally cannot trigger until that setting is corrected.
- The MQ-2 value is uncalibrated raw ADC data and is not a ppm measurement.
- JSON deserialization errors are not checked. A malformed or oversized meal response can silently leave values at defaults.
- HTTP success ranges are not validated beyond checking that a GET returned a positive code. POST response bodies are ignored.
- NTP uses a fixed UTC+6 offset and no daylight-saving rule, which is appropriate for Bangladesh but not for deployment in other time zones.
- The DHT11 is polled far more frequently than its typical update rate; failed reads are ignored and the last valid values are retained.
- UI and network work share one FreeRTOS task. Slow HTTPS requests can temporarily delay display and button handling.
- Servo angles, sensor polarities, relay polarities, and thresholds require calibration for the actual hardware.
- The firmware has no remote authentication logic of its own; backend access control depends entirely on the server.
- Local automation continues offline, but meal updates, time synchronization, sensor uploads, and cooking-status messages require Wi-Fi and a reachable backend.

## Additional documentation

See [`WIRING.md`](WIRING.md) for a compact wiring-only reference. The current source of truth for implemented behavior is [`code/code.ino`](code/code.ino).

## License

No license file is currently included. Unless a license is added, normal copyright restrictions apply and reuse rights are not automatically granted.
