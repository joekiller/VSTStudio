// A walker merged with a car coming the other way must never be timed as a fast vehicle (VehicleDynamics, 2026-11-07).
//
// Joe's report from 7½ St SW: in a close view a person on the near sidewalk covers the travel lane in the picture, and
// when they come in at one edge as a car comes in at the other, VST attributed a rapid wrong-way speed to "the car".  It
// was the walker's track.  The car's blob reached it, and the walker's front bumper either jumped onto the car's blob or
// was taken as hidden by the oncoming car and filled in with the last projection, which early in a track is mostly entry
// look-ahead (maxDistOnEntry, 417 px in a 20 ft view).  The fit steepened, the projection ran further ahead, every true
// sighting after it read as backward motion and was replaced by the projection, and the projection alone carried the
// track over both speed lines.  VehicleDynamics::leapsAhead() and hiddenFrontBumper() now stop both.
//
// This drives VehicleDynamics the way manageMovers() (videoSpeedTracker.cpp) does for one rightbound track, without
// video: each frame pair it asks for the projection, cuts the search window from it (the projected box, margin(80)
// behind and margin(100) ahead, as findContours() sees it), keeps every blob that overlaps the coalesce range for the
// track's state (coalesce(), greedy), and hands back their union as the sighting.  The blobs are a walker on the
// near sidewalk coming in at the left edge and a car coming in at the right edge going left, each spanning its place in
// both frames of the pair as frame differencing does.  The car isn't tracked here, so the walker's track is never told its
// front is hidden by it (doesL2ROverlapAnyR2L()); the window cut, the greedy coalesce and the backward-motion rule are
// enough to make the projection run away.  tests/pedestrian.cmake covers the hidden-front path with real video.
//
// Swept over walker speed (2-4 mph) and width, car speed (10-35 mph) and the frame the car comes in, in the 2016 view
// (whole frames) and a 20 ft short view (subFrameTiming): 1116 runs each.  Before the fix the short view timed the walker
// at 33-46 mph in 10 runs; now no run times it faster than a walker could go.  A car alone (15, 25 and 35 mph) is checked
// too, so a fix can't pass by timing nothing.
#include "Globals.h"
#include "VehicleDynamics.h"
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace {

// Something moving sideways: its left side is at x0 + v * (f - f0) in frame f.
struct Mover { double x0, v; int width, f0, top, height; };

// The blob frame differencing gives m over the pair (f, f + 1), clipped to the analysis box; none if it isn't there.
std::optional<cv::Rect> blob(const Mover& m, int f, const Globals& g){
	if (f < m.f0) return std::nullopt;
	double a = m.x0 + m.v * (f - m.f0), b = m.x0 + m.v * (f + 1 - m.f0);
	int left = std::max(int(std::min(a, b)), g.pixelLeft), right = std::min(int(std::max(a, b)) + m.width, g.pixelRight);
	if (right - left < 5) return std::nullopt;
	return cv::Rect(left, m.top, right - left, m.height);
}

// The 2016 Locust Avenue view (Globals' defaults, bin/VST.cfg) or the 20 ft short view of tests/short_view.cmake.
Globals view(bool shortView){
	Globals g;
	if (shortView){
		g.AnalysisBoxWidth = 1279; g.pixelRight = 1279; g.speedLineLeft = 128; g.speedLineRight = 1151;
		g.maxL2RDistOnEntry = g.maxR2LDistOnEntry = 417; g.entryLookBack = 1000; g.SLOP = 83; g.nextHeight = 278;
		g.CalibrationSpeed = 2.5; g.CalibrationFramesL2R = g.CalibrationFramesR2L = 131; g.subFrameTiming = 1;
	}
	g.obstruction[0] = g.obstruction[1] = 0;
	g.videoFPS = 30;
	return g;
}

// px a frame at 1 mph along the zone, from the calibration.
double pxPerMph(const Globals& g){ return (g.speedLineRight - g.speedLineLeft) / (g.CalibrationSpeed * g.CalibrationFramesL2R); }

// Follows one rightbound track, started on the first blob of `first` seen at the left edge, through the movers'
// blobs, and returns the speed it was timed at (-1 = never timed).
int follow(Globals& g, const Mover& first, const Mover* other){
	VehicleDynamics v(L2R);
	int f = 2;
	for (; f < 400; f += 2) if (auto r = blob(first, f, g)) { v.addSnapshot(Snapshot(*r, f)); break; }
	int speed = -1;
	for (f += 2; f < 700; f += 2){
		Projection p = v.getBestProjection(g, f);
		if (v.getAmIOK() != ImOK || p.getVState() == exited) break;
		const cv::Rect box = p.getBox();
		const int front = box.x + box.width;
		// The search window findContours() is given, and the coalesce() range for the track's state.
		const int lo = std::max(box.x - g.margin(80, true), g.pixelLeft), hi = std::min(front + g.margin(100, true), g.pixelRight);
		int from, to;
		if (p.getVState() == entering){ from = std::max(g.pixelLeft, box.x - g.margin(10, true)); to = front + g.margin(20, true); }
		else if (p.getVState() == exiting){ from = box.x - g.margin(10, true); to = std::min(front + g.margin(10, true), g.pixelRight); }
		else { from = std::max(box.x - g.margin(50, true), g.pixelLeft); to = std::min(front + g.margin(10, true), g.pixelRight); }
		std::optional<cv::Rect> seen;
		for (const Mover* m : { &first, other }){
			if (!m) continue;
			auto r = blob(*m, f, g);
			if (!r) continue;
			cv::Rect c = *r & cv::Rect(lo, 0, hi - lo, 1000);
			if (c.empty() || c.x > to || c.x + c.width < from) continue;
			seen = seen ? (*seen | c) : c;
		}
		if (seen) v.addSnapshot(Snapshot(*seen, f));
		if (v.getTrackEndPixel() > 0) speed = v.getFinalSpeed();
	}
	return speed;
}

}  // namespace

int main(){
	int failures = 0;
	for (bool shortView : { false, true }){
		Globals g = view(shortView);
		const char* name = shortView ? "20 ft short view" : "2016 view";
		const double k = pxPerMph(g);
		const int carLen = int(15 * k * g.videoFPS / (5280.0 / 3600.0));  // 15 ft: px a frame at 1 mph, over ft a frame at 1 mph
		// A car alone going right, so a fix that timed nothing would fail here.
		for (double mph : { 15.0, 25.0, 35.0 }){
			Mover car{ -double(carLen), mph * k, carLen, 0, 10, 120 };
			int got = follow(g, car, nullptr);
			if (got < mph - 3 || got > mph + 3){
				std::printf("FAIL %s: a %.0f mph car alone was timed at %d\n", name, mph, got);
				failures++;
			}
		}
		// The walker and the car coming the other way.
		int runs = 0, fast = 0;
		for (double walkMph : { 2.0, 3.0, 4.0 }) for (int width : { 40, 80, 150 }) for (double carMph : { 10.0, 15.0, 25.0, 35.0 })
			for (int carF0 = 0; carF0 <= 60; carF0 += 2){
				Mover walker{ -double(width), walkMph * k, width, 0, 20, 140 };
				Mover car{ double(g.pixelRight), -carMph * k, carLen, carF0, 10, 120 };
				int got = follow(g, walker, &car);
				runs++;
				if (got > 2 * walkMph + 2){  // faster than any walker: the car's motion taken for the walker's
					if (fast < 10) std::printf("FAIL %s: %.0f mph walker %d px wide, %.0f mph car in at frame %d: timed at %d mph\n",
						name, walkMph, width, carMph, carF0, got);
					fast++;
				}
			}
		std::printf("%s: %d of %d walker + oncoming car runs timed faster than a walker\n", name, fast, runs);
		failures += fast;
	}
	if (failures) return 1;
	std::printf("no walker merged with an oncoming car was timed as a vehicle\n");
	return 0;
}
