Code Architecture Notes — Micromouse 2026
Purpose: A practical blueprint for structuring Micromouse code.
Note: This is a blueprint, not copy-paste code. Pin numbers and kit-specific details will differ.

1. The Golden Rule
Build for a finish first. Always keep a working version to go back to.
Your code should always have a version that can:
Move one cell reliably
Turn 90° reliably
Reach the centre slowly
Only increase speed once those fundamentals work.
A mouse that reliably finishes in 60 seconds beats one that would have done 12 and didn't.

2. Overall Structure
Organize the code into distinct layers, rather than one giant loop:
┌─────────────────────────────────────┐
│         MAIN STATE MACHINE          │  ← Decides what to do next
├─────────────────────────────────────┤
│         MAZE / FLOOD FILL           │  ← The algorithm
├─────────────────────────────────────┤
│         SENSOR FUSION               │  ← Where am I?
├─────────────────────────────────────┤
│         MOTION CONTROL              │  ← How do I move?
├─────────────────────────────────────┤
│         HARDWARE ABSTRACTION        │  ← Motors, encoders, IMU, sensors
└─────────────────────────────────────┘

Each layer should only communicate with the layer below it.
This allows you to:
Debug motion without touching flood fill
Swap sensors without rewriting the algorithm
Keep the main control logic understandable

3. Data Structures
3.1 Maze
#define MAZE_SIZE 16

// Wall bits: 1=North, 2=East, 4=South, 8=West
uint8_t walls[MAZE_SIZE][MAZE_SIZE];

// Flood values (distance to goal)
uint8_t flood[MAZE_SIZE][MAZE_SIZE];

// Goal: 2x2 block at centre (cells 7,7 / 7,8 / 8,7 / 8,8)
bool isGoal(int x, int y) {
    return (x == 7 || x == 8) && (y == 7 || y == 8);
}

Critical rule
A wall between cell (x,y) and (x,y+1) belongs to both cells.
When setting a wall, set the corresponding bit in the adjacent cell too.

3.2 Position & Heading
int currentX = 0, currentY = 0;   // Start at bottom-left
int currentHeading = 0;            // 0=North, 1=East, 2=South, 3=West


3.3 Motion Parameters
Starting values should be tuned from measurement:
const float CELL_MM = 180.0;
float COUNTS_PER_MM = 2.5;        // Measure this: counts per mm of travel
float TURN_TOLERANCE = 2.0;       // degrees

// Movement profiles
float accel = 500;                // mm/s²
float cruise = 300;               // mm/s (banked run)
float decel = 500;                // mm/s²


4. The State Machine
The main loop should be a clean state machine, not spaghetti.
Six states are enough:
enum State {
    IDLE,       // Waiting for start signal
    SENSE,      // Read sensors, update walls
    DECIDE,     // Pick next direction from flood
    TURN,       // Execute 90° turn
    FORWARD,    // Move exactly one cell
    GOAL        // Reached centre
};

State currentState = IDLE;

Main Loop
void loop() {
    switch (currentState) {

        case IDLE:
            if (startButtonPressed())
                currentState = SENSE;
            break;

        case SENSE:
            updateWallsFromSensors();
            floodFill();
            currentState = DECIDE;
            break;

        case DECIDE:
            nextDir = chooseDirection(
                currentX,
                currentY,
                currentHeading
            );

            if (nextDir == -1) {
                /* stuck */
            } else {
                currentState = TURN;
            }
            break;

        case TURN:
            if (turnComplete())
                currentState = FORWARD;
            break;

        case FORWARD:
            if (moveComplete()) {
                currentX += dx[currentHeading];
                currentY += dy[currentHeading];

                if (isGoal(currentX, currentY))
                    currentState = GOAL;
                else
                    currentState = SENSE;
            }
            break;

        case GOAL:
            stopMotors();
            break;
    }
}

Why this matters
A clean state machine makes debugging possible.
You can print the current state and see exactly where the mouse gets stuck.

5. Flood Fill — The Algorithm
5.1 BFS Structure
void floodFill() {

    // 1. Initialize all to 255
    for (int x = 0; x < MAZE_SIZE; x++)
        for (int y = 0; y < MAZE_SIZE; y++)
            flood[x][y] = 255;

    // 2. Queue for BFS
    int queueX[MAZE_SIZE * MAZE_SIZE];
    int queueY[MAZE_SIZE * MAZE_SIZE];
    int head = 0, tail = 0;

    // 3. Seed the goal cells
    //    4 cells, all with flood = 0
    for (int x = 7; x <= 8; x++) {
        for (int y = 7; y <= 8; y++) {
            flood[x][y] = 0;
            queueX[tail] = x;
            queueY[tail] = y;
            tail++;
        }
    }

    // 4. BFS
    while (head < tail) {

        int x = queueX[head];
        int y = queueY[head];
        head++;

        for (int dir = 0; dir < 4; dir++) {

            if (walls[x][y] & wallBit[dir])
                continue;  // Wall blocks

            int nx = x + dx[dir];
            int ny = y + dy[dir];

            if (nx < 0 || nx >= MAZE_SIZE ||
                ny < 0 || ny >= MAZE_SIZE)
                continue;

            if (flood[nx][ny] > flood[x][y] + 1) {
                flood[nx][ny] = flood[x][y] + 1;
                queueX[tail] = nx;
                queueY[tail] = ny;
                tail++;
            }
        }
    }
}


5.2 Direction Constants
// North=0, East=1, South=2, West=3
int dx[] = {0, 1, 0, -1};
int dy[] = {1, 0, -1, 0};   // Y increases north

uint8_t wallBit[]     = {1, 2, 4, 8};
uint8_t oppositeBit[] = {4, 8, 1, 2};


5.3 Recording Walls
void setWall(int x, int y, int dir) {

    walls[x][y] |= wallBit[dir];

    int nx = x + dx[dir];
    int ny = y + dy[dir];

    if (nx >= 0 && nx < MAZE_SIZE &&
        ny >= 0 && ny < MAZE_SIZE) {

        walls[nx][ny] |= oppositeBit[dir];
        // Set in BOTH cells
    }
}


5.4 Choosing a Direction
int chooseDirection(int x, int y, int currentHeading) {

    int bestDir = -1;
    int bestFlood = 255;

    for (int dir = 0; dir < 4; dir++) {

        if (walls[x][y] & wallBit[dir])
            continue;

        int nx = x + dx[dir];
        int ny = y + dy[dir];

        if (nx < 0 || nx >= MAZE_SIZE ||
            ny < 0 || ny >= MAZE_SIZE)
            continue;

        if (flood[nx][ny] < bestFlood) {
            bestFlood = flood[nx][ny];
            bestDir = dir;
        }
    }

    return bestDir;
}


6. Weighted Flood Fill
The Upgrade — Hour 4–5
Plain flood fill finds the route with the fewest cells.
For racing, you want the route with the lowest time.
A straight is nearly free, while a turn costs a slow-down and a speed-up. Eleven cells with ten turns and eleven cells with one turn look identical to naive flood fill but can be seconds apart on the floor.
const float STRAIGHT = 1.0;
const float TURN_90  = 3.5;
// TUNE: time 10 straight cells vs 10 cells with a turn

float move_cost(int heading_in, int heading_out) {
    return (heading_in == heading_out)
        ? STRAIGHT
        : STRAIGHT + TURN_90;
}

Because cost depends on heading, model each cell as four states — one per heading — and flood over 1024 states.
This is the optimization for racing.
Recommended progression
Hour 3–4: Use plain flood fill.
Get reliably to the centre.
Then: Upgrade to weighted flood fill.

7. Motion Control
7.1 Principle
Command motion in distance, not time.
Accelerate at a fixed rate to a cruise speed, hold that speed, then decelerate to arrive at the target.
A banked run and a flat-out run should use the same code with different cruise speeds and acceleration values.

7.2 moveDistance()
void moveDistance(float mm) {

    long targetCounts = mm * COUNTS_PER_MM;
    long startCounts = encoderRead();

    while (abs(encoderRead() - startCounts) < abs(targetCounts)) {

        float error =
            targetCounts - (encoderRead() - startCounts);

        // Trapezoidal profile would go here
        // (accelerate, cruise, decelerate)

        // For hour 1–2, simple P controller:
        int pwm = constrain(
            error * KP_DISTANCE,
            -MAX_PWM,
            MAX_PWM
        );

        setMotorPWM(pwm, pwm);
    }

    stopMotors();
    brake();
}


7.3 turnAngle()
void turnAngle(float degrees) {

    float startHeading = readYaw();
    float targetHeading = startHeading + degrees;

    while (abs(targetHeading - readYaw()) > TURN_TOLERANCE) {

        float error = targetHeading - readYaw();

        int pwm = constrain(
            error * KP_TURN,
            -MAX_PWM,
            MAX_PWM
        );

        setMotorPWM(pwm, -pwm);   // Spin in place
    }

    stopMotors();
}


7.4 Turn Primitive Progression
Hour 1–2: Pivot turn — stop, rotate, go. Slow but reliable.
Hour 4–5: Arc turn — a fixed manoeuvre with known entry offset, radius, and exit offset.
Let the planner call the arc turn by name.
Do not compute turn geometry during a run.

8. Sensor Handling
8.1 Read at Consistent Points
Read walls at a consistent point in the cell.
A single false wall can send the mouse confidently into a dead end.
// Read walls when the mouse is at the CENTRE of the cell,
// not at the boundary where posts cause spikes.

void updateWallsFromSensors() {

    if (wallFront)
        setWall(currentX, currentY, currentHeading);

    if (wallLeft)
        setWall(
            currentX,
            currentY,
            (currentHeading + 3) % 4
        );

    if (wallRight)
        setWall(
            currentX,
            currentY,
            (currentHeading + 1) % 4
        );
}


8.2 Hysteresis — Two Thresholds
const int WALL_PRESENT = 200;   // mm — definitely a wall
const int WALL_ABSENT  = 400;   // mm — definitely no wall

bool updateWallState(int mm, bool prevState) {

    if (mm < WALL_PRESENT)
        return true;

    if (mm > WALL_ABSENT)
        return false;

    return prevState;   // In between — keep previous
}


8.3 Sample in a Timer
Use a hardware timer interrupt for sensor sampling rather than relying on the main loop.
// Fixed rate, independent of how long the main loop takes

hw_timer_t *timer = NULL;

void IRAM_ATTR onTimer() {
    readSensors();
    updateWallStates();
}

Why?
A single false wall can send the mouse confidently into a dead end.
Consistent sampling + hysteresis helps prevent this.

9. Position Correction — The Three Layers
Encoder position is wrong: wheels slip and tyre diameters differ. Over a 40-cell run that error puts you in a wall.
Use all three corrections together.
9.1 Heading — IMU
// Calibrate gyro zero-rate offset
// with mouse absolutely still at power-up.

float readYaw() {

    int16_t gz;
    imu.getRotation(&gz, NULL, NULL);

    unsigned long now = millis();

    float dt =
        (now - lastImuTime) / 1000.0;

    lastImuTime = now;

    currentHeading +=
        (gz / 131.0) * dt;
    // 131 LSB/°/s for ±250°/s

    return currentHeading;
}

9.2 Lateral — Side Walls
Use side walls with PID to keep the mouse centred.
Keeps the mouse centred
Stops working when a wall disappears
Detect the disappearance and hold heading instead
9.3 Longitudinal — Front Wall
Every visible front wall provides a free reset of accumulated distance error.
Critical
When a side wall disappears, the centring loop will steer hard at the gap.
Detect the wall disappearance and hold heading.

10. Two-Phase Strategy
10.1 Search Run
// Slow, careful, map-building

// Treat unknown walls as OPEN (optimistic)

// Go to goal, come back to start

// Return leg sees walls from the other side for free


10.2 Check Before Racing
// Flood twice:
//
// 1. Once with unknowns open
// 2. Once with unknowns walled
//
// If route costs match:
//     stop searching and race
//
// If optimistic cost is much lower:
//     unexplored cells matter — check them


10.3 Budget Exploration
Decide in code, not in the moment, how much of the slot goes to exploring.
// Hard-code your exploration budget

const unsigned long SEARCH_TIME_LIMIT = 120000;
// 2 minutes

if (millis() - searchStartTime > SEARCH_TIME_LIMIT) {

    // Stop exploring
    // Use best known route
    // Switch to speed run
}


11. Escalation Ladder
// Run 1: Search
float cruiseSpeed = 200;   // Slow, careful
bool explore = true;

// Run 2: Conservative speed run
float cruiseSpeed = 300;   // Banked time
bool explore = false;

// Run 3: Faster
float cruiseSpeed = 450;   // If it makes mistakes, back off

// Runs 4–5: Escalate until it breaks
float cruiseSpeed = 600;   // A crash now costs nothing

Only your best run counts. Risk is cheap once you have a time on the board.

12. What to Build, and When
Hour
Code Focus
0–1
Blink LED, motor spin, encoder counts change correctly
1–2
moveDistance(180) and turnAngle(90) — repeat 10× each, measure drift
2–3
Read sensor values, set thresholds on real maze, add hysteresis, wall-centring loop
3–4
Wire plain flood fill to movement primitives, search slowly, reach centre
4–5
Add return leg, weighted flood fill, speed run, escalate in steps
5–6
Freeze, charge battery, run full sequence twice, document banked settings


13. Failure Modes
Failure
Code Prevention
Phantom wall
Only record walls from consistent cell positions
Accumulated heading error
Correct against walls continuously; don't trust gyro alone
Corridor with no side walls
Detect wall disappearance, hold heading
Wrong start pose
Make it impossible for a human to place wrong
Flat battery
Know voltage-fall behaviour; charge between sessions
Untested return leg
Test turning around inside goal block
Recovery wipes map
Know if you're willing to pay that before raising hand


14. Code Mantras
Distance, not time — command motion in mm, not milliseconds.
Cost turns, not cells — a turn costs 3.5× a straight.
Read walls at consistent points — never at cell boundaries.
Hysteresis, not single thresholds — two thresholds for wall present/absent.
Three corrections together — IMU + side walls + front wall.
Clean state machine — not spaghetti.
Build for a finish first — speed is the last thing you add.


