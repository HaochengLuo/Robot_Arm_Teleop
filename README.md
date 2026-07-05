# Robot Arm Teleop

ESP32 teleoperation code for a compact robot arm controlled by potentiometers through a PCA9685 servo driver.

## Features

- ESP32 support with explicit I2C pins
- Potentiometer control for base, shoulder, elbow, and wrist servos
- EMA filtering to reduce analog input jitter
- Per-joint software angle limits
- Staggered servo startup to reduce current spikes
- Push-button gripper control

## Hardware

- ESP32 development board
- PCA9685 16-channel PWM servo driver
- 5 servos for base, shoulder, elbow, wrist, and gripper
- 4 potentiometers
- 1 push button
- External 5-6V servo power supply

## Default Wiring

| Function | ESP32 Pin / PCA9685 Channel |
| --- | --- |
| I2C SDA | GPIO 21 |
| I2C SCL | GPIO 22 |
| Base potentiometer | GPIO 32 |
| Shoulder potentiometer | GPIO 33 |
| Elbow potentiometer | GPIO 34 |
| Wrist potentiometer | GPIO 35 |
| Gripper button | GPIO 13 |
| Gripper servo | PCA9685 channel 11 |
| Wrist servo | PCA9685 channel 12 |
| Elbow servo | PCA9685 channel 13 |
| Shoulder servo | PCA9685 channel 14 |
| Base servo | PCA9685 channel 15 |

Connect potentiometers to `3.3V`, `GND`, and the matching ADC pin. Do not feed 5V into ESP32 analog pins.

For the PCA9685, connect `VCC` to ESP32 `3.3V`, `SDA` to GPIO 21, `SCL` to GPIO 22, and `GND` to ESP32 ground. Power the servos through the PCA9685 `V+` terminal using a separate 5-6V supply, and make sure the ESP32, PCA9685, and servo power supply share a common ground.

## Arduino Setup

Install these in Arduino IDE:

- ESP32 board support package
- `Adafruit PWM Servo Driver Library`

Open `Robot_Arm_Teleop.ino`, select your ESP32 board, then upload.

## Tuning

Most tuning happens near the top of `Robot_Arm_Teleop.ino`.

`EMA_ALPHA` controls smoothing. Lower values are smoother but slower. Higher values respond faster but may jitter more.

Each joint has this configuration:

```cpp
{"Base", 32, 15, 3200, 960, 10.0f, 170.0f}
```

The fields are:

- Joint name
- ESP32 potentiometer GPIO
- PCA9685 servo channel
- ADC value at minimum angle
- ADC value at maximum angle
- Minimum allowed servo angle
- Maximum allowed servo angle

The default ADC calibration preserves the reversed mapping from the original Arduino Uno sketch. If your potentiometers use most of the ESP32 ADC range, try values closer to `3900` and `200`.

## Safety Notes

Start with conservative angle limits before testing the full mechanical range. Move one joint at a time, keep the arm unloaded during first tests, and use an external servo power supply instead of powering servos from the ESP32.

## License

MIT License. See [LICENSE](LICENSE).
