# Getting started with VideoSpeedTracker

This guide takes you from a fresh download to measuring the traffic on your street. You don't need to know anything
about cameras or video first, and you don't need to buy anything to try it: VST Studio comes with a sample video, and
the camera you already own (even a phone) is a fine place to start. Release packages install this guide as
`GETTING-STARTED.md`.

The full documentation is at [github.com/joekiller/VideoSpeedTracker](https://github.com/joekiller/VideoSpeedTracker#documentation).

## 1. Install it

Download the file for your computer from the
[latest release](https://github.com/joekiller/VideoSpeedTracker/releases/latest):

- **Windows:** `VideoSpeedTracker-<version>-windows-x64-setup.exe`. Run it and choose whether to install just for you
  or for everyone (for everyone also lets your phone reach it through Windows Firewall).
- **Mac** (Apple silicon, macOS 13 or later): `VideoSpeedTracker-<version>-macos-arm64.dmg`. Open it and drag
  **VSTStudio.app** onto Applications.
- **Linux or Raspberry Pi:** the `.deb` for your system (`arm64` for a Pi), then
  `sudo apt install ./videospeedtracker_*.deb`.

[Installing](https://github.com/joekiller/VideoSpeedTracker/blob/main/docs/install.md) has every option and how to
check that a download is genuine.

## 2. Open VST Studio and watch it work

Open **VST Studio** from the Start menu, Applications, or your Linux applications menu. It opens on step 1 of setup.
To see it work before you do anything else:

1. Click **Connect the camera** (step 3) in the side rail and press **Use the sample**. That opens eight minutes of
   Locust Avenue in Charlottesville, recorded in 2016 by VST's original author.
2. Click **Live tracking** and press **Start tracking**.

Each car gets a box, a bar on its front bumper, and a speed as it crosses the second white line. When you're ready,
go back to step 1, **What you need**, and work through the steps with your own view.

## 3. Follow the five setup steps

The side rail lists five numbered steps, and the button at the top always names the next one. You can go back to any
step at any time; nothing is lost.

1. **What you need.** A window on the street, a camera that can be held still, a few minutes with the picture, and one
   drive past (or a tape measure). It also gathers the tips that make the biggest difference: lens against the glass,
   insect screen out, a dark cloth or hood against reflections.
2. **Pick your spot.** A site planner, for before you mount anything. Pick the preset closest to your view, or type
   how far the lane is and how much of it you can see, and it tells you how fast a car it can time and how accurate it
   will be. **Find it on the map** fills it in from OpenStreetMap, including whether your street is one-way. Nothing
   changes until you press **Save changes**. A narrow street from a front window is fine: about 14 ft of clear street
   is enough with short-view timing, which the planner offers when it sees one.
3. **Connect the camera.** A USB webcam, a phone in USB webcam mode, a network camera's address, a Raspberry Pi
   camera, or a recording. VST picks the camera's best mode for timing and says, in plain words, what it can and
   can't do. No camera yet? Keep using the sample, or a photo of your view to plan with.
4. **Mark your street.** Drag the box around the street, the two white speed lines, and the lane line onto the
   cars' hubcaps. Set the compass direction and, for a one-way street, which way is legal: VST then flags every
   wrong-way driver. A crooked camera or a street at a slant each have a one-click fix here.
5. **Calibrate.** VST needs to know how long the zone between the white lines is. The simplest way is to drive past
   at a steady speed and tell it how fast you went, then **Check it with a second drive**. No car or no helper?
   **Cones or pole** works with a tape measure and one person; **Known car** works from any parked car of a model
   you know; a phone running VST Ride-along can record your speed so you don't need cruise control. Every way you
   calibrate adds to the others rather than replacing them.

Not calibrated yet? Tracking runs anyway, and speeds firm up as you calibrate. [VST Studio](https://github.com/joekiller/VideoSpeedTracker/blob/main/docs/vst-studio.md)
describes every step in detail.

## 4. Leave it running

Press **Start tracking** and leave it. VST Studio reconnects a camera that drops out, keeps video within a disk budget
(20 GB by default), warns if the camera gets knocked out of line, and logs each day so you can tell a quiet street
from a camera that was offline. Reopen VST Studio and it picks up where you left off.

## 5. Pair your phone (optional)

Turn on **Companion app** in VST Studio's side rail and scan the QR code with your phone, on the same Wi-Fi. From the
phone you can watch the camera, see recent speeders and wrong-way drivers with clips, and do steps 2 to 5 from the
street. Phone busy being the camera? Pair a second phone or a tablet, or skip it: every step also works on the
computer.

## 6. Look at what you've found

**Results** shows every vehicle with filters for speeding, way over and wrong way, a speed chart with the limit
marked, and the 85th-percentile speed (the number traffic engineers use). After a week or two:

- **Make report** writes a street report that prints cleanly and is written for neighbors, city staff and council.
- **Archive** puts a period away under a name ("Before the new signs") so you can compare later.
- **Publish** freezes a dataset with checksums so anyone can check it, and it can be re-checked against later
  calibrations.

[Working with your city](https://github.com/joekiller/VideoSpeedTracker/blob/main/docs/working-with-your-city.md) is
about what to do next: who to talk to, what to bring, and what to ask for.

## Prefer the command line?

`VideoSpeedTracker --demo` runs the sample without VST Studio, and `VideoSpeedTracker --help` lists every option. It
reads the same `VST.cfg` VST Studio writes, so you can set up in Studio and run headless on a schedule.
[Command line](https://github.com/joekiller/VideoSpeedTracker/blob/main/docs/command-line.md) covers it, including
camera options and setting up by hand.

## If something goes wrong

[Troubleshooting](https://github.com/joekiller/VideoSpeedTracker/blob/main/docs/troubleshooting.md) covers the common
cases. If VST says something you don't understand, it is a bug in the wording as much as anything:
[tell us](https://github.com/joekiller/VideoSpeedTracker/issues/new).
