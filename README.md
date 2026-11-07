# VST Studio

> VST Studio is built on [VideoSpeedTracker](https://github.com/pfr/VideoSpeedTracker), written in 2016 by
> [Paul Reynolds](https://www.cs.virginia.edu/~pfr) to measure speeding on Locust Avenue in Charlottesville. Until
> v2.0.0 this project was called VideoSpeedTracker too; [see below](#built-on-videospeedtracker).

**Measure the traffic on your own street with a camera in your window.** VST Studio (VST) times every car that
passes, calls out speeders and wrong-way drivers on one-way streets, and turns weeks of measurements into a report
your neighbors, city staff and council can read. It is free, open source, and runs on Windows, macOS, Linux and the
Raspberry Pi.

![VST Studio measuring cars live](docs/images/studio/05-live-tracking.jpg)

- **Any camera to start.** A webcam, a phone in USB webcam mode, a network camera, a Raspberry Pi camera, or a
  recording. VST tells you what your camera can do and what would do better, and never sells you anything.
- **Any window.** A long view of a two-way street, or as little as about 14 ft of a narrow one-way street seen from a
  front window, even at an angle.
- **Guided from start to finish.** **VST Studio**, the desktop app, walks you through five steps: what you need, pick
  your spot, connect the camera, mark your street, calibrate. A phone app lets you aim and calibrate from the curb.
- **Numbers you can stand behind.** Calibrate by driving past, with cones or a pole, or from a parked car; check it
  with a second drive; and publish a dataset with checksums that anyone can re-check later.
- **Respects privacy.** VST keeps each vehicle's speed, direction and time, with a snapshot and short clip for you
  to review. It doesn't read plates or recognize faces, and everything stays on your computer unless you choose to
  share it.

## Get started

1. **Download** the package for your computer from the
   [latest release](https://github.com/joekiller/VSTStudio/releases/latest) and install it
   ([which file?](docs/install.md)).
2. **Open VST Studio.** No camera yet? **Watch the sample measure** on the first page plays a 2016 recording of a real
   street, so you can watch cars get measured first.
3. **Follow the five steps** in the side rail. [Getting started](docs/getting-started.md) goes through them.

## How it measures speed

VST compares each video frame with the one before to find what's moving, follows each vehicle's front bumper, and
counts the frames it takes to cross a zone marked by two lines on the picture. Calibration tells VST how long that
zone is in feet, so frames become miles per hour. On a narrow street, where a car crosses in a fraction of a second,
it fits a line through the bumper's positions to time the crossing to a fraction of a frame. Every report states how
accurate its speeds are and how the calibration was checked.

## Documentation

**Using VST**

- [Getting started](docs/getting-started.md): install, the five setup steps, and leaving it running.
- [VST Studio](docs/vst-studio.md): every page of the desktop app in detail.
- [Phone companion](companion/README.md): live view, recent passes, setup and calibration from the street, sharing.
- [Working with your city](docs/working-with-your-city.md): how to bring your data to neighbors, city staff and
  council, and what to ask for.
- [Troubleshooting](docs/troubleshooting.md).

**Reference**

- [Installing](docs/install.md): downloads, checking a download is genuine, where settings and data live.
- [Command line](docs/command-line.md): `VideoSpeedTracker` for scripts, batch runs and headless boxes, camera
  options, setting up and calibrating by hand.
- [Configuration reference](docs/config-reference.md): every `VST.cfg` setting.
- [Output files](docs/output-files.md): the stats csv, run manifests and logs.
- [Published datasets](docs/dataset-schema.md), [speed trend](docs/speed-trend.md), and the
  [companion API](docs/companion-api.md).

**Contributing**

- [Building, testing and releasing](docs/building.md), and [code signing](docs/code-signing.md).

## Built on VideoSpeedTracker

VideoSpeedTracker was created in 2016 by [Paul Reynolds](https://www.cs.virginia.edu/~pfr), a retired University of
Virginia computer science professor, to measure speeding on Locust Avenue in Charlottesville, Virginia. He shared his
data with the city and council, measured the city's own traffic-calming pilot, and published the software so any
neighborhood could do the same. VST Studio began as a fork of his
[pfr/VideoSpeedTracker](https://github.com/pfr/VideoSpeedTracker). It keeps his tracker and his calibration method at
its heart, still as the `VideoSpeedTracker` program, and builds on them for narrower streets, more cameras and people
with no technical background. It was renamed VST Studio at v2.0.0; v1 releases and their downloads keep the old name.
[Where VideoSpeedTracker came from](docs/history/README.md) tells the story, what it teaches about working with your
city, and keeps the original 2016 README, manual and council briefing.

## License

BSD 3-Clause ([LICENSE](LICENSE)). Copyright Paul Reynolds, 2016; later contributions copyright Joe Lawson, 2026, under
the same license.
