/******************************************************************************
 * MICROMOUSE 2026 — ESP32-C6
 * Single-file Arduino sketch. Change the CONFIG section on the day.
 *
 * KIT: ESP32-C6-DevKitC-1, DFRobot DRI0044, DFRobot SEN0142 (MPU-6050),
 *      3x VL53L0X, 2x GA12-N20, 2S battery + 5V buck
 *
 * BUILD ORDER (from the playbook):
 *   Hour 0-1: Blink LED, spin motors, confirm encoders
 *   Hour 1-2: moveDistance(180), turnAngle(90) — repeat, measure drift
 *   Hour 2-3: Read sensors, set thresholds, verify wall detection
 *   Hour 3-4: Flood fill + slow search to centre
 *   Hour 4-5: Return leg, speed run, escalate
 *   Hour 5-6: Freeze, charge, rehearse
 ******************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <MPU6050.h>

// ============================================================================
// ============================== CONFIG ======================================
// ============== CHANGE THESE ON THE DAY. NOTHING ELSE. ======================
// ============================================================================

// ----------------------------- I2C PINS -------------------------------------
#define I2C_SDA             20
#define I2C_SCL             21

// ----------------------------- XSHUT PINS -----------------------------------
// Each VL53L0X needs its own XSHUT pin. All power up at 0x29.
#define XSHUT_LEFT          22
#define XSHUT_FRONT         23
#define XSHUT_RIGHT         24

// ----------------------------- MOTOR DRIVER (DRI0044) -----------------------
// DRI0044: 2 direction + 2 PWM. VCC = 3.3V logic, VM = motor supply.
#define MOTOR_LEFT_DIR      1
#define MOTOR_LEFT_PWM      2
#define MOTOR_RIGHT_DIR     3
#define MOTOR_RIGHT_PWM     4

// LEDC channels for PWM (ESP32 has 16 channels, use 0 and 1)
#define MOTOR_LEFT_CH       0
#define MOTOR_RIGHT_CH      1
#define PWM_FREQ            20000   // 20 kHz — above human hearing
#define PWM_RESOLUTION      8       // 8-bit: 0-255

// ----------------------------- ENCODERS (GA12-N20) ---------------------------
#define ENC_LEFT_A          10
#define ENC_LEFT_B          11
#define ENC_RIGHT_A         18
#define ENC_RIGHT_B         19

// Encoder polarity: set to -1 if a wheel counts backwards when driven forward
#define ENC_LEFT_POLARITY   (-1)
#define ENC_RIGHT_POLARITY  (1)

// Motor polarity: set to -1 if a wheel spins backwards when told to go forward
#define MOTOR_LEFT_POLARITY  (1)
#define MOTOR_RIGHT_POLARITY (-1)

// ----------------------------- MECHANICAL CALIBRATION ------------------------
// MEASURE THESE ON THE DAY. Do not trust the defaults.
// Procedure: turn wheel by hand 10 revolutions, count pulses, divide by 10.
// Then: MM_PER_COUNT = (PI * wheel_diameter_mm) / ticks_per_rev
#define WHEEL_DIAMETER_MM   12.0    // <-- measure your wheels
#define TICKS_PER_REV_LEFT  1175.0  // <-- measure on the day
#define TICKS_PER_REV_RIGHT 1166.0  // <-- measure on the day

#define MM_PER_COUNT_LEFT   (PI * WHEEL_DIAMETER_MM / TICKS_PER_REV_LEFT)
#define MM_PER_COUNT_RIGHT  (PI * WHEEL_DIAMETER_MM / TICKS_PER_REV_RIGHT)

// Distance between tyre centres (mm). Affects turn angle calculation.
#define TRACK_WIDTH_MM      75.0    // <-- measure your wheelbase

// ----------------------------- SENSOR THRESHOLDS -----------------------------
// MEASURE THESE ON THE REAL MAZE UNDER THE REAL LIGHTS.
// VL53L0X reports millimetres. Wall is at ~180mm when mouse is centred.
#define WALL_PRESENT_MM     200     // reading < this = wall definitely present
#define WALL_ABSENT_MM      400     // reading > this = wall definitely absent
// Between these two values = hysteresis: keep previous state.

// ----------------------------- MOTION PARAMETERS -----------------------------
// Start slow. Escalate on run 4-5.
#define SEARCH_SPEED        250     // mm/s
#define SEARCH_ACCEL        800     // mm/s^2
#define TURN_SPEED          150     // mm/s during pivot
#define TURN_ACCEL          600     // mm/s^2

// Banked run (run 2)
#define RUN_SPEED           400
#define RUN_ACCEL           1200

// Gambled run (runs 4-5)
#define FAST_SPEED          700
#define FAST_ACCEL          2000

#define CELL_MM             180.0
#define TURN_TOLERANCE_DEG  2.0

// ----------------------------- I2C ADDRESSES ---------------------------------
#define VL53_LEFT_ADDR      0x30
#define VL53_FRONT_ADDR     0x31
#define VL53_RIGHT_ADDR     0x32
#define MPU6050_ADDR        0x68

// ----------------------------- TIMING ----------------------------------------
#define SENSOR_INTERVAL_MS  20      // sample sensors every 20ms
#define CONTROL_INTERVAL_MS 5       // motion control loop every 5ms
#define SERIAL_BAUD         115200

// ============================================================================
// ============================ END OF CONFIG =================================
// ============================================================================

// ----------------------------- GLOBAL OBJECTS --------------------------------
Adafruit_VL53L0X loxLeft  = Adafruit_VL53L0X();
Adafruit_VL53L0X loxFront = Adafruit_VL53L0X();
Adafruit_VL53L0X loxRight = Adafruit_VL53L0X();
MPU6050 imu;

// ----------------------------- ENCODER STATE ---------------------------------
volatile long encoderLeft  = 0;
volatile long encoderRight = 0;

// ----------------------------- SENSOR STATE ----------------------------------
int  distLeft  = 9999;
int  distFront = 9999;
int  distRight = 9999;
bool wallLeft  = false;
bool wallFront = false;
bool wallRight = false;

// ----------------------------- IMU STATE -------------------------------------
float headingDeg = 0.0;
unsigned long lastImuMicros = 0;
float gyroBiasZ = 0.0;   // measured at startup

// ----------------------------- MAZE STATE ------------------------------------
#define MAZE_SIZE 16
uint8_t walls[MAZE_SIZE][MAZE_SIZE];   // bit 1=N, 2=E, 4=S, 8=W
uint8_t flood[MAZE_SIZE][MAZE_SIZE];

int currentX = 0;
int currentY = 0;
int currentHeading = 0;   // 0=N, 1=E, 2=S, 3=W

// Direction constants
const int dx[4] = { 0, 1, 0, -1 };
const int dy[4] = { 1, 0, -1, 0 };
const uint8_t wallBit[4]     = { 1, 2, 4, 8 };
const uint8_t oppositeBit[4] = { 4, 8, 1, 2 };

// ----------------------------- STATE MACHINE ---------------------------------
enum State { IDLE, SENSE, DECIDE, TURN, FORWARD, GOAL, PANIC };
State currentState = IDLE;

// ----------------------------- TIMING ----------------------------------------
unsigned long lastSensorRead = 0;
unsigned long lastControl    = 0;

// ============================================================================
// ============================== SETUP =======================================
// ============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);
  Serial.println();
  Serial.println(F("=== MICROMOUSE 2026 ==="));

  // --- LED for status ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // --- Motor pins ---
  pinMode(MOTOR_LEFT_DIR,  OUTPUT);
  pinMode(MOTOR_RIGHT_DIR, OUTPUT);
  pinMode(MOTOR_LEFT_PWM,  OUTPUT);
  pinMode(MOTOR_RIGHT_PWM, OUTPUT);
  digitalWrite(MOTOR_LEFT_DIR,  LOW);
  digitalWrite(MOTOR_RIGHT_DIR, LOW);

  // ESP32 Arduino core 3.x: ledcAttachChannel. Older: ledcSetup + ledcAttachPin.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(MOTOR_LEFT_PWM,  PWM_FREQ, PWM_RESOLUTION, MOTOR_LEFT_CH);
  ledcAttachChannel(MOTOR_RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION, MOTOR_RIGHT_CH);
#else
  ledcSetup(MOTOR_LEFT_CH,  PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(MOTOR_RIGHT_CH, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(MOTOR_LEFT_PWM,  MOTOR_LEFT_CH);
  ledcAttachPin(MOTOR_RIGHT_PWM, MOTOR_RIGHT_CH);
#endif
  setMotorPWM(0, 0);

  // --- Encoder pins ---
  pinMode(ENC_LEFT_A,  INPUT_PULLUP);
  pinMode(ENC_LEFT_B,  INPUT_PULLUP);
  pinMode(ENC_RIGHT_A, INPUT_PULLUP);
  pinMode(ENC_RIGHT_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_LEFT_A),  readEncoderLeft,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_RIGHT_A), readEncoderRight, CHANGE);

  // --- I2C bus ---
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- VL53L0X address assignment (MUST be in this order) ---
  setupDistanceSensors();

  // --- IMU ---
  setupIMU();

  // --- Maze ---
  initialiseMaze();

  // --- Ready ---
  Serial.println(F("RDY"));
  blinkLED(3);
}

// ============================================================================
// ============================== MAIN LOOP ===================================
// ============================================================================

void loop() {
  // --- Sensor sampling on a fixed schedule ---
  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    readDistanceSensors();
    updateWallFlags();
    updateHeading();
  }

  // --- State machine ---
  switch (currentState) {
    case IDLE:
      // Wait for button / serial command
      if (Serial.available()) {
        char c = Serial.read();
        if (c == 's' || c == 'S') {
          Serial.println(F("START"));
          currentState = SENSE;
        }
      }
      break;

    case SENSE:
      updateWallsFromSensors();
      floodFill();
      currentState = DECIDE;
      break;

    case DECIDE: {
      int nextDir = chooseDirection(currentX, currentY);
      if (nextDir == -1) {
        Serial.println(F("PANIC: no route"));
        currentState = PANIC;
        break;
      }
      // Turn to face nextDir
      int turnAmount = (nextDir - currentHeading) * 90;
      if (turnAmount > 180)  turnAmount -= 360;
      if (turnAmount < -180) turnAmount += 360;

      if (turnAmount != 0) {
        turnAngle(turnAmount);
        currentHeading = nextDir;
      }
      currentState = FORWARD;
      break;
    }

    case FORWARD:
      moveDistance(CELL_MM);
      currentX += dx[currentHeading];
      currentY += dy[currentHeading];
      Serial.print(F("At ")); Serial.print(currentX);
      Serial.print(',');       Serial.println(currentY);

      if (isGoal(currentX, currentY)) {
        Serial.println(F("GOAL"));
        currentState = GOAL;
      } else {
        currentState = SENSE;
      }
      break;

    case GOAL:
      stopMotors();
      blinkLED(5);
      while (true) { delay(1000); }   // park until reset
      break;

    case PANIC:
      stopMotors();
      blinkLED(20);
      while (true) { delay(1000); }
      break;
  }

  delay(1);
}

// ============================================================================
// ============================ SENSOR SETUP ==================================
// ============================================================================

void setupDistanceSensors() {
  // All three power up at 0x29. Wake one at a time via XSHUT.
  pinMode(XSHUT_LEFT,  OUTPUT);
  pinMode(XSHUT_FRONT, OUTPUT);
  pinMode(XSHUT_RIGHT, OUTPUT);
  digitalWrite(XSHUT_LEFT,  LOW);
  digitalWrite(XSHUT_FRONT, LOW);
  digitalWrite(XSHUT_RIGHT, LOW);
  delay(10);

  // --- Left sensor → 0x30 ---
  digitalWrite(XSHUT_LEFT, HIGH);
  delay(10);
  if (!loxLeft.begin(VL53_LEFT_ADDR)) {
    Serial.println(F("ERR: left VL53L0X"));
  } else {
    loxLeft.setMeasurementTimingBudget(20000);
  }

  // --- Front sensor → 0x31 ---
  digitalWrite(XSHUT_FRONT, HIGH);
  delay(10);
  if (!loxFront.begin(VL53_FRONT_ADDR)) {
    Serial.println(F("ERR: front VL53L0X"));
  } else {
    loxFront.setMeasurementTimingBudget(20000);
  }

  // --- Right sensor → 0x32 ---
  digitalWrite(XSHUT_RIGHT, HIGH);
  delay(10);
  if (!loxRight.begin(VL53_RIGHT_ADDR)) {
    Serial.println(F("ERR: right VL53L0X"));
  } else {
    loxRight.setMeasurementTimingBudget(20000);
  }

  Serial.println(F("VL53L0X ready (0x30, 0x31, 0x32)"));
}

void setupIMU() {
  imu.initialize();
  if (!imu.testConnection()) {
    Serial.println(F("ERR: MPU-6050"));
    return;
  }
  // Calibrate gyro bias with mouse absolutely still
  Serial.println(F("Calibrating IMU — do not move..."));
  delay(2000);
  long sum = 0;
  const int N = 200;
  for (int i = 0; i < N; i++) {
    int16_t gz;
    imu.getRotation(NULL, NULL, &gz);
    sum += gz;
    delay(5);
  }
  gyroBiasZ = (float)sum / N;
  Serial.print(F("Gyro bias Z: ")); Serial.println(gyroBiasZ);
  lastImuMicros = micros();
}

// ============================================================================
// ============================ SENSOR READING ================================
// ============================================================================

void readDistanceSensors() {
  VL53L0X_RangingMeasurementData_t m;

  loxLeft.rangingTest(&m, false);
  distLeft = (m.RangeStatus != 4) ? m.RangeMilliMeter : 9999;

  loxFront.rangingTest(&m, false);
  distFront = (m.RangeStatus != 4) ? m.RangeMilliMeter : 9999;

  loxRight.rangingTest(&m, false);
  distRight = (m.RangeStatus != 4) ? m.RangeMilliMeter : 9999;
}

// Hysteresis: only flip state if reading crosses the opposite threshold
void updateWallFlags() {
  if (distLeft < WALL_PRESENT_MM)       wallLeft  = true;
  else if (distLeft > WALL_ABSENT_MM)   wallLeft  = false;
  // else keep previous

  if (distFront < WALL_PRESENT_MM)      wallFront = true;
  else if (distFront > WALL_ABSENT_MM)  wallFront = false;

  if (distRight < WALL_PRESENT_MM)      wallRight = true;
  else if (distRight > WALL_ABSENT_MM)  wallRight = false;
}

void updateHeading() {
  int16_t gz;
  imu.getRotation(NULL, NULL, &gz);
  unsigned long now = micros();
  float dt = (now - lastImuMicros) / 1000000.0;
  lastImuMicros = now;
  // MPU-6050: 131 LSB per deg/s at ±250 deg/s range
  float rate = (gz - gyroBiasZ) / 131.0;
  headingDeg += rate * dt;
}

// ============================================================================
// ============================ ENCODER ISRs ==================================
// ============================================================================

void IRAM_ATTR readEncoderLeft() {
  int a = digitalRead(ENC_LEFT_A);
  int b = digitalRead(ENC_LEFT_B);
  encoderLeft += ENC_LEFT_POLARITY * ((a == b) ? 1 : -1);
}

void IRAM_ATTR readEncoderRight() {
  int a = digitalRead(ENC_RIGHT_A);
  int b = digitalRead(ENC_RIGHT_B);
  encoderRight += ENC_RIGHT_POLARITY * ((a == b) ? 1 : -1);
}

// ============================================================================
// ============================ MOTOR CONTROL =================================
// ============================================================================

void setMotorPWM(int left, int right) {
  left  = constrain(left,  -255, 255);
  right = constrain(right, -255, 255);

  left  *= MOTOR_LEFT_POLARITY;
  right *= MOTOR_RIGHT_POLARITY;

  // Left
  if (left >= 0) {
    digitalWrite(MOTOR_LEFT_DIR, LOW);
    ledcWrite(MOTOR_LEFT_CH, left);
  } else {
    digitalWrite(MOTOR_LEFT_DIR, HIGH);
    ledcWrite(MOTOR_LEFT_CH, -left);
  }

  // Right
  if (right >= 0) {
    digitalWrite(MOTOR_RIGHT_DIR, LOW);
    ledcWrite(MOTOR_RIGHT_CH, right);
  } else {
    digitalWrite(MOTOR_RIGHT_DIR, HIGH);
    ledcWrite(MOTOR_RIGHT_CH, -right);
  }
}

void stopMotors() {
  setMotorPWM(0, 0);
}

// ============================================================================
// ============================ MOTION PRIMITIVES =============================
// ============================================================================

// Move forward exactly `mm`. Simple P controller on encoder delta.
void moveDistance(float mm) {
  long startL = encoderLeft;
  long startR = encoderRight;
  long targetL = (long)(mm / MM_PER_COUNT_LEFT);
  long targetR = (long)(mm / MM_PER_COUNT_RIGHT);

  unsigned long timeout = millis() + 5000;   // safety

  while (millis() < timeout) {
    long doneL = encoderLeft  - startL;
    long doneR = encoderRight - startR;

    if (abs(doneL) >= abs(targetL) && abs(doneR) >= abs(targetR)) break;

    float errL = targetL - doneL;
    float errR = targetR - doneR;
    int pwm = constrain((int)((errL + errR) * 0.5 * 1.5), -200, 200);
    setMotorPWM(pwm, pwm);
    delay(2);
  }
  setMotorPWM(0, 0);
  delay(50);
}

// Turn in place by `degrees`. Positive = left, negative = right.
void turnAngle(float degrees) {
  float startHeading = headingDeg;
  float target = startHeading + degrees;

  unsigned long timeout = millis() + 5000;

  while (millis() < timeout) {
    float err = target - headingDeg;
    if (abs(err) < TURN_TOLERANCE_DEG) break;

    int pwm = constrain((int)(err * 3.0), -180, 180);
    setMotorPWM(pwm, -pwm);   // spin in place
    delay(2);
  }
  setMotorPWM(0, 0);
  delay(100);
}

// ============================================================================
// ============================ MAZE LOGIC ====================================
// ============================================================================

void initialiseMaze() {
  for (int x = 0; x < MAZE_SIZE; x++)
    for (int y = 0; y < MAZE_SIZE; y++)
      walls[x][y] = 0;

  // Border walls
  for (int x = 0; x < MAZE_SIZE; x++) {
    walls[x][0] |= 4;                 // south
    walls[x][MAZE_SIZE - 1] |= 1;     // north
  }
  for (int y = 0; y < MAZE_SIZE; y++) {
    walls[0][y] |= 8;                 // west
    walls[MAZE_SIZE - 1][y] |= 2;     // east
  }
  // Start cell: walled on three sides (assume start at 0,0 facing north)
  walls[0][0] |= 2;   // east wall of start
  walls[0][0] |= 4;   // south wall of start
  walls[0][0] |= 8;   // west wall of start

  currentX = 0; currentY = 0; currentHeading = 0;
  floodFill();
}

bool isGoal(int x, int y) {
  return (x == 7 || x == 8) && (y == 7 || y == 8);
}

void setWall(int x, int y, int dir) {
  walls[x][y] |= wallBit[dir];
  int nx = x + dx[dir];
  int ny = y + dy[dir];
  if (nx >= 0 && nx < MAZE_SIZE && ny >= 0 && ny < MAZE_SIZE)
    walls[nx][ny] |= oppositeBit[dir];
}

void updateWallsFromSensors() {
  if (wallFront) setWall(currentX, currentY, currentHeading);
  if (wallLeft)  setWall(currentX, currentY, (currentHeading + 3) % 4);
  if (wallRight) setWall(currentX, currentY, (currentHeading + 1) % 4);
}

// Standard BFS flood fill from the 4 goal cells
void floodFill() {
  for (int x = 0; x < MAZE_SIZE; x++)
    for (int y = 0; y < MAZE_SIZE; y++)
      flood[x][y] = 255;

  int queueX[MAZE_SIZE * MAZE_SIZE];
  int queueY[MAZE_SIZE * MAZE_SIZE];
  int head = 0, tail = 0;

  for (int x = 7; x <= 8; x++) {
    for (int y = 7; y <= 8; y++) {
      flood[x][y] = 0;
      queueX[tail] = x;
      queueY[tail] = y;
      tail++;
    }
  }

  while (head < tail) {
    int x = queueX[head];
    int y = queueY[head];
    head++;

    for (int dir = 0; dir < 4; dir++) {
      if (walls[x][y] & wallBit[dir]) continue;
      int nx = x + dx[dir];
      int ny = y + dy[dir];
      if (nx < 0 || nx >= MAZE_SIZE || ny < 0 || ny >= MAZE_SIZE) continue;
      if (flood[nx][ny] > flood[x][y] + 1) {
        flood[nx][ny] = flood[x][y] + 1;
        queueX[tail] = nx;
        queueY[tail] = ny;
        tail++;
      }
    }
  }
}

int chooseDirection(int x, int y) {
  int bestDir = -1;
  int bestCost = 255;
  for (int dir = 0; dir < 4; dir++) {
    if (walls[x][y] & wallBit[dir]) continue;
    int nx = x + dx[dir];
    int ny = y + dy[dir];
    if (nx < 0 || nx >= MAZE_SIZE || ny < 0 || ny >= MAZE_SIZE) continue;
    if (flood[nx][ny] < bestCost) {
      bestCost = flood[nx][ny];
      bestDir = dir;
    }
  }
  return bestDir;
}

// ============================================================================
// ============================ UTILITIES =====================================
// ============================================================================

void blinkLED(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(100);
    digitalWrite(LED_BUILTIN, LOW);  delay(100);
  }
}
