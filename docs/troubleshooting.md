# Troubleshooting

Something not working? It is almost always one of the things below, and every one of them has a way through. If
yours isn't here, [open an issue](https://github.com/joekiller/VSTStudio/issues/new) with what you tried and
what VST said; for a camera that misbehaves, **Report this camera** on VST Studio's Connect the camera step fills one
in for you.

## Setting up

| What you see | What to do |
|---|---|
| No camera yet | That's fine. **Watch the sample measure** on step 1 plays the bundled sample video, so you can see it work first. A phone in USB webcam mode, or a recording from a phone, works as a camera. |
| The view from your window seems too short | About 14 ft of clear street is enough with short-view timing; step 2's site planner says what your view can time and offers **Use short-view timing**. Record at 60 fps if your camera can. |
| The camera isn't mounted level, or the street runs at a slant | Step 4: **Level the picture** for a tilted camera, or drag the ends of the lane line to follow a slanted street. |
| No car handy to calibrate, or nobody to drive | Step 5: **Cones or pole** (one person, one pole, a tape measure) or **Known car** (any parked car of a model you know). A drive past later checks it. |
| Cruise control won't hold 15 or 20 mph | Drive at any steady speed and let a phone running VST Ride-along, or a Tesla's dashcam, record the speed. |
| The phone can't reach VST Studio | The phone has to be on the same Wi-Fi as the computer. On Windows, installing *for everyone* opens the firewall for it. On a computer with several networks, pick the address on the phone's network on the Companion page. |
| **Camera moved?** warning | The view shifted. Re-aim the camera and redraw the zones, or press **It's fine, use this view**. |
| A person on the near sidewalk showed up as a fast wrong-way car | In a close view a walker stands taller in the picture than the lane, and up to v2.0.0 a walker coming in at one edge as a car came in at the other could be logged as a wrong-way car at 18-45 mph. VST now only believes front-bumper sightings a vehicle could really have made, so this no longer happens, and cars passing a walker are timed more often. If one still gets through, run the clip with `--trace trace.txt` and [open an issue](https://github.com/joekiller/VSTStudio/issues/new) with the trace. |
| Speeds all look off by about the same factor | The calibration is off. Run **Check it with a second drive** on the Calibrate step; it lists the usual causes. |

## Cameras and video

| What you see | What to do |
|---|---|
| "Can't open video" | The file is missing or can't be decoded. Convert it: `ffmpeg -i in.ext -c:v libx264 out.mp4`. |
| A webcam delivers fewer frames than it claims, or looks dark | Many webcams slow down in dim light on automatic exposure. VST fixes the shutter where it can; on macOS lock exposure in the camera's own app. *Check what this camera can do* shows what it really sends. |
| "No Raspberry Pi camera found" | The ribbon cable isn't seated, is the wrong way round, or a Pi 5 has the older 15-pin cable. Reseat it with the power off; `rpicam-hello --list-cameras` should list the camera. |
| "Pi camera 0 is in use by another program" | Only one program can use a Pi camera. Close `rpicam-hello`, another VST, or a browser using it. |
| Odd speeds or a burst of passes after the computer slept (older versions) | The tracker counted frames across the sleep. VST Studio now keeps the computer awake while tracking live and reopens the camera after any gap. Leave those passes out in Results. |
| "This computer may still go to sleep on its own" | VST Studio couldn't hold off sleep (on Linux, a desktop without systemd). Turn automatic sleep off in the system's power settings while it watches the street. |
| A camera or stream keeps saying **Reconnecting** | The source stopped sending pictures. VST keeps retrying every 30 s; check the camera's power, network and URL. Measuring resumes when it is back. |
| Date, Time and Timestamp are blank | The recording's time can't be trusted. Rename it `manual_<yyyymmddhhmmss when it started>.<ext>` and run it again. |
| macOS says the app can't be opened | Only builds from a release are signed. For another build, run `xattr -dr com.apple.quarantine VSTStudio.app`. |

## Command line

| What you see | What to do |
|---|---|
| "unknown setting" or "can't read a value" | A typo in `VST.cfg`. The message gives the file and line number, and lists the known settings. |
| "No video folders found in ...IPCam" | Interactive mode looks for recordings in `IPCam/<a folder>/` under the data folder. Put a folder of videos there, accept the offer to run the sample, or pass a file with `--input`. |
| It says it is using a different `VST.cfg` from the one you edited | A `VST.cfg` in the folder you run from wins over the per-user copy. Each run prints `Using config <path>`; pass `--config` to be explicit. |

Every error is printed to stderr, and the exit status is 1 on any error, so scripts can check it.

## Coming from the 2016 VideoSpeedTracker

If you ran Paul Reynolds' original 2016 VideoSpeedTracker (a `VST.cfg` next to the program, data in a folder like
`g:\locustdata`), VST Studio finds it at launch and says what it found. **Pick up where I left off** copies your
settings to `VST-2016.cfg` and your results into Studio's data folder, and opens your latest recording where it is.
Nothing in the 2016 setup is changed, so the old program keeps working. If Studio doesn't find it (it's on another
drive, say), use **Find my 2016 setup** at the bottom of **What you need**. See
[VST Studio](vst-studio.md#coming-from-the-2016-videospeedtracker).

## Upgrading from an early release

| What you see | What to do |
|---|---|
| `dataPathPrefix = g:\locustdata` and "No video folders found" | v1.0.0 shipped the 2016 author's data path. Later releases leave it blank, meaning your per-user data folder; blank it in your copy too. |
| A csv from an old version has fewer columns | Nothing to do: VST Studio, archives and the street report read every version ([output files](output-files.md#the-stats-csv)). |

Problems reported against the original 2016 program (hangs at start, "Frame width is not 1280", crashes on H.264
files) were all fixed when the project was modernized; what happened to each is in
[history/upstream-issues.md](history/upstream-issues.md).
