# Building, testing and releasing

For contributors and anyone who wants to build VST Studio themselves. To *use* it, download a release
instead ([Installing](install.md)).

## What's where

| Path | What it is |
|---|---|
| `src/Wintel64/VideoSpeedTracker/` | The tracker: `videoSpeedTracker.cpp` (command line, setup, the frame loop and outputs), `VehicleDynamics` (the predictive tracker and speed), `Globals` (`VST.cfg` and its parser), `FrameInput.h` (every video source), `Perspective.h`, `Level.h`. The folder name is from 2016; it builds on every platform. |
| `src/Wintel64/ProcessHiLites/` | The highlights editor. |
| `src/Wintel64/Common/` | Per-user config and data folders, shared by both programs. |
| `src/Gui/` | VST Studio (Dear ImGui + GLFW) and its local API for the phone. |
| `companion/` | The phone companion (static HTML, CSS and JavaScript, no build step) and VST Ride-along (`companion/ride/`). See [companion/README.md](../companion/README.md). |
| `bin/` | The `VST.cfg` and `ProcessHiLites.cfg` templates every package installs. |
| `sampleData/` | The 2016 Locust Avenue sample video. |
| `tests/` | ctest cases and their baselines. |
| `packaging/` | OpenCV and FFmpeg build scripts, the Windows installer, the macOS app bundle. |
| `docs/` | This documentation. `docs/history/` keeps the original 2016 materials. |

## Building

You need CMake 3.16 or newer, a C++17 compiler and OpenCV 4 or 5. The quickest start is the distro's OpenCV:

```
sudo apt-get install -y libopencv-dev cmake g++ \
  libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev   # the last line is for VST Studio
cmake -S . -B build -DVST_WERROR=ON && cmake --build build -j
ctest --test-dir build
```

CMake fetches VST Studio's libraries at pinned versions and links them statically (GLFW, Dear ImGui,
portable-file-dialogs, cpp-httplib, nlohmann/json, Nayuki's QR Code generator, the Inter and Font Awesome fonts).
`-DVST_BUILD_GUI=OFF` builds only the command-line programs. `-DVST_WERROR=ON` turns compiler warnings in VST's own
sources into errors, as the pull-request check does.

To build what the release packages use instead:

- **Linux:** `packaging/build-opencv.sh /opt/opencv` builds a static OpenCV 5 (it needs the FFmpeg and GTK 3 `-dev`
  packages listed in `.github/workflows/release.yml`); then add `-DCMAKE_PREFIX_PATH=/opt/opencv` to the first
  `cmake` line. `OPENCV_VERSION=<tag>` builds another OpenCV release.
- **macOS:** `brew install opencv cmake`, then `cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix opencv)"`.
- **Windows** (Visual Studio 2022 and Git Bash): `packaging/build-opencv.sh C:/opencv`, then
  `cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=C:/opencv -DOpenCV_STATIC=ON` and
  `cmake --build build --config Release`. Put the `opencv_videoio_ffmpeg*_64.dll` from under `C:\opencv` next to the
  exes so videos can be decoded, and run the tests with `ctest --test-dir build -C Release`.

## Tests

`ctest` runs the tracker on the bundled sample and compares it with `tests/baseline_*.csv` (same vehicles and
directions, speeds within 1 mph), and covers the config parser, first-run folders, short and receding views,
levelling, every calibration method, calibration sets, the street report, archives and published datasets, the
camera and stream inputs, VST Studio's companion API end to end, and two portability guards:

- `portability_windows_macros` compiles every source again, syntax only, with the macros `windows.h` defines
  (`cmake/WindowsMacros.h`). Never name anything `near`, `far`, `small`, `ERROR`, `IN`, `OUT`, `interface` and so on.
- `std_headers_included` checks each file includes the standard headers it uses.

The phone companion has its own tests: `node --test companion/test/*.test.mjs` (Node 20 or newer, no `npm install`).
`node companion/dev/mock-vst.mjs` serves the companion against simulated traffic with no camera.

For a box left running for days, `soak_test` (Linux, built on request: `cmake --build build --target soak_test`) runs
the tracker the way VST Studio does and prints its memory every 10 s, so growth over a long run shows up in minutes:
`build/src/Gui/soak_test <url> bin/VST.cfg <data dir> <seconds> live` on a looped live stream, or
`... <video> ... runs` on a video tracked again and again. `tests/soak_test.cpp` says more.

`VSTStudio --shots DIR` drives every page on the sample video and saves a screenshot of each; CI runs it and uploads
the images, which is where the screenshots in these docs come from.

## Before you push

Turn on the repository's pre-commit hook once per clone:

```
git config core.hooksPath .githooks
```

It looks at what is staged: companion changes run the companion tests, and changes under `src/`, `tests/`, `bin/`,
`cmake/`, `sampleData/` or `CMakeLists.txt` rebuild `build/` (or `$VST_BUILD_DIR`) and run `ctest`, leaving the two
slow wall-clock tests to CI unless `VST_HOOK_FULL=1`. `git commit --no-verify` skips it.

## Continuous integration

Each workflow runs only when a change touches files it builds from, to keep CI costs down.

| Workflow | Runs on | What it does |
|---|---|---|
| `build.yml` | pull requests and `main` | Linux build with warnings as errors, all tests, VST Studio screenshots, and a compile against OpenCV 5 with deprecations as errors (`opencv5-headers`). |
| `windows.yml`, `macos.yml` | pull requests (unsigned), and called by `release.yml` | The same Windows and macOS builds, tests and packages the release ships. |
| `release.yml` | `main` and version tags | Every platform, including the eight Linux `.deb` packages, attested; signed and published on tags only. |
| `cut-release.yml` | by hand | Tags `main` from the GitHub web page (see below). |
| `pages.yml` | `main`, when `site/`, `companion/` or `docs/images/` change | Publishes the [home page](https://joekiller.github.io/VSTStudio/) (`site/`) on GitHub Pages, with the companion demo at `/companion/` and VST Ride-along at `/ride/`. Preview it with `site/assemble.sh`. |

Wait for the Windows and macOS checks before calling a change to their code done: code under `#ifdef _WIN32`,
`packaging/windows`, `.mm` files and the app bundle are only built there.

## Releasing

A release is made by pushing a version tag, and nothing else:

```
git fetch origin && git tag v1.11.0 origin/main && git push origin v1.11.0
```

`release.yml` then builds and tests every platform, signs the Windows and macOS programs, attests every file, uploads
them with `SHA256SUMS` to a draft release, and publishes it last. From the GitHub web page, **Actions > cut-release >
Run workflow** with the version does the same.

**Never create or publish the release in GitHub's web interface yourself.** Releases are immutable: one published
before its packages are uploaded is locked empty for good, and its version number can't be used again. Signing setup
(Azure Artifact Signing for Windows, an Apple Developer ID for macOS) is in [code-signing.md](code-signing.md).
