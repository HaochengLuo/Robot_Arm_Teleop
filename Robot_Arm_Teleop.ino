#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#if !defined(ESP32)
#warning "This sketch is configured for ESP32 boards."
#endif

// ESP32 default I2C pins. Change these if your wiring is different.
const uint8_t I2C_SDA_PIN = 21;
const uint8_t I2C_SCL_PIN = 22;

// PCA9685 servo driver settings.
const uint8_t SERVO_FREQ_HZ = 50;
const uint16_t SERVO_MIN_US = 650;
const uint16_t SERVO_MAX_US = 2350;

// ESP32 ADC settings. Use ADC1 pins for pots, especially if you later use WiFi.
const uint8_t ADC_BITS = 12;
const int ADC_MAX_VALUE = 4095;

// Pot smoothing: smaller = smoother/slower, larger = faster/more jitter.
const float EMA_ALPHA = 0.16f;

// Startup timing. Move the controller to a safe pose before startup finishes.
const uint16_t CONTROLLER_SETTLE_MS = 3000;
const uint16_t STARTUP_SERVO_GAP_MS = 350;
const uint8_t LOOP_DELAY_MS = 15;

// Ignore very tiny angle changes to reduce buzzing.
const float MIN_ANGLE_STEP = 0.4f;

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

struct JointConfig {
  const char *name;
  uint8_t potPin;
  uint8_t pwmChannel;

  // Calibration values for ESP32 12-bit analogRead.
  // These preserve your original reversed UNO mapping, scaled from 10-bit:
  // 800 -> about 3200, 240 -> about 960.
  // If your pots use the full ESP32 range, try 3900 and 200 instead.
  int potAtMinAngle;
  int potAtMaxAngle;

  // Mechanical software limits. Tune these for your arm before using full range.
  float minAngle;
  float maxAngle;
};

JointConfig joints[] = {
  // name       pot GPIO  PCA9685 ch  potMin  potMax  minDeg  maxDeg
  {"Base",       32,      15,          3200,   960,    10.0f,  170.0f},
  {"Shoulder",   33,      14,          3200,   960,    25.0f,  145.0f},
  {"Elbow",      34,      13,          3200,   960,    15.0f,  165.0f},
  {"Wrist",      35,      12,          3200,   960,    20.0f,  160.0f},
};

const uint8_t JOINT_COUNT = sizeof(joints) / sizeof(joints[0]);

// Gripper: button uses ESP32 GPIO13, gripper servo uses PCA9685 channel 11.
const uint8_t BUTTON_PIN = 13;
const uint8_t HAND_CHANNEL = 11;
const float GRIPPER_MIN_ANGLE = 60.0f;
const float GRIPPER_MAX_ANGLE = 150.0f;
const float GRIPPER_OPEN_ANGLE = 85.0f;
const float GRIPPER_CLOSED_ANGLE = 135.0f;

float filteredPot[JOINT_COUNT];
float lastAngle[JOINT_COUNT];
bool gripperClosed = false;

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

bool changedEnough(float current, float previous) {
  return (current - previous >= MIN_ANGLE_STEP) || (previous - current >= MIN_ANGLE_STEP);
}

uint16_t pulseUsToTicks(uint16_t pulseUs) {
  const float usPerTick = 1000000.0f / SERVO_FREQ_HZ / 4096.0f;
  return (uint16_t)(pulseUs / usPerTick + 0.5f);
}

uint16_t angleToPulseUs(float angle) {
  angle = clampFloat(angle, 0.0f, 180.0f);
  return (uint16_t)(SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / 180.0f + 0.5f);
}

void setServoAngle(uint8_t pwmChannel, float angle) {
  uint16_t pulseUs = angleToPulseUs(angle);
  pwm.setPWM(pwmChannel, 0, pulseUsToTicks(pulseUs));
}

int readPotAverage(uint8_t pin, uint8_t samples) {
  long total = 0;

  for (uint8_t i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(2);
  }

  return (int)(total / samples);
}

float potToAngle(float potValue, const JointConfig &joint) {
  float potSpan = (float)(joint.potAtMaxAngle - joint.potAtMinAngle);

  if (potSpan == 0.0f) {
    return joint.minAngle;
  }

  float normalized = (potValue - joint.potAtMinAngle) / potSpan;
  normalized = clampFloat(normalized, 0.0f, 1.0f);

  return joint.minAngle + normalized * (joint.maxAngle - joint.minAngle);
}

void setupAdcPins() {
  analogReadResolution(ADC_BITS);

  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    pinMode(joints[i].potPin, INPUT);
    analogSetPinAttenuation(joints[i].potPin, ADC_11db);
  }
}

void seedFiltersFromPots() {
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    filteredPot[i] = (float)readPotAverage(joints[i].potPin, 10);
    lastAngle[i] = potToAngle(filteredPot[i], joints[i]);
  }
}

void setGripper(bool closeGripper) {
  float angle = closeGripper ? GRIPPER_CLOSED_ANGLE : GRIPPER_OPEN_ANGLE;
  angle = clampFloat(angle, GRIPPER_MIN_ANGLE, GRIPPER_MAX_ANGLE);

  setServoAngle(HAND_CHANNEL, angle);
  gripperClosed = closeGripper;

  Serial.println(closeGripper ? "Grab" : "Release");
}

void staggeredStartup() {
  delay(CONTROLLER_SETTLE_MS);
  seedFiltersFromPots();

  setGripper(false);
  delay(STARTUP_SERVO_GAP_MS);

  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    setServoAngle(joints[i].pwmChannel, lastAngle[i]);

    Serial.print("Startup ");
    Serial.print(joints[i].name);
    Serial.print(": ");
    Serial.println(lastAngle[i]);

    delay(STARTUP_SERVO_GAP_MS);
  }
}

void updateJoint(uint8_t index) {
  JointConfig &joint = joints[index];
  int rawPot = analogRead(joint.potPin);

  rawPot = constrain(rawPot, 0, ADC_MAX_VALUE);
  filteredPot[index] += EMA_ALPHA * ((float)rawPot - filteredPot[index]);
  float angle = potToAngle(filteredPot[index], joint);

  if (changedEnough(angle, lastAngle[index])) {
    setServoAngle(joint.pwmChannel, angle);
    lastAngle[index] = angle;
  }
}

void updateGripperFromButton() {
  bool buttonPressed = (digitalRead(BUTTON_PIN) == LOW);

  if (buttonPressed != gripperClosed) {
    setGripper(buttonPressed);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setupAdcPins();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ_HZ);
  delay(10);

  staggeredStartup();
}

void loop() {
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    updateJoint(i);
  }

  updateGripperFromButton();
  delay(LOOP_DELAY_MS);
}
