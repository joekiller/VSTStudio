# Configuration reference (`VST.cfg`)

`VST.cfg` holds everything the tracker needs to know about one camera view: where the street is in the picture,
where the speed zone is, which way traffic is allowed to go, and the calibration. **VST Studio writes it for you** as
you work through its setup steps, so most people never open it. This page is for anyone who wants to read it, edit it
by hand, or set up a headless box.

Each program prints `Using config <path>` when it starts. Where the file lives is described in
[Installing VST Studio](install.md#where-settings-and-data-live).

## Format

```
# A comment line
key = value      # text after # is a comment
```

- Keys can be in any order and any letter case. Blank lines and comment lines are fine, as are Windows (CRLF) or Unix
  line endings.
- A key you leave out keeps its built-in default, listed below.
- Text values can be bare (`SE`) or quoted (`"SE"`). Pairs are written `[left,right]`.
- A misspelled key or a value VST can't read stops it with the file name, line number and the list of known keys. If
  a key appears twice, the later line wins and VST says so.
- **Pixel values are in a 1280 x 720 picture**, with the origin at the top left (x to the right, y down). Video of any
  other size is scaled to 1280 x 720 before the tracker sees it, so measure positions on a 1280 x 720 copy of a frame.

The `VST.cfg` that ships with VST Studio is set up for the bundled 2016 sample video from Locust Avenue, so its
values are a working example. The defaults below are the values built into the program.

## Where things are in the picture

| Key | Default | Meaning |
|---|---|---|
| `AnalysisBoxLeft`, `AnalysisBoxTop` | 10, 220 | Top-left corner of the *analysis box*, the part of the picture the tracker looks at. Make it hold both lanes and the full height of the cars, and as little else as possible: no sky, no swaying trees, no sidewalk. |
| `AnalysisBoxWidth`, `AnalysisBoxHeight` | 1269, 190 | Size of the box. Left + Width must be at most 1280 and Top + Height at most 720. |
| `speedLineLeft`, `speedLineRight` | 350, 909 | x of the two white *speed lines* that bound the timing zone, **relative to `AnalysisBoxLeft`**. `0 <= speedLineLeft < speedLineRight <= AnalysisBoxWidth`. Put them where cars are wholly in view, in the flattest middle part of the picture. |
| `R2LStreetY`, `L2RStreetY` | 122, 158 | The *lane lines*, one per direction, at about hubcap height, **relative to `AnalysisBoxTop`** (1 to `AnalysisBoxHeight`). Each direction's cars are looked for only above its line, which keeps shadows on the road in front of a car from confusing the tracker. R2L is usually the far lane, so its line is higher (smaller y). On a one-way street both directions use the same lane, so both lines are the same. |
| `R2LStreetYRight`, `L2RStreetYRight` | 0 | Where each lane line meets the right edge of the box, for a street that runs at a slant across the picture. 0 means level (the same as the left end). |
| `obstruction` | `[0,0]` | `[left,right]` x range, relative to `AnalysisBoxLeft`, of one pole or tree trunk in front of the street. The tracker carries a car's bumper across it by prediction. `[0,0]` (or any left >= right) means none. |
| `obstruction_extent` | 30 | Extra pixels past the obstruction before a car shows up again in the motion picture. |
| `levelDegrees` | 0 | For a camera that isn't mounted level: every frame is turned this many degrees (positive is counter-clockwise, -15 to 15) before anything else sees it, so lanes run straight across. Set it before the box, lines and calibration, since a new angle moves the street under them. VST Studio's **Level the picture** sets it. |
| `streetEdge` | `[0,0]` | Optional line along the street (the curb, a lane stripe, the tops of parked cars), at the box's left and right edges, relative to `AnalysisBoxTop`. Where it closes in on a lane line is the street's vanishing point; with `subFrameTiming = 1`, VST then measures each bumper along the ground, so a street that recedes across the picture still times correctly. Parallel to the lane line, it changes nothing. `[0,0]` is off. Refused if it crosses a lane line inside the box or makes the far end more than three times as far away. |

## Directions and one-way streets

| Key | Default | Meaning |
|---|---|---|
| `L2RDirection`, `R2LDirection` | `SE`, `NW` | Compass labels for traffic moving left to right and right to left across the picture. They are labels in the results. |
| `LegalDirection` | `Both` | `Both` for a two-way street. On a one-way street, the way traffic is allowed to go: `L2R`, `R2L`, or one of the compass labels above (such as `NE`). Vehicles going the other way are still timed and logged, and are called out as wrong-way: the command line prints a `WRONG WAY` line, and VST Studio, the street report and the phone count them. In VST Studio this is **Your street** in step 4. |

## Calibration

VST doesn't measure distance directly. It counts the frames a car takes to cross the zone between the speed lines and
compares that with a calibration:

```
speed (mph) = CalibrationSpeed x CalibrationFrames x (video fps / CalibrationFPS) / frames the car took
```

| Key | Default | Meaning |
|---|---|---|
| `CalibrationFramesL2R`, `CalibrationFramesR2L` | 35, 40 | Frames a vehicle takes to cross the zone in each direction at `CalibrationSpeed`. The far lane usually takes more frames, because the same pixels cover more road there. |
| `CalibrationSpeed` | 25 | The speed, in mph, that those frame counts correspond to. It sets the scale of every speed. It is not the speed limit, which only colors the results. |
| `CalibrationFPS` | 30 | The frame rate those frame counts were counted at. Video at other frame rates is measured correctly by scaling. |

VST Studio's Calibrate step works these out from a drive past, cones or a pole, a car of known model, or a phone or
dashcam recording, and combines them (see [VST Studio](vst-studio.md#5-calibrate)). To calibrate by hand, see
[the command line guide](command-line.md#calibrating-by-hand).

## Short views

| Key | Default | Meaning |
|---|---|---|
| `subFrameTiming` | 0 | Set to 1 for a *short view*, where only about 14 to 25 ft of street is visible and a car fills most of the picture. Whole frames are too coarse there, so VST fits a straight line through the front bumper's positions and times the crossing to a fraction of a frame. A vehicle whose positions stray from the line gets no speed. The csv's frame columns stay whole-frame counts. |
| `maxL2RDistOnEntry`, `maxR2LDistOnEntry` | 75 | How far ahead, in pixels, the tracker looks for the front bumper of a car entering the box. It should be about how far a car moves in two frames at about 70 mph. With `subFrameTiming = 1`, the tracker's other built-in margins scale with it (they were tuned for 75). A 20 ft view at 30 fps wants about 440. |
| `entryLookBack` | 250 | How far back, in pixels, to look for the rear bumper of an entering car. About a car length in pixels. Too large lets the tracked area behind a car grow and causes needless dead reckoning of oncoming cars. |

VST Studio's site planner sets all four when it sees a short view (**Use short-view timing**). At 30 fps a 15 ft view
times cars up to about 45 mph and a 20 ft view up to about 60; at 60 fps both time cars over 70.

## Tracker tuning

These rarely need changing.

| Key | Default | Meaning |
|---|---|---|
| `nextHeight` | 85 | First guess, in pixels, at the height of a car entering the box, used until the tracker has measured it. Larger for a close view. |
| `SLOP` | 15 | Pixels of margin when deciding whether two opposite-direction vehicles overlap. |
| `SENSITIVITY_VALUE` | 30 | Threshold for the frame-to-frame difference that counts as motion. |
| `BLUR_SIZE` | 20 | Smoothing applied to the difference picture. |
| `largeVehicleArea` | 109 | Vehicle profile area (pixel area / 300) at or above which a vehicle is probably a bus or truck; used to pick large vehicles for a highlights video. The sample `VST.cfg` uses 100. VST Studio resets it to about 190 sq ft at the scale of each new calibration. |
| `minLoggedSpeed` | 18 | Vehicles timed slower than this, in mph (walkers, cyclists, cars turning in), get no csv row; they are counted in the run manifest instead. 0 logs every timed vehicle. VST Studio sets it 5 mph under the posted limit on slower streets (never above 18) unless you set it yourself. |

## Files

| Key | Default | Meaning |
|---|---|---|
| `dataPathPrefix` | blank | Folder that holds `IPCam` (input videos for interactive mode), `Stats`, `Trace` and `HiLites`. Blank means the per-user data folder, which is right for almost everyone. Set it to keep videos and results elsewhere, such as a large drive. Use plain single back-slashes on Windows; quotes are optional. Missing folders are created. |

## `ProcessHiLites.cfg`

The highlights editor, `ProcessHiLites`, has its own small config. Unlike `VST.cfg` it keeps the strict 2016 format:
exactly these three settings, in this order. Blank lines and trailing `# comments` are fine; comment-only lines are
not.

| Key | Meaning |
|---|---|
| `dataPathPrefix` | As in `VST.cfg`. Blank is the per-user data folder. |
| `CropLeft`, `CropTop` | Top-left corner of the 640 x 360 crop written for each vehicle you keep. |
