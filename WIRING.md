# ESP32 Smart Kitchen Wiring

This reference matches the pin definitions in `code/code.ino`.

## ST7789 SPI display

| Display pin | ESP32 connection | Note |
| --- | --- | --- |
| VCC | 3.3 V | Do not connect directly to 5 V unless the module explicitly supports it |
| GND | GND | Common ground |
| CS | GPIO 5 | Chip select |
| RESET / RES | GPIO 15 | Reset |
| DC / RS | GPIO 2 | Data/command |
| SDA / SDI / MOSI | GPIO 23 | SPI data |
| SCL / SCK | GPIO 18 | SPI clock |
| BLK / LED | 3.3 V | Backlight always on |

## Cooking Done button

| Button connection | ESP32 connection | Note |
| --- | --- | --- |
| One side | GPIO 13 | Configured with the internal pull-up |
| Other side | GND | Pressing the button produces an active-LOW input |

No external pull-down resistor is required.

## Sensors

| Sensor | ESP32 connection | Note |
| --- | --- | --- |
| PIR motion output | GPIO 36 (VP) | Digital input |
| Flame sensor output | GPIO 39 (VN) | Digital input; firmware currently treats HIGH as flame detected |
| MQ-2 analog output | GPIO 32 | Analog input; must not exceed 3.3 V |
| DHT11 data | GPIO 33 | Digital input |
| Tap ultrasonic TRIG | GPIO 25 | Digital output |
| Tap ultrasonic ECHO | GPIO 26 | Digital input; level-shift if the sensor outputs 5 V |
| Dustbin ultrasonic TRIG | GPIO 27 | Digital output |
| Dustbin ultrasonic ECHO | GPIO 14 | Digital input; level-shift if the sensor outputs 5 V |

## Actuators

| Component | ESP32 connection | Note |
| --- | --- | --- |
| Tap motor/pump driver input | GPIO 12 | Active-HIGH control signal to a logic-level MOSFET/transistor driver |
| Fan driver input | GPIO 4 | Active-HIGH control signal to a logic-level MOSFET/transistor driver |
| LED light | GPIO 16 | Use a current-limiting resistor for a small LED or a driver for a larger light |
| Active buzzer | GPIO 17 | Use a driver if its current exceeds the GPIO rating |
| Dustbin servo signal | GPIO 21 | Power the servo from an external supply |
| Emergency servo signal | GPIO 22 | Power the servo from an external supply |

## Motor and fan driver wiring

Remove the relay modules, but do not connect either load directly to an ESP32 GPIO. For each DC motor or fan:

```text
External supply +  ---- motor/fan ---- MOSFET drain
ESP32 GPIO ------- gate resistor ---- MOSFET gate
                                  |
                            10 kOhm pull-down
                                  |
MOSFET source ------------------- GND
ESP32 GND ----------------------- GND
External supply GND ------------- GND
```

Place a flyback diode across each inductive load: cathode/striped end to the positive supply and anode to the MOSFET/load negative side. Select the MOSFET, diode, supply, fuse, and wiring for the load's startup and stall current—not only its normal running current.

GPIO 12 is an ESP32 boot-strapping pin. Ensure its driver circuit does not force an invalid level while the ESP32 starts.

## Safety reminders

- Use an external regulated supply with enough current for the motor, fan, and servos.
- Join the external supply ground and ESP32 ground.
- Never apply more than 3.3 V to an ESP32 GPIO.
- Keep mains voltage away from the breadboard and low-voltage wiring.
- Test flame and gas behavior using safe simulated inputs before connecting real loads.
