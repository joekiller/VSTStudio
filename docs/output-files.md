# Output files

Everything VST Studio records is a plain file you can open, copy, back up or analyze yourself. This page is
for the curious and for anyone loading the numbers into a spreadsheet, R or Python. Where the folders are on your
computer is in [Installing](install.md#where-settings-and-data-live).

## The data folder

| Folder or file | What's in it |
|---|---|
| `Stats/stats_<name>.csv` | One row per vehicle measured ([below](#the-stats-csv)). |
| `Stats/run_<name>.json` | How that csv was made: source, period, outages, calibration ([below](#the-run-manifest)). |
| `Stats/health.csv` | One row per day a live camera ran ([below](#the-daily-health-log)). |
| `Stats/exclusions.json` | Passes you reviewed by hand: left out (not a vehicle, a person merged with a car, counted twice, a bad reading), put back, or deleted for good. Each by its csv and frame, with why and when ([VST Studio](vst-studio.md#results)). |
| `Stats/calibration-sets.json` | Which runs share a camera view and calibration, so past speeds can follow a recalibration ([VST Studio](vst-studio.md#results)). |
| `Clips/` | A short clip of each vehicle (VST Studio). Pruned oldest first to the disk budget. |
| `HiLites/` | Highlights videos, if enabled; `HiLites/forPosting/` holds what you kept in `ProcessHiLites`. |
| `Reports/` | Street reports, one HTML file each. |
| `Archive/<date>_<name>/` | Datasets you put away with **Archive** in Results. |
| `Published/<date>_<name>/` | Datasets frozen with **Publish**, for others to check ([dataset-schema.md](dataset-schema.md)). |
| `Sample/` | The bundled sample's own `Stats`, `HiLites` and `Reports`, kept apart so the sample never appears in your saved runs, your phone or your street report. |
| `IPCam/` | Recordings for the command line's interactive mode, one folder per day. |
| `Trace/` | Debug traces, when asked for. |

Stats csvs, run manifests, reports and archives are never pruned. Only clips and highlight videos are, oldest first,
to stay under **Keep up to** (20 GB by default), and more if the disk falls under 2 GB free.

## The settings folder

| File | What's in it |
|---|---|
| `VST.cfg` | The camera view and calibration ([configuration reference](config-reference.md)). The first save of a session keeps the previous file as `VST.cfg.bak`. |
| `vst-studio.ini` | VST Studio's own preferences: speed limit, site plan, last source and page, recent sources, phone pairing code, parking lane. |
| `VST-2016.cfg` | Studio's copy of a 2016 `VST.cfg`, made when you pick up a setup from the original program ([VST Studio](vst-studio.md#coming-from-the-2016-videospeedtracker)). |
| `calibration-history.csv` | Every calibration and check, appended and never rewritten: the record you can show someone ([VST Studio](vst-studio.md#calibration-history)). |
| `calibration-inputs.json` | The measurements the calibration is combined from (drives, cones, a known car...). |
| `calibration-passes.csv` | Which vehicles were your own calibration and check drives, so they are left out of the counts. |
| `view-reference.jpg` | The reference picture VST compares against to warn if the camera moves. |

## The stats csv

Each vehicle VST measures is one row of `stats_<name>.csv`:

```
Date, Time, Frame, Direction, StartFrame, EndFrame, # Frames, StartPix, EndPix, DeltaPix, VehicleArea, , estSpeed, Flag, Timestamp, EntryMph, ExitMph, AccelMphPerSec, Trend, MphIn20ft, LengthPx, ZoneSeconds
20160130, 172251, 348, NW, 290, 320, 30, 878, 317, 561, 63, , 33, , 2016-01-30T17:23:02-05:00, 34, 32, -2.0, decelerating, 31, 182, 0.9871
```

| Column | Meaning |
|---|---|
| Date, Time | `yyyymmdd`, `hhmmss`. For a `manual_yyyymmddhhmmss` recording, when the recording started (the same on every row, as in 2016). For every other source, when this vehicle passed. Blank when unknown. |
| Frame | Frame number when the vehicle was logged, just after it left the zone. |
| Direction | `L2RDirection` or `R2LDirection` from `VST.cfg`. |
| StartFrame, EndFrame, # Frames | Frames at which the front bumper crossed the two speed lines, and the difference. |
| StartPix, EndPix, DeltaPix | Front bumper x at the two speed lines, and the distance between them, in analysis-box pixels. |
| VehicleArea | Profile area / 300. At `largeVehicleArea` and above it is probably a bus or truck. |
| *(blank)* | Always empty; kept so the 2016 column positions don't move. |
| estSpeed | Estimated speed, mph. |
| Flag | `*****` when the speed is implausible (more than 20 mph over the "way over" speed), usually two vehicles merged into one. Check it by eye. Otherwise empty. |
| Timestamp | When the vehicle passed: ISO 8601 local time with its UTC offset, such as `2026-10-30T14:03:22-04:00`. Empty when unknown ([where times come from](command-line.md#recordings)). |
| EntryMph, ExitMph | Speed at the first and second speed line; they average to estSpeed. |
| AccelMphPerSec | Acceleration through the zone, mph per second, positive when speeding up. |
| Trend | `decelerating` (-2 or less), `steady`, `accelerating` (+2 to +6) or `rapid` (+6 or more). |
| MphIn20ft | The speed 20 ft past the second line at that acceleration (0 if it would stop). |
| LengthPx | The vehicle's side-on length in analysis-box pixels; VST Studio shows it in feet. |
| ZoneSeconds | Seconds the front bumper took between the speed lines, to 0.1 ms. estSpeed is the zone length over this, so a car can be re-timed exactly when the same view is calibrated again. |

The five trend columns are empty when VST couldn't tell (too few or too ragged sightings); how they are measured is
in [speed-trend.md](speed-trend.md).

Only vehicles timed at `minLoggedSpeed` or faster get a row; slower ones are counted in the run manifest. Older csvs
are shorter (no Flag column when it was empty, no Timestamp in the earliest versions, nothing after Timestamp before v1.10). VST
Studio and the street report read every version, and spreadsheets that read columns by position are unaffected, since
the first 15 columns have never changed.

## The run manifest

Beside each `stats_<name>.csv`, VST writes `run_<name>.json`, a record of how the csv was made, so a report built on
it can say what was measured, over which period, and with which calibration. (With `--stats PATH` the manifest goes in
the same folder, with `stats_` swapped for `run_`, or `run_` put in front.)

```json
{
  "format": 1,
  "vst": "1.2.0",
  "stats": "stats_manual_20160130172251.csv",
  "source": { "input": "manual_20160130172251.avi", "kind": "file" },
  "started": "2016-01-30T17:22:51-05:00",
  "ended": "2016-01-30T17:31:26-05:00",
  "complete": true,
  "frames": 15458,
  "fps": 30,
  "outages": [],
  "config": {
    "CalibrationSpeed": 25, "CalibrationFPS": 30,
    "CalibrationFramesL2R": 35, "CalibrationFramesR2L": 40,
    "LegalDirection": "Both", "L2RDirection": "SE", "R2LDirection": "NW",
    "speedLineLeft": 350, "speedLineRight": 909,
    "minLoggedSpeed": 18, "subFrameTiming": 0,
    "levelDegrees": 0, "streetEdge": [0, 0]
  },
  "counts": { "logged": 61, "belowMinSpeed": 0, "untimed": 5 }
}
```

| Field | Meaning |
|---|---|
| source | `input` is the file name (a whole folder for an interactive `*` run), the camera number, the Pi camera name, or the stream URL with any password removed. `kind` is `file`, `camera` or `stream`. |
| started, ended | When the first frame and the end of the last frame analyzed were taken, by the same rules as Timestamp. `null` when unknown. |
| complete | `false` while a camera or stream run is still going. |
| frames, fps | Frames analyzed, and the video's frame rate. |
| outages | For a camera or stream, each gap in the picture: `from` the last frame before it, `to` when frames came back. |
| config | The calibration, directions, speed lines and timing settings every speed in the csv rests on. |
| counts | `logged`: rows in the csv. `belowMinSpeed`: timed, but slower than `minLoggedSpeed`. `untimed`: tracked into the zone but never given a usable speed (usually lost there, or hidden by oncoming traffic). |

`logged + belowMinSpeed` is every vehicle that was timed. The file is replaced whole each time it is written: at the
end of each video and of the run, and for a camera or stream also when the first frames arrive, after each reconnect,
and every 10 minutes, so a crash loses at most 10 minutes of it.

## The daily health log

`Stats/health.csv` gets one row per day a live source ran in VST Studio:

```
Date, TrackedHours, OutageHours, Outages, Vehicles, ClipsRemoved, FreeGB, Camera
```

It tells a quiet street apart from a camera that was offline. `Camera` is the worst view check that day: `moved`,
`unclear`, `same` or `unchecked`. The row is written when the day ends or VST Studio closes, so a restart adds a
second row for the same date.
