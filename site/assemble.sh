#!/bin/sh
# Assembles the GitHub Pages site into one folder, exactly as .github/workflows/pages.yml publishes it, so a local
# preview is the real thing:
#
#   site/assemble.sh /tmp/vst-site && python3 -m http.server -d /tmp/vst-site 8000   # then open localhost:8000
#
# Layout (the repository is served at https://joekiller.github.io/VSTStudio/):
#   /             site/            the splash page: what VST Studio is, quick start, downloads, the story
#   /companion/   companion/       the phone companion; on https it can't reach a VST on the home network, so here
#                                  it is the demo (companion/#demo opens straight into it)
#   /ride/        companion/ride/  VST Ride-along, the GPS recorder for calibration drives (needs https for GPS)
#   /img/         docs/images/     the screenshots the docs already use, so the site never keeps its own copies
#
# Ride-along used to be the whole site at /. site/index.html sends its old links (?target=…) and home-screen
# installs on to /ride/, and site/sw.js retires the old root service worker.
set -eu
out=${1:?usage: site/assemble.sh <output folder>}
root=$(cd "$(dirname "$0")/.." && pwd)

rm -rf "$out"
mkdir -p "$out/companion" "$out/ride" "$out/img"

cp "$root"/site/index.html "$root"/site/site.css "$root"/site/site.js "$root"/site/sw.js "$root"/site/404.html "$out/"
# The companion app itself: no dev server, tests, README or the Ride-along folder (that one is published at /ride/).
cp -R "$root"/companion/index.html "$root"/companion/manifest.webmanifest "$root"/companion/sw.js \
      "$root"/companion/css "$root"/companion/js "$root"/companion/icons "$root"/companion/demo "$out/companion/"
cp -R "$root"/companion/ride/. "$out/ride/"
cp -R "$root"/docs/images/studio "$root"/docs/images/companion "$out/img/"
# GitHub Pages runs Jekyll unless told not to; nothing here needs it.
touch "$out/.nojekyll"
