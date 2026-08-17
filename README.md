# ESP32 Smart Kitchen IoT System

An ESP32-based smart kitchen prototype that combines environmental monitoring, safety automation, touchless appliances, a TFT user interface, and authenticated Firebase cloud synchronization.

The firmware reads temperature, humidity, gas, flame, motion, and distance sensors. It controls externally driven DC motor and fan outputs, lighting, a buzzer, and two servo motors. Meal information and device status are exchanged with Firebase Realtime Database after the ESP32 signs in with Firebase Authentication.

> [!IMPORTANT]
> This is a prototype, not a certified fire, gas, or life-safety system. Do not use it as the only protection against fire, gas leaks, or other hazards.

> [!CAUTION]
> Never commit `code/secrets.h`. Before publishing the repository, also verify that `code/secrets.example.h` contains placeholders only. Wi-Fi passwords, Firebase user passwords, and other real credentials must not appear in tracked files or Git history.

## Table of contents

- [Features](#features)
- [System architecture](#system-architecture)
- [Hardware requirements](#hardware-requirements)
- [Complete wiring](#complete-wiring)
- [Power and electrical safety](#power-and-electrical-safety)
- [Software requirements](#software-requirements)
- [Firebase setup](#firebase-setup)
- [Installation and upload](#installation-and-upload)
- [Configuration](#configuration)
- [Display and controls](#display-and-controls)
- [Automation rules](#automation-rules)
- [Firebase data model](#firebase-data-model)
- [Cloud synchronization behavior](#cloud-synchronization-behavior)
- [Project structure](#project-structure)
- [Serial monitoring](#serial-monitoring)
- [Troubleshooting](#troubleshooting)
- [Known limitations and security notes](#known-limitations-and-security-notes)

## Features

- Authenticates a dedicated ESP32 user with Firebase Email/Password Authentication.
- Reads meal menus and counts from Firebase Realtime Database.
- Uploads temperature, humidity, and raw MQ-2 gas readings every five seconds.
- Opens a temporary local Wi-Fi setup portal after a 15-second connection timeout.
- Uploads emergency state changes to Firebase.
- Sends a `cooking_done` status when the cooking button is pressed.
- Refreshes Firebase authentication by signing in again approximately every 50 minutes.
- Shows meal information or live sensor values using a high-contrast ST7789 interface.
- Wraps each meal menu within its own display column and truncates excess text with an ellipsis.
- Fetches time using NTP and displays Bangladesh time (UTC+6).
- Detects flame and high gas readings as emergency conditions.
- Activates a buzzer, emergency servo, warning screen, and other safety outputs.
- Controls a touchless tap and touchless dustbin with ultrasonic sensors.
- Controls a light using PIR motion detection.
- Continues local sensor and actuator behavior when cloud access is unavailable.

## System architecture

The firmware divides its work between the ESP32's two processor cores.

| Execution context         | Main responsibilities                                                                                                                         |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Core 1 (`loop`)           | Reads sensors, evaluates automation and emergency conditions, controls actuators, logs readings, and sends warning state changes to Firebase. |
| Core 0 (`TaskUI_Network`) | Manages Wi-Fi, Firebase login, token renewal, NTP time, the TFT interface, buttons, meal downloads, and periodic sensor uploads.              |

Temperature, humidity, gas, and emergency state are shared between the two tasks through variables protected by a FreeRTOS mutex.

### Cloud flow

1. The ESP32 connects to saved Wi-Fi, waiting up to 15 seconds during startup.
2. If connection fails, it creates the temporary `SmartKitchen-Setup` access point and serves a credential form at `http://192.168.4.1`.
3. Submitted credentials are saved in ESP32 nonvolatile storage. After Wi-Fi connects, the access point, DNS server, and web dashboard are stopped.
4. It sends the configured Firebase email and password to Google Identity Toolkit.
5. A successful login returns a Firebase ID token.
6. The token is added to Realtime Database REST requests as `?auth=<ID_TOKEN>`.
7. The firmware signs in again when the token is missing or about 50 minutes old.
8. Meals are read from `/meal`; sensor, kitchen, and warning states are written to their corresponding database paths.

Local automation does not require Firebase. Cloud reads and writes require both Wi-Fi and a valid Firebase ID token.

## Hardware requirements

### Controller and interface

- ESP32 development board
- 2.8-inch ST7789 SPI TFT display, 320 x 240 pixels
- 1 momentary push button for the Cooking Done action

### Sensors

- DHT11 temperature and humidity sensor
- MQ-2 gas/smoke sensor module
- Flame sensor module
- PIR motion sensor
- 2 ultrasonic distance sensors: one for the tap and one for the dustbin

### Actuators

- 2 logic-level MOSFET/transistor driver channels: tap motor/pump and exhaust fan
- LED or suitable lighting module
- Active buzzer
- 2 servo motors: dustbin mechanism and emergency mechanism
- Water pump or valve, fan, and suitable mechanical components
- External regulated power supply suitable for the actuators

## Complete wiring

The following connections match the pin definitions in [`code/code.ino`](code/code.ino).

### ST7789 SPI display

| Display pin      | ESP32 connection | Purpose                |
| ---------------- | ---------------- | ---------------------- |
| VCC              | 3.3 V            | Display logic power    |
| GND              | GND              | Ground                 |
| CS               | GPIO 5           | SPI chip select        |
| RESET / RES      | GPIO 15          | Display reset          |
| DC / RS          | GPIO 2           | Data/command selection |
| SDA / SDI / MOSI | GPIO 23          | SPI data from ESP32    |
| SCL / SCK        | GPIO 18          | SPI clock              |
| BLK / LED        | 3.3 V            | Backlight always on    |

The firmware initializes the display as 240 x 320 and rotates it into landscape orientation. MISO and touch-controller pins are not used.

### Push buttons

| Function          | ESP32 pin | Required connection                         |
| ----------------- | --------- | ------------------------------------------- |
| Cooking completed | GPIO 13   | Button between GPIO 13 and GND (active LOW) |

The firmware enables GPIO 13's internal pull-up resistor, so no external button resistor is required.

### Sensors

| Sensor or function          | ESP32 pin    | Direction     |
| --------------------------- | ------------ | ------------- |
| PIR motion output           | GPIO 36 (VP) | Input         |
| Flame sensor digital output | GPIO 39 (VN) | Input         |
| MQ-2 analog output          | GPIO 32      | Analog input  |
| DHT11 data                  | GPIO 33      | Digital input |
| Tap ultrasonic TRIG         | GPIO 25      | Output        |
| Tap ultrasonic ECHO         | GPIO 26      | Input         |
| Dustbin ultrasonic TRIG     | GPIO 27      | Output        |
| Dustbin ultrasonic ECHO     | GPIO 14      | Input         |

### Actuators

| Component or function        | ESP32 pin | Direction  |
| ---------------------------- | --------- | ---------- |
| Tap motor/pump driver signal | GPIO 12   | Output     |
| Exhaust-fan driver signal    | GPIO 4    | Output     |
| Motion-controlled light      | GPIO 16   | Output     |
| Active buzzer                | GPIO 17   | Output     |
| Dustbin servo signal         | GPIO 21   | PWM output |
| Emergency servo signal       | GPIO 22   | PWM output |

## Power and electrical safety

1. Do not power motors, servos, pumps, or fans directly from an ESP32 pin or its 3.3 V rail. Use an external regulated supply and a correctly rated logic-level MOSFET/transistor driver for each DC motor or fan.
2. Connect the external supply ground to ESP32 GND so all signal voltages share the same reference.
3. ESP32 GPIO pins are not 5 V tolerant. If an ultrasonic ECHO pin produces 5 V, use a voltage divider or logic-level converter.
4. Confirm that all sensor outputs and motor-driver inputs are compatible with 3.3 V logic.
5. Use a current-limiting resistor and suitable driver if GPIO 16 controls more than a small LED.
6. Fit a flyback diode across each DC motor/fan, and use correctly rated drivers, fuses, and wiring for inductive or high-current loads.
7. Keep mains voltage away from breadboards and exposed low-voltage wiring. Mains wiring should only be performed by someone qualified to do it safely.
8. GPIO 12 is an ESP32 boot-strapping pin. Ensure its motor driver does not force an invalid startup level and prevent booting.
9. Test every emergency response with safe simulated inputs before connecting pumps, valves, or other real loads.

## Software requirements

### Development tools

- Arduino IDE 2.x or Arduino CLI
- Espressif ESP32 Arduino board package
- Data-capable USB cable
- Serial driver appropriate for the ESP32 development board

### Arduino libraries

Install the following libraries using Arduino IDE Library Manager.

| Library                            | Header used         | Purpose                                                 |
| ---------------------------------- | ------------------- | ------------------------------------------------------- |
| ArduinoJson 6                      | `ArduinoJson.h`     | Parses Firebase Authentication and meal JSON responses. |
| Adafruit GFX Library               | `Adafruit_GFX.h`    | Graphics primitives and text rendering.                 |
| Adafruit ST7735 and ST7789 Library | `Adafruit_ST7789.h` | ST7789 display driver.                                  |
| DHT sensor library by Adafruit     | `DHT.h`             | Reads the DHT11 sensor.                                 |
| Adafruit Unified Sensor            | Indirect dependency | Common dependency of the DHT library.                   |
| ESP32Servo                         | `ESP32Servo.h`      | Controls both servo motors.                             |

`WiFi`, `WebServer`, `DNSServer`, `Preferences`, `WiFiClientSecure`, `HTTPClient`, `SPI`, `time`, and FreeRTOS support are provided by the ESP32 Arduino core.

## Firebase setup

The firmware requires a Firebase project with Email/Password Authentication and Realtime Database.

### 1. Enable Email/Password Authentication

In the Firebase project:

1. Open Authentication.
2. Enable the Email/Password sign-in provider.
3. Create a dedicated user account for the ESP32.
4. Do not reuse a personal account password.

The ESP32 uses the Firebase Identity Toolkit `accounts:signInWithPassword` endpoint. A successful login supplies the ID token used by all database requests.

### 2. Create Realtime Database

Create a Firebase Realtime Database and note its complete database URL. Depending on the database region, it may resemble one of these forms:

```text
https://PROJECT_ID-default-rtdb.firebaseio.com
https://PROJECT_ID.REGION.firebasedatabase.app
```

Do not add a trailing slash to `firebaseHost`; the firmware adds path separators itself.

### 3. Configure database rules

Database rules must allow the authenticated ESP32 user to access `/meal`, `/sensors`, `/kitchen`, and `/warning`. Use the least privilege appropriate to the project.

The following rules are suitable only as a simple authenticated prototype starting point because any authenticated project user receives access to all four paths:

```json
{
	"rules": {
		"meal": {
			".read": "auth != null",
			".write": "auth != null"
		},
		"sensors": {
			".read": "auth != null",
			".write": "auth != null"
		},
		"kitchen": {
			".read": "auth != null",
			".write": "auth != null"
		},
		"warning": {
			".read": "auth != null",
			".write": "auth != null"
		}
	}
}
```

For a real deployment, restrict access to the dedicated device user and validate the allowed fields and value types.

### 4. Collect configuration values

You need:

- The Web API key from Firebase project settings
- The complete Realtime Database URL
- The dedicated Firebase user's email address
- The dedicated Firebase user's password
- The local 2.4 GHz Wi-Fi SSID and password

Keep real values in `code/secrets.h`, which is excluded by [`.gitignore`](.gitignore).

## Installation and upload

1. Clone or download this repository.
2. Copy [`code/secrets.example.h`](code/secrets.example.h) to `code/secrets.h` if the private file does not already exist.
3. Replace every example value in `code/secrets.h` with the correct Wi-Fi and Firebase configuration.
4. Open [`code/code.ino`](code/code.ino) in Arduino IDE.
5. Install the ESP32 board package and the required libraries.
6. Select the board matching the hardware. `ESP32 Dev Module` is appropriate for many generic boards.
7. Select the correct serial port.
8. Complete and verify the low-voltage wiring before applying actuator power.
9. Compile the sketch using Verify.
10. Upload the firmware.
11. Open Serial Monitor at 115200 baud and reset the board.

If uploading does not begin automatically, hold the board's BOOT button while upload starts, then release it when writing begins. The exact procedure depends on the board.

## Configuration

### Private settings

The firmware expects these constants in `code/secrets.h`:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* firebaseHost = "https://YOUR_PROJECT-default-rtdb.firebaseio.com";
const char* firebaseApiKey = "YOUR_FIREBASE_WEB_API_KEY";
const char* firebaseUserEmail = "YOUR_DEVICE_USER_EMAIL";
const char* firebaseUserPassword = "YOUR_DEVICE_USER_PASSWORD";
```

| Constant               | Description                                                  |
| ---------------------- | ------------------------------------------------------------ |
| `ssid`                 | 2.4 GHz Wi-Fi network name                                   |
| `password`             | Wi-Fi password                                               |
| `firebaseHost`         | Complete Realtime Database base URL without a trailing slash |
| `firebaseApiKey`       | Firebase project's Web API key used for Email/Password login |
| `firebaseUserEmail`    | Email of the dedicated Firebase Authentication user          |
| `firebaseUserPassword` | Password of the dedicated Firebase Authentication user       |

The Firebase Web API key identifies the Firebase project but does not replace database rules or user authentication. The Wi-Fi password and Firebase user password are credentials and must remain private.

### Thresholds and timings

These values are defined near the top of [`code/code.ino`](code/code.ino).

| Setting                  | Current value | Effect                                                                    |
| ------------------------ | ------------: | ------------------------------------------------------------------------- |
| `GAS_THRESHOLD`          |        `1600` | A higher MQ-2 reading is treated as a gas leak. Calibrate for the installed sensor. |
| `TEMP_THRESHOLD`         |      `32.0 C` | Turns on the exhaust fan at or above this temperature.                    |
| `DISTANCE_THRESHOLD`     |       `15 cm` | Activates the tap or opens the dustbin below this distance.               |
| `LED_DELAY`              |    `60000 ms` | Turns the light off after one minute without detected motion.             |
| Firebase synchronization |     `5000 ms` | Uploads sensor data and downloads meals every five seconds.               |
| Authentication interval  |  `3000000 ms` | Signs in again after approximately 50 minutes.                            |
| Automatic page interval |    `10000 ms` | Switches between meal and sensor pages every 10 seconds.                  |
| Cooking-button debounce  |     `1000 ms` | Prevents repeated cooking events from one press.                          |

MQ-2 readings depend on the module, supply voltage, warm-up period, environment, and ADC settings. Calibrate with the actual circuit. A raw ADC reading is not a calibrated gas concentration.

## Display and controls

### Startup

The TFT displays `Connecting to Wi-Fi...` while the initial connection attempt runs for up to 15 seconds. If it fails, connect a phone or computer to `SmartKitchen-Setup` and open `http://192.168.4.1`. Enter the 2.4 GHz Wi-Fi credentials. The firmware stores them in NVS, attempts the connection, and completely stops the setup access point and web server after it connects. If it remains disconnected for another 15 seconds, the setup portal returns. Local automation continues throughout.

### Top status bar

- `WIFI OK` means Wi-Fi is connected and a Firebase ID token is available.
- `NO AUTH` means either Wi-Fi is disconnected or Firebase is not authenticated.
- The date and time appear after successful NTP synchronization.

### Meal page

The default page shows:

- Date and time
- Authentication status
- Breakfast, lunch, and dinner counts
- Menu text for each meal
- Calculated total meal count

### Sensor page

The sensor page shows:

- Date and time
- Authentication status
- Temperature in degrees Celsius
- Relative humidity percentage
- Raw MQ-2 ADC reading

### Warning page

During a flame or gas emergency, a red `WARNING! FIRE/GAS DETECTED` screen overrides both normal pages. The previous normal page returns when the emergency clears.

### Buttons

- The display automatically switches between the meal and sensor pages every 10 seconds.
- Cooking Done writes `{"status":"cooking_done"}` to `/kitchen` when Wi-Fi and Firebase authentication are available.

## Automation rules

| Condition                            | Automatic response                                                        |
| ------------------------------------ | ------------------------------------------------------------------------- |
| Tap distance is below 15 cm          | Tap motor/pump driver turns on.                                           |
| Flame is detected                    | Tap motor/pump driver turns on.                                           |
| Dustbin distance is below 15 cm      | Dustbin servo moves to 90 degrees; otherwise it returns to 0 degrees.     |
| PIR motion is detected               | Light turns on and the one-minute timer restarts.                         |
| No motion is detected for one minute | Light turns off.                                                          |
| Temperature is at least 32 C         | Exhaust-fan driver turns on.                                              |
| Gas reading is above the threshold   | Fan, buzzer, warning screen, and emergency servo activate.                |
| Flame is detected                    | Buzzer, warning screen, and emergency servo activate.                     |
| Emergency begins                     | The firmware attempts to write an active warning and message to Firebase. |
| Emergency clears                     | The firmware attempts to write an inactive `Normal` warning to Firebase.  |
| No emergency exists                  | Buzzer turns off and the emergency servo returns to 0 degrees.            |

Motor and fan driver outputs are active HIGH in the firmware. Use a 3.3 V-compatible driver whose input does not overload the GPIO. The GPIO is a control signal only; it must never carry load current.

The flame input is interpreted as active HIGH. Many flame modules provide active-LOW digital output, so verify the specific module before testing emergency behavior.

## Firebase data model

The firmware uses four root-level Realtime Database nodes.

```json
{
	"meal": {
		"bfast_count": 12,
		"lunch_count": 20,
		"dinner_count": 18,
		"bfast_menu": "Egg and bread",
		"lunch_menu": "Rice and chicken",
		"dinner_menu": "Rice and vegetables"
	},
	"sensors": {
		"temp": 29.5,
		"humidity": 68.0,
		"gas": 1240
	},
	"kitchen": {
		"status": "cooking_done"
	},
	"warning": {
		"active": false,
		"message": "Normal"
	}
}
```

### `/meal`

The ESP32 reads this object using `GET`.

| Field          | Type   | Firmware behavior            |
| -------------- | ------ | ---------------------------- |
| `bfast_count`  | Number | Defaults to `0` when absent. |
| `lunch_count`  | Number | Defaults to `0` when absent. |
| `dinner_count` | Number | Defaults to `0` when absent. |
| `bfast_menu`   | String | Defaults to `-` when absent. |
| `lunch_menu`   | String | Defaults to `-` when absent. |
| `dinner_menu`  | String | Defaults to `-` when absent. |

The total is calculated locally as breakfast + lunch + dinner. The JSON document capacity for this response is 1024 bytes.

### `/sensors`

The ESP32 replaces this object using `PUT` approximately every five seconds:

```json
{
	"temp": 29.5,
	"humidity": 68.0,
	"gas": 1240
}
```

`gas` is a raw ADC reading, not a concentration such as ppm. Because `PUT` replaces the node, other fields previously stored under `/sensors` are removed.

### `/kitchen`

The ESP32 replaces this object using `PUT` when the Cooking Done button is pressed:

```json
{
	"status": "cooking_done"
}
```

### `/warning`

When emergency state changes, the ESP32 replaces this object using `PUT`.

Fire example:

```json
{
	"active": true,
	"message": "Fire Detected"
}
```

Gas example:

```json
{
	"active": true,
	"message": "Gas Leak Detected"
}
```

Cleared example:

```json
{
	"active": false,
	"message": "Normal"
}
```

If both fire and gas are active at the transition, the message is `Fire Detected` because the firmware checks the flame state first when choosing the text.

## Cloud synchronization behavior

| Function              | Method and path                    | Trigger                                               |
| --------------------- | ---------------------------------- | ----------------------------------------------------- |
| Firebase login        | `POST accounts:signInWithPassword` | Startup, missing token, or token age above 50 minutes |
| Fetch meals           | `GET /meal.json?auth=<token>`      | After successful startup login and every five seconds |
| Upload sensors        | `PUT /sensors.json?auth=<token>`   | Every five seconds                                    |
| Upload cooking status | `PUT /kitchen.json?auth=<token>`   | Cooking Done button press                             |
| Upload warning state  | `PUT /warning.json?auth=<token>`   | Emergency state transition                            |

Periodic synchronization only checks that Wi-Fi is connected before calling the Firebase functions. Each Firebase function also checks that the ID token is non-empty.

## Project structure

```text
iot_project/
|-- .gitignore             # Excludes credentials and generated files
|-- README.md              # Main project documentation
|-- WIRING.md              # Compact wiring reference
`-- code/
    |-- code.ino           # ESP32 firmware
    |-- secrets.example.h  # Public configuration template
    `-- secrets.h          # Private local credentials; ignored by Git
```

## Serial monitoring

Open Serial Monitor at 115200 baud. The current firmware prints:

- Project startup and hardware initialization messages
- Firebase Authentication success or HTTP failure code
- Temperature, humidity, raw gas value, and flame state every two seconds
- Emergency triggered and emergency cleared events

Example sensor output:

```text
[Core 1] Temp: 29.5C | Hum: 68.0% | Gas: 1240 | Fire: 0
```

The current firmware does not print ultrasonic distances, Firebase database response codes, button presses, or the assigned local IP address.

An ultrasonic timeout is represented internally as `999 cm`, preventing the associated tap or dustbin action from activating.

## Troubleshooting

### The firmware does not compile because `secrets.h` is missing

Copy `code/secrets.example.h` to `code/secrets.h`. Confirm that the new file is in the same directory as `code.ino` and defines all six required constants.

### Firebase authentication fails

- Confirm Email/Password sign-in is enabled in Firebase Authentication.
- Confirm `firebaseApiKey`, `firebaseUserEmail`, and `firebaseUserPassword` are correct.
- Confirm the dedicated Firebase user exists and is enabled.
- Check the authentication HTTP code in Serial Monitor.
- Confirm the ESP32 has internet access and can resolve Google domains.

### Authentication succeeds but database data does not update

- Confirm `firebaseHost` is the exact Realtime Database URL without a trailing slash.
- Confirm database rules permit the authenticated user to read and write the required paths.
- Confirm `/meal` uses the exact field names documented above.
- Keep the `/meal` response small enough for the 1024-byte JSON document.
- The current firmware does not print database REST response codes, so inspect Firebase or add temporary diagnostics when debugging.

### The display shows `NO AUTH`

This label combines two conditions: Wi-Fi disconnection and missing Firebase authentication. Check the Wi-Fi credentials first, then check Serial Monitor for Firebase login failure codes.

### The ESP32 does not boot or repeatedly resets

- Disconnect servos, pumps, fans, and their drivers, then test the ESP32 alone.
- Use an external actuator supply with adequate current.
- Connect the actuator supply ground to ESP32 GND.
- Check whether the GPIO 12 motor-driver input is interfering with the ESP32 boot-strapping level.
- Look for brownout messages in Serial Monitor.

### The display is blank or corrupted

- Confirm the display controller is ST7789 and the module uses SPI.
- Verify CS, DC, RESET, MOSI, and SCK against the wiring table.
- Confirm the display power voltage using its documentation.
- Check the backlight connection.
- Confirm the Adafruit GFX and ST7789 libraries are installed.

### Cooking button does not respond reliably

- Confirm the button connects GPIO 13 to GND when pressed.
- Keep button wires short and ensure there is a common ground.

### Ultrasonic distance always times out

- Check TRIG and ECHO wiring.
- Ensure the sensor and ESP32 share ground.
- Safely level-shift a 5 V ECHO signal.
- Confirm that the target is inside the sensor's useful range.

### Temperature or humidity does not update

- Verify that the device is a DHT11 connected to GPIO 33.
- Add the required data pull-up resistor if a bare DHT11 does not include one.
- DHT11 sensors update slowly. The firmware polls faster than their usual update rate, ignores failed reads, and retains the last valid shared value.

### Emergency mode never activates for gas

The configured `GAS_THRESHOLD` of 1600 is a raw ADC threshold, not a calibrated gas concentration. MQ-2 output depends on supply voltage, warm-up time, ADC configuration, and the individual sensor. Calibrate it using the completed circuit before relying on gas detection.

## Known limitations and security notes

- `code/secrets.h` contains private Wi-Fi and Firebase credentials and must never be committed.
- Verify that the tracked `code/secrets.example.h` contains placeholders only before every public push.
- If credentials were committed previously, removing them in a later commit is insufficient. Rotate them and remove them from Git history.
- HTTPS requests use `client.setInsecure()`. Traffic is encrypted, but server certificates are not verified, allowing server impersonation attacks.
- Firebase login sends the configured user's email and password from the device. A dedicated least-privilege account is essential.
- The ID token is stored in RAM as a `String` and appended to database request URLs.
- The firmware signs in again every 50 minutes instead of using the returned Firebase refresh token.
- The configured gas threshold of 1600 is installation-specific and requires calibration.
- MQ-2 data is an uncalibrated raw ADC value, not a ppm measurement.
- JSON deserialization errors are not checked.
- Database write response codes are ignored, so failed writes are not reported or retried.
- A warning is uploaded only when local emergency state changes. If Wi-Fi or authentication is unavailable at that moment, that state is not queued for later upload.
- Firebase operations can occur from both ESP32 cores, and shared network/authentication objects are not protected by a separate mutex.
- The DHT11 is polled more frequently than its normal update rate.
- NTP uses fixed UTC+6 without a daylight-saving rule.
- UI and network work share one task, so slow HTTPS calls can temporarily delay display and button handling.
- Driver polarity, flame-sensor polarity, servo angles, and all sensor thresholds require validation using the actual hardware.
- Local automation continues offline, but Firebase features and initial NTP synchronization require internet access.

## Additional documentation

See [`WIRING.md`](WIRING.md) for a compact wiring reference. The implemented behavior in [`code/code.ino`](code/code.ino) is the source of truth.

## License

No license file is currently included. Unless a license is added, normal copyright restrictions apply and reuse rights are not automatically granted.
