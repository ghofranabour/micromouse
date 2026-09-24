Micromouse 2026 — Final Notes & Cheat Sheet
Everything you need to know about the rules, strategy, hardware, and general competition format, distilled from the document.
Review this before the competition.

1. The Maze — UKMARS Classic Standard
Specification
Value
Grid
16 × 16 cells
Total size
2880 mm × 2880 mm
Cell pitch
180 mm centre-to-centre
Clear passage
168 mm between walls
Wall thickness
12 mm
Wall height
50 mm
Posts
12 × 12 mm at every lattice point, even where no wall exists
Start
Corner cell, walled on three sides
Goal
2 × 2 block at centre, with one entrance
Finish
White wall sides, red tops, matt black floor

Key Design Notes
Posts provide a 180 mm reference feature for distance correction, but they can also cause sensor spikes that must not be interpreted as walls.


The goal is a set of four cells, not a single coordinate.


Calibrate sensors under the same lighting conditions used for racing.


Nothing is bespoke to Dublin. Maze files, simulators, algorithms, and write-ups from the wider sport apply directly.



2. What Counts as a Micromouse?
A legal mouse:
Fits inside a 25 cm × 25 cm square at all times, measured on the floor. No unfolding.


Is fully autonomous and self-contained.


Uses no remote control, tether, or off-board computing.


Remains on the floor and uses the maze openings — no flying, jumping, or climbing.


Does not damage or mark the maze.


Does not shed parts during a run.


The Unwritten Rule
The maze is unseen.
You cannot hard-code a route or tune the robot to a known layout. Your software must work on a maze handed to you cold.
There is no weight, cost, or height limit. The rules protect one thing: the mouse must solve the maze itself.

3. Timing & Scoring
You get 10 minutes in the maze, or fewer if the entry list requires it.


You may make up to 5 runs.


A run is timed from leaving the start cell to reaching the goal.


Time between runs is not run time, but it does count against your 10-minute slot.


Only your best single run counts.


A slow exploratory run therefore does not drag down your score.


You may not touch a running mouse unless instructed by a judge.


A requested recovery erases the mouse's memory of the maze.


Where a touch is permitted and the run continues, the penalty is:


+3 seconds


+10% of the run time


Competition Strategy
Explore → bank a safe finish → increase speed run by run.
One recorded 45-second run beats four spectacular crashes and no time on the board.

4. The Dublin Format
Competition Day
6-hour build window


One identical kit per team


Design, build, program, and race on the day


Standard Kit
Every team receives:
ESP32-C6


Encoded motors


Motor driver


IMU


Distance sensors


BMS


Buck converter


Battery pack


Judging
Fastest verified run to the centre wins.


Additional awards recognise design and reliability.


Practice
Full-size practice mazes are available all day.


Mini practice mazes are available all day.


One mini maze is deliberately designed so that the fastest route cuts diagonally.


No Experience Required
Before the event:
Pre-recorded video series covering sensor driving with the ESP32


Setup documentation published in advance


On the day:
Full briefing with diagrams


GitHub basics


Maze-solving strategy walkthrough


Throughout the event:
Committee members and postgraduate demonstrators are available on the floor.


Stuck? Someone should be able to help within about a minute.

5. Race Strategy — The Two-Phase Race
Micromouse is essentially two different races wearing one costume.
Phase 1 — Search Run
The mouse knows nothing.
It drives carefully, gathers sensor information, and builds a map.
Goal: Information, not speed.
Phase 2 — Speed Run
The mouse uses the map to calculate the best route and drives it as quickly as the hardware allows.
The Run Ladder
Run
Purpose
1
Search — slow, careful, map-building. Reach the centre.
2
Conservative speed run — bank a time.
3
Faster, or briefly re-explore if a better route may exist.
4–5
Escalate until it breaks. A crash now costs you nothing.

Core Principle
Only your best run counts.
Risk is cheap after you have a time on the board and ruinous before you do.
Teams lose by trying to win on run one.

6. Why Wall Following Fails
The classic left-hand rule only works when every wall is connected to the maze's outer boundary.
The goal block in Micromouse is a central island.
Following the boundary wall can therefore loop back to the start indefinitely without ever entering the centre.
If your fallback plan is "at least the wall follower will finish," it will not.
You need a mouse that builds a map and searches it.

7. Reaching the Centre ≠ Knowing the Maze
Your map only contains the cells you have actually observed.
The fastest route may pass through cells you never entered, and your map cannot know whether those cells are open.
Recommended Approach
Go to the goal with unknown walls treated as open.


Return to the start.


Use the return trip to observe walls from the opposite direction.


Check only the cells that matter.


Two Flood-Fill Tests
Run the map twice:
Optimistic map: unknown walls are treated as open.


Conservative map: unknown walls are treated as blocked.


If both routes have the same cost, further exploration may not be useful.
If the optimistic route is significantly cheaper, the difference is likely caused by unexplored cells worth checking.
Exploration Budget
A search can consume 2–3 minutes of a 10-minute slot.
Decide in code, rather than in the heat of competition, how much of the slot is allocated to exploration.

8. Flood Fill & Routing
Don't Optimise for Cells Alone
A naive flood fill treats these as equivalent:
11 cells with 10 turns


11 cells with 1 turn


They are not equivalent on the floor.
A straight is almost free. A turn requires deceleration, rotation, and acceleration.
Starting Cost Model
STRAIGHT = 1.0
TURN_90  = 3.5

Tune these values from real measurements.
Heading-Aware Routing
Represent each cell as four states, one for each heading.
That gives:
16 × 16 × 4 = 1024 states
Flood-fill across those states so that the planner can account for turning costs.
Diagonals
A staircase of alternating turns can effectively become a 45° straight-line route.
Benefits:
Approximately 29% less ground covered for the same grid displacement.


Costs:
Much less clearance between the mouse and the posts.


Greater sensitivity to heading error.


Requires good control and a narrow mouse.


Make the orthogonal speed run reliable before attempting diagonals.

9. Motion & Hardware
Motion Control
Command motion in distance, not time.
The basic profile should be:
Accelerate → cruise → decelerate
A banked run and a flat-out run should use the same motion code, with different:
Cruise speed


Acceleration


Turning
Build turns in stages:
Pivot turn — stop, rotate, go.


Characterised arc turn — a fixed, tested manoeuvre.


Only later consider more advanced turn profiles.


Do not calculate turn geometry during a competition run.
Pre-characterise the manoeuvre and execute it deterministically.

10. Knowing Where You Are
Encoder position alone is not enough.
Wheel slip and different tyre diameters create accumulated error. Over a 40-cell run, that error can be enough to put the mouse into a wall.
Use three corrections together.
1. Heading — IMU
Use the IMU to track heading.
At power-up:
Keep the mouse completely still.


Measure the gyro zero-rate offset.


Calibrate before driving.


2. Lateral Position — Side Walls
Use a PID loop based on the difference between left and right wall distances.
This keeps the mouse centred.
However, the system must recognise when a wall disappears.
When there is no usable side wall:
Hold heading instead of steering aggressively toward the missing wall.
3. Longitudinal Position — Front Walls
Every visible front wall is an opportunity to reset accumulated distance error.
Use it as a positional reference.

11. Sensor Discipline
Sensors should be treated as part of the control system, not just as occasional measurements.
Sample at a fixed rate using a timer.


Use hysteresis with separate thresholds for wall-present and wall-absent.


Read walls at a consistent point within each cell.


Do not let individual sensor spikes immediately alter the map.


This is especially important because the maze's posts can create readings that look like walls.

12. Hardware Decisions to Make in Hour One
These choices become effectively permanent once the hardware is soldered.
Decision
Guidance
Geometry
Put the wheel axis under the centre of rotation, otherwise turns also translate the mouse.
Mass
Light beats powerful. Battery placement affects handling.
Width
Narrower means more margin for diagonal routes, not merely easier fitting.
Sensors
Angle side sensors slightly forward and aim them at mid-height on the wall face.
Power
Battery voltage sags during acceleration. Give logic a stable rail from the buck converter.
Traction
Grip is the real speed limit. If more acceleration stops producing more speed, traction is probably the limiting factor.


13. The Six-Hour Playbook
Time
Focus
Target
0–1 h
Move something
Blink an LED, run both motors, verify encoder direction/counts. Split the team into mechanics, motion, and sensors.
1–2 h
Drive one cell
Travel exactly 180 mm and stop. Turn exactly 90° and stop. Repeat each 10× and measure drift.
2–3 h
See walls
Print raw sensor values, set thresholds on the real maze under the real lights, add hysteresis, and get wall-centring working.
3–4 h
Solve slowly
Connect flood fill to movement primitives and deliberately search at low speed. Reaching the centre makes you competition-legal.
4–5 h
Return + speed up
Add the return leg and speed run. Increase speed in steps until errors appear, then back off one step.
5–6 h
Freeze + rehearse
Stop adding features. Charge the battery and run the full sequence end-to-end at least twice. Record the banked setting and the gamble setting.

The Golden Rule
A mouse that reliably finishes in 60 seconds beats one that would have done 12 seconds but never finished.
Build for a finish first.
Always keep a working version you can return to.

14. The Ways Mice Actually Fail
Failure
Prevention
Phantom wall in the map
Record a wall only from a reading taken at a consistent position within the cell.
Accumulated heading error
Continuously correct heading against walls. Do not rely on the gyro alone.
Corridor with no side walls
Detect the missing wall and hold heading instead of steering toward the gap.
Wrong starting position
Make the start pose obvious and difficult to get wrong when placing the mouse.
Flat battery mid-slot
Charge between sessions and understand behaviour as voltage falls.
Untested return leg
Test turning around inside the 2 × 2 goal block.
Recovery at the wrong moment
Recovery can wipe the map. Decide beforehand whether the situation is worth paying that cost.


15. The Diagonal Maze
One mini practice maze is deliberately designed so the fastest route cuts diagonally.
A mouse that only follows the grid will finish.


A mouse that thinks about geometry can exploit the diagonal route.


The maze is open all day, so use it to find out which type of mouse you built.


Priority
First: reliable orthogonal navigation.
Then: diagonal optimisation.
Do not sacrifice a reliable competition run for an untested diagonal strategy.

16. Resources
Watch
Veritasium — The Fastest Maze-Solving Competition On Earth


UKMARS — Full contest run from start to finish


IEEE Bruins — Micromouse lecture on flood fill


APEC 2024 — HAL900's fastest run; focus on the route it chooses, not just the speed


Rules & Reading
ukmars.org — UK Micromouse Classic rules; the definitive rules source


micromouseonline.com — Peter Harrison's Micromouse book


micromouseonline.com — Mazes and maze-solving chapter


Wikipedia — Micromouse overview


Code & Simulation
GitHub: mackorone/mms — Micromouse simulator for testing algorithms without the robot


GitHub: ukmars/mazerunner-core — Complete, readable firmware


GitHub: micromouseonline/mazefiles — Decades of real contest maze files



17. The One-Sentence Summary
Build a mouse that reliably finishes first — then make it fast. Only your best run counts. Risk is cheap once you have a time on the board, and ruinous before it.



