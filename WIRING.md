### 📱 ST7789 SPI Display

| Display Pin          | ESP32 Pin | Wiring Note          |
| -------------------- | --------- | -------------------- |
| **VCC**              | 3.3V      | Do not connect to 5V |
| **GND**              | GND       |                      |
| **CS**               | 5         | Chip Select          |
| **RESET / RES**      | 15        | Reset                |
| **DC / RS**          | 2         | Data/Command         |
| **SDA / SDI (MOSI)** | 23        | SPI Data             |
| **SCL / SCK**        | 18        | SPI Clock            |
| **BLK / LED**        | 3.3V      | Keeps backlight on   |

---

### 🔘 Push Buttons

| Button           | ESP32 Pin | Wiring Note                                           |
| ---------------- | --------- | ----------------------------------------------------- |
| **Cooking Done** | 13        | **Must** use a physical 10k pull-down resistor to GND |

---

### 📡 Sensors (Inputs)

| Sensor                    | ESP32 Pin | Wiring Note    |
| ------------------------- | --------- | -------------- |
| **PIR Motion Sensor**     | 36 (VP)   | Digital Input  |
| **Flame Sensor**          | 39 (VN)   | Digital Input  |
| **MQ-2 Gas Sensor**       | 32        | Analog Input   |
| **DHT11 Temp/Humid**      | 33        | Digital Input  |
| **Ultrasonic (Tap) TRIG** | 25        | Digital Output |
| **Ultrasonic (Tap) ECHO** | 26        | Digital Input  |
| **Ultrasonic (Bin) TRIG** | 27        | Digital Output |
| **Ultrasonic (Bin) ECHO** | 14        | Digital Input  |

---

### ⚙️ Actuators (Outputs)

| Component                  | ESP32 Pin | Wiring Note                                                                  |
| -------------------------- | --------- | ---------------------------------------------------------------------------- |
| **Tap motor driver input** | 12        | Connect to a logic-level MOSFET/transistor driver, not directly to the motor |
| **Fan driver input**       | 4         | Connect to a logic-level MOSFET/transistor driver, not directly to the fan   |
| **LED Light**              | 16        | Use a resistor if connecting an LED directly                                 |
| **Buzzer**                 | 17        | Active buzzer                                                                |
| **Servo 1 (Dustbin)**      | 21        | **Power servos via external 5V source**, not ESP32                           |
| **Servo 2 (Emergency)**    | 22        | Controls Window & Gas Valve                                                  |

### ⚠️ Crucial Wiring Reminders:

1. **Motor/fan driver:** Remove both relay modules. Each GPIO connects only to the input/gate of a correctly rated logic-level MOSFET or transistor driver. Power the motor and fan from an external supply, add a flyback diode across each inductive load, and connect the external supply GND to ESP32 GND. Never connect a motor or fan directly to an ESP32 pin.
2. **Button Wiring:** Connect one side of the button to 3.3V. Connect the other side to the ESP32 pin (34 or 35) **AND** to a 10k resistor that goes to GND.
