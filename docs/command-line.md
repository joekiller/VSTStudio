# The command-line tracker

`VideoSpeedTracker` is the tracker on its own, without VST Studio's windows: for scripts, scheduled jobs, batch runs
over a folder of recordings, and headless boxes such as a Raspberry Pi in a closet. It reads the same `VST.cfg` and
writes the same files VST Studio does, so you can set a view up in Studio and then run it from here, or the other way
round.

If you are setting up for the first time, use [VST Studio](vst-studio.md); it writes `VST.cfg` for you. Where to find
the programs after installing is in [Installing](install.md#macos-command-line-tools).

## Quick start

```
VideoSpeedTracker --demo              # track the bundled 2016 sample in a window; Esc quits
VideoSpeedTracker --demo --headless   # the same with no window, as fast as the computer allows
```

Both write a csv of every vehicle to the `Stats` folder in your data folder and print its path.

```
VideoSpeedTracker --headless --input clip.mp4 --stats out.csv --speed-limit 25 \
  --highlights hilites.mp4 --hl-min 35
```

| Option | What it does |
|---|---|
| `--demo` | Analyze the bundled sample video with the settings it was recorded with. |
| `--input SOURCE` | What to analyze: a video file, a stream URL (`rtsp://`, `http://`), a USB camera number (`0`, `1`, ...), or a Raspberry Pi camera (`picam`). |
| `--config PATH` | The `VST.cfg` to use. Default: `./VST.cfg`, else the per-user copy. |
| `--stats PATH` | Where to write the csv. Default: `stats_<input name>.csv` in the working directory. |
| `--trace PATH` | Also write a detailed debug trace of every track. |
| `--speed-limit N` | Posted limit, mph (default 25). Only colors the results; it never changes a measured speed. |
| `--egregious N` | The "way over" speed shown in red (default: limit + 10). |
| `--start-frame N` | Skip to this frame first. |
| `--highlights PATH` | Write a video (MPEG-4) of the vehicles that qualify, for review with `ProcessHiLites`. |
| `--hl-min N`, `--hl-max N` | Speed range for the highlights video (default 35 to 100). |
| `--hl-area N` | Also include vehicles at least this large (buses and trucks; default 100). |
| `--headless` | No windows; runs as fast as the computer allows. |
| `--list-pi-cameras` | List the Raspberry Pi cameras attached, then exit. |
| `--help` | List every option. `--opt=value` works as well as `--opt value`. |

The 8.6 minute sample processes in about 20 seconds headless. Every error goes to stderr and the exit status is 1, so
scripts and cron jobs can check `$?`.

### Interactive mode

Run `VideoSpeedTracker` with no options for the original 2016 workflow: it lists the folders under
`<data folder>/IPCam` (put each day's recordings in a folder of its own, such as `IPCam/20261102/`), then the videos
in the one you pick (type `*` for all of them), draws the analysis box and lines over the first frame for you to
check, and asks for the speed limit and whether to write a trace or highlights. Press Enter to take each default.
With no videos there yet, it says where to put them and offers the sample instead. On Windows you can double-click
`VideoSpeedTracker.exe`; the window stays open afterwards so you can read why it stopped.

While a window is showing: `p` pauses, `v` hides the motion picture and stops refreshing (faster), `f` and `s` make
the display faster or slower, and Esc stops.

## Sources

### Recordings

Any video OpenCV can decode works, at any size and frame rate: frames are scaled to 1280 x 720 (the grid `VST.cfg`
uses) and speeds are scaled by the frame rate. A video that won't open can be converted with
`ffmpeg -i in.ext -c:v libx264 out.mp4`. Video files and streams are decoded on the graphics chip when it has a
decoder (Intel Quick Sync, VA-API, D3D11), which takes most of the work off the processor; set `VST_HW_DECODE=0` to
turn that off.

Each vehicle's time comes from:

- **a camera or stream:** this computer's clock when the vehicle passed (keep the clock on network time);
- **a Foscam-style recording** named `manual_yyyymmddhhmmss.<ext>`: the start time in the name plus the vehicle's
  offset into the video, in this computer's time zone;
- **any other recording:** the file's modified time (when the camera finished writing it) minus the video's length,
  plus the offset. Copying a file resets its modified time, so check one vehicle against the clock in the picture. If
  the time can't be trusted, it is left blank; rename the file `manual_<start time>.<ext>` to supply it.

### Cameras and streams: running unattended

A camera (`--input 0`), Pi camera (`--input picam`) or stream (`--input rtsp://...`) runs until it is stopped (Esc,
Stop in VST Studio, or ending the process). If the picture drops out, VST doesn't end the job: it reopens the source
after 1, 2, 4, 8 and 16 s, then every 30 s for as long as it takes, and logs each attempt. Vehicles being tracked when
the picture stopped are dropped, so no speed is ever measured across a gap, and each gap is recorded in the run
manifest's `outages` ([Output files](output-files.md#the-run-manifest)). A gap without a failed read counts too: if
no frames arrive for 8 s or more (the computer slept, or the run was paused) and then they do, the first frames after it
may be stale ones buffered before it, so VST drops what it was tracking, records the gap as an outage and reopens the
source at once. The command line doesn't keep the computer awake (VST Studio does); for an unattended run turn sleep
off, or run it under `systemd-inhibit` on Linux or `caffeinate -i` on macOS. A URL that reports a length (an `http://`
link to a recorded file) is treated as a file and ends at its end.

### USB webcams and phones

A USB camera, or a phone in USB webcam mode, is opened in the best mode for timing that its driver lists. VST asks the
system for every size, frame rate and format the camera offers and prefers, in order: 60 fps or more at 720p or
better, then 60 fps at 480p, then 30 fps at HD; then 16:9; then the size nearest 720p; then MJPG over raw. No list of
camera models is involved, so a webcam VST has never heard of gets the same treatment. Where the modes can't be
listed, it asks for 1280 x 720 at 60 fps in MJPG and takes the nearest.

To choose a mode yourself, put it in the name: `--input "0?w=1920&h=1080&fps=30&fmt=MJPG&shutter=1000"`.

VST fixes the shutter at 1/1000 s where the camera allows it, so cars stay sharp and a webcam can't quietly slow to
30 fps in shade while still reporting 60 (speeds are counted in frames). If the picture is too dark it tries 1/500 and
1/250 s, then goes back to automatic. This works on Linux and Windows; macOS doesn't let VST set it, so lock exposure
in the camera's own app there. `shutter=0`, or `VST_CAMERA_SHUTTER_US=0`, leaves exposure automatic. The log says
what happened, and whether the camera sent fewer frames than it reported.

In VST Studio, step 3's camera menu lists cameras by name and **Settings** on the camera card shows every mode, VST's
pick and why. *Check what this camera can do* measures what the camera really sends, and *Report this camera* opens
a pre-filled GitHub issue (no pictures, no file paths, nothing sent unless you submit it).

### Raspberry Pi cameras

VST reads the Pi's own ribbon-cable cameras when it runs on that Pi (Raspberry Pi OS Bookworm or later; on a Lite
install, `sudo apt install rpicam-apps`). It runs `rpicam-vid` and reads its frames, so the same `.deb` works with or
without a camera.

| Camera | Use it for | At VST's defaults |
|---|---|---|
| **Global Shutter Camera** (IMX296) with a CS-mount lens | the best choice: no rolling-shutter lean on fast cars | 60 fps, full sensor width. Focus by hand on the lens, then lock its screw. |
| **Camera Module 3** (IMX708), also Wide and NoIR | cheaper, sharper in daylight | 60 fps; the 720p mode is a centre crop (about 47 degrees of view, 79 for Wide). Focus is locked (see `focus`). |
| **Camera Module v2** (IMX219), also NoIR | if you already have one | about 47 fps at 720p. |

NoIR models have no infrared filter: daytime colors look pink, which tracking doesn't mind, and at night they need an
infrared light. A Pi 5 needs the 22-pin to 15-pin camera cable that some cameras don't come with.

```
VideoSpeedTracker --list-pi-cameras
VideoSpeedTracker --headless --input picam --config VST.cfg
VideoSpeedTracker --headless --input 'picam:0?fps=30&shutter=2000&focus=0.2'   # quote it: ? and & mean something to the shell
```

| Setting | Default | |
|---|---|---|
| `fps` | 60, or the camera's best below that | If the camera delivers more than 10% fewer frames than asked, VST measures the real rate and uses it, and says so. |
| `shutter` | `1000` (1/1000 s) | Microseconds, fixed so cars don't smear; the camera raises its gain to make up the light. `0` lets the camera choose (blurrier cars, better at night). |
| `gain` | `0` (automatic) | Analogue gain, such as `4`, to fix the brightness too. |
| `focus` | `0.25` | Camera Module 3's lens position in dioptres (1 / metres to the lane), locked for the run. 0.25 (4 m, 13 ft) keeps about 2 m to beyond 30 m sharp. |
| `size` | `1280x720` | What `rpicam-vid` delivers. Width a multiple of 64, height of 16. |
| `rotate` | `0` | `180` for a camera mounted upside down. |

`picam:1` is a second camera. In VST Studio the Pi camera is first in step 3's camera menu, and the sliders button
next to it sets the same things.

## Setting up a view by hand

VST Studio's **Mark your street** does this by dragging on the picture. Without it:

1. Record a minute of ordinary traffic and save one frame at 1280 x 720, for example
   `ffmpeg -i clip.mp4 -frames:v 1 -s 1280x720 frame.png`. Open it in an image viewer that shows pixel coordinates.
2. Set the analysis box, speed lines, lane lines and obstruction in `VST.cfg`
   ([where things are in the picture](config-reference.md#where-things-are-in-the-picture)). Speed lines are relative to
   the box's left edge and lane lines to its top.
3. Run the clip interactively (or with `--input clip.mp4` and no `--headless`). VST draws everything over the first
   frame; adjust and run again until it sits right.

What makes a good view: the street crossing the picture left to right, about halfway up; the speed lines in the
flattest middle part of the picture; and a zone a car at the limit takes about a second to cross (about 37 ft at
25 mph). A short view of 14 to 25 ft works too, with `subFrameTiming = 1`
([short views](config-reference.md#short-views)). Record at 60 fps with a 1/1000 s shutter if you can.

### Example: a narrow one-way street from a front window

A second-floor window over a one-way street that runs northeast, so legal traffic moves right to left across the
picture and left-to-right vehicles are going the wrong way. These numbers show the shape of such a config; measure
your own.

```
L2RDirection = "SW"
R2LDirection = "NE"
LegalDirection = NE          # one-way: L2R rows are wrong-way callouts
obstruction = [0,0]
AnalysisBoxLeft = 0
AnalysisBoxTop = 300         # below the porch roof and the yards across the street
AnalysisBoxWidth = 1280
AnalysisBoxHeight = 220
speedLineLeft = 200
speedLineRight = 1080
R2LStreetY = 150             # one lane, so both lines are the same
L2RStreetY = 150
nextHeight = 110             # cars look bigger close up
subFrameTiming = 1           # a 20 ft view is a short view
maxL2RDistOnEntry = 440      # how far a car moves in two frames at ~70 mph here
maxR2LDistOnEntry = 440
entryLookBack = 990          # about a car length in pixels (15.5 ft of a 20 ft view)
CalibrationFramesR2L = 11    # from your own calibration drive (about 14 ft at 25 mph, 30 fps)
CalibrationFramesL2R = 11    # the same lane, so the same value
CalibrationSpeed = 25
CalibrationFPS = 30
```

## Calibrating by hand

VST Studio's Calibrate step does all of this, more accurately, and keeps a record. By hand:

1. Drive (or have someone drive) through the zone at a steady, known speed, using a GPS speed app rather than the
   speedometer if you can. Any steady speed works; on a one-way street only the legal direction is needed.
2. Run VST on that recording: `VideoSpeedTracker --headless --input calib.mp4 --stats calib.csv`. The `# Frames`
   column of your car's row is how many frames it took to cross the zone.
3. Put that count in `CalibrationFramesL2R` or `CalibrationFramesR2L`, the speed you drove in `CalibrationSpeed`, and
   the recording's frame rate in `CalibrationFPS`. A count from a pass at another speed can be converted:
   frames at `CalibrationSpeed` = frames counted x speed driven / `CalibrationSpeed`.
4. Drive past again at a different speed and check VST reports it within a mph or two. Keep a note of both passes
   (speed driven, frames, what VST reported): it is how you show someone the calibration is right.

## The highlights editor, `ProcessHiLites`

`ProcessHiLites` steps through a highlights video one vehicle at a time so you can check each track by eye before
anything is shown to anyone. For each vehicle: **R**eplay, **S**low replay, **D**elete, **K**eep or **Q**uit. Kept
vehicles are cropped to 640 x 360 at (`CropLeft`, `CropTop`) from `ProcessHiLites.cfg` and written to
`forPosting/forPost_<name>` beside the input video. It needs a display.

```
ProcessHiLites                                    # offers each video in <data folder>/HiLites
ProcessHiLites --config ProcessHiLites.cfg --input hilites.mp4 [--output keepers.mp4]
```

## Environment variables

| Variable | Effect |
|---|---|
| `VST_HW_DECODE=0` | Decode video in software only. |
| `VST_CAMERA_SHUTTER_US` | USB camera shutter in microseconds, or `0` for automatic. |
| `VST_RPICAM_VID` | Program to run instead of `rpicam-vid` (the tests use it to stand a recording in for a camera). |
