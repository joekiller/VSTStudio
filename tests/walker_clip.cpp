// Synthetic clips for tests/pedestrian.cmake: a 20 ft view of a one-way street (the 7½ St SW setup, legal way right to
// left) with a walker on the near sidewalk and a car, written as an MJPG .avi.  OpenCV carries its own MJPEG encoder and AVI
// muxer, so every build can write and read it back with no codec or ffmpeg, on Windows and macOS too.
//
//   walker_clip <out.avi> <scene>
//   walker_clip <out.avi> custom <walkerL2R 0|1> <walker f0> <carL2R 0|1> <car f0> <car mph> <walker width px> <walker mph>
//
// Geometry (1280x720, 30 fps; the config tests/pedestrian.cmake writes matches it):
//   - The lane is seen through 20 ft of frontage at 64 px/ft.  A car is 15 ft (960 px) long and stands from y 250 to 560.
//   - A walker on the near sidewalk is nearer the camera than the lane, so they stand taller and lower in the picture
//     (y 230 to the bottom), 80 px wide, at 3 mph (about 9 px a frame).  Their head and body rise well above the hubcap
//     line, which is how a person on the near sidewalk "covers the travel lane" in a close view.
//   - Every object carries its own fixed texture (seeded noise), so frame differencing finds it the way it finds a real
//     car or person, not only as the two edges a flat box would give.
//
// Scenes (frame numbers from 0; nothing moves for the first 20 frames so the background settles):
//   walker_then_car   the reported bug: a walker comes in at the left edge, walking right, and two frames later a car
//                     going the legal way (right to left, 35 mph) comes in at the right edge.  The walker's track used to
//                     run away on its own look-ahead while the car "hid" its front, and was logged as an 18 mph wrong-way
//                     car (VehicleDynamics::hiddenFrontBumper()).
//   walker_crossing_car  a walker crossing left to right, a third of the way over, when a legal 25 mph car comes in at the
//                     right edge.  The walker's track used to keep the right end of the box closed to new cars
//                     (oncomingLead() in videoSpeedTracker.cpp), so the car was never timed.
//   walker_right_then_car  the mirror of walker_then_car: a walker comes in at the right edge, walking left (the legal way),
//                     and a car going the wrong way (left to right, 25 mph) comes in at the left.  That car really is
//                     going the wrong way and must still be timed and called out.
//   wrong_way_car     a car alone going the wrong way at 25 mph: timed and called out.
//   legal_car         a car alone going the legal way at 25 mph.
//   custom            any walker and car, for exploring other timings (the sweep that found walker_then_car).
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kW = 1280, kH = 720, kFps = 30;
constexpr double kPxPerFt = 64.0;
constexpr double kFtPerSecPerMph = 5280.0 / 3600.0;

// A fixed noise texture for one object (or the background), smoothed so MJPEG keeps it.
cv::Mat texture(cv::Size size, int seed, int mean, int spread){
	cv::Mat t(size, CV_8UC3);
	cv::RNG rng{ uint64_t(seed) };
	rng.fill(t, cv::RNG::UNIFORM, cv::Scalar::all(mean - spread), cv::Scalar::all(mean + spread));
	cv::GaussianBlur(t, t, cv::Size(5, 5), 0);
	return t;
}

// Something moving sideways at a steady speed: its left side is at x0 + v * (f - f0) in frame f, and it is drawn only
// from frame f0 on.
struct Mover {
	cv::Mat look;
	int top;
	double x0, v;
	int f0;
};

// Pastes m's texture at its place in frame f, clipped to the picture.
void draw(cv::Mat& frame, const Mover& m, int f){
	if (f < m.f0) return;
	int x = int(m.x0 + m.v * (f - m.f0) + 0.5);
	cv::Rect where(x, m.top, m.look.cols, m.look.rows);
	cv::Rect seen = where & cv::Rect(0, 0, kW, kH);
	if (seen.empty()) return;
	m.look(cv::Rect(seen.x - x, seen.y - m.top, seen.width, seen.height)).copyTo(frame(seen));
}

double pxPerFrame(double mph){ return mph * kFtPerSecPerMph * kPxPerFt / kFps; }

// A car coming in at the left edge (leftToRight) or the right edge from frame f0.
Mover car(bool leftToRight, int f0, double mph){
	const int len = int(15 * kPxPerFt);
	double v = pxPerFrame(mph);
	return { texture(cv::Size(len, 310), leftToRight ? 11 : 12, 60, 50), 250, leftToRight ? -double(len) : double(kW), leftToRight ? v : -v, f0 };
}

// A walker on the near sidewalk coming in at the left edge (leftToRight) or the right edge from frame f0.
Mover walker(bool leftToRight, int f0, int width = 80, double mph = 3.0){
	double v = pxPerFrame(mph);
	return { texture(cv::Size(width, kH - 230), leftToRight ? 21 : 22, 200, 40), 230, leftToRight ? -double(width) : double(kW), leftToRight ? v : -v, f0 };
}

}  // namespace

int main(int argc, char** argv){
	if (argc < 3){
		std::fprintf(stderr, "usage: walker_clip <out.avi> <walker_then_car|walker_crossing_car|walker_right_then_car|wrong_way_car|legal_car>\n"
			"       walker_clip <out.avi> custom <walkerL2R 0|1> <walker f0> <carL2R 0|1> <car f0> <car mph> <walker width> <walker mph>\n");
		return 2;
	}
	const std::string scene = argv[2];
	std::vector<Mover> movers;
	int frames = 0;
	if (scene == "walker_then_car"){ movers = { walker(true, 20), car(false, 22, 35) }; frames = 240; }
	else if (scene == "walker_crossing_car"){ movers = { walker(true, 20, 110), car(false, 60, 25) }; frames = 240; }
	else if (scene == "walker_right_then_car"){ movers = { walker(false, 20), car(true, 22, 25) }; frames = 240; }
	else if (scene == "wrong_way_car"){ movers = { car(true, 20, 25) }; frames = 80; }
	else if (scene == "legal_car"){ movers = { car(false, 20, 25) }; frames = 80; }
	else if (scene == "custom" && argc == 10){
		movers = { walker(std::stoi(argv[3]) != 0, std::stoi(argv[4]), std::stoi(argv[8]), std::stod(argv[9])),
		           car(std::stoi(argv[5]) != 0, std::stoi(argv[6]), std::stod(argv[7])) };
		frames = std::stoi(argv[6]) + 200;
	}
	else { std::fprintf(stderr, "walker_clip: unknown scene '%s' (or wrong number of arguments)\n", scene.c_str()); return 2; }

	cv::VideoWriter out(argv[1], cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), kFps, cv::Size(kW, kH), true);
	if (!out.isOpened()){ std::fprintf(stderr, "walker_clip: can't write %s\n", argv[1]); return 1; }
	const cv::Mat background = texture(cv::Size(kW, kH), 1, 120, 25);
	for (int f = 0; f < frames; f++){
		cv::Mat frame = background.clone();
		for (const Mover& m : movers) draw(frame, m, f);
		out.write(frame);
	}
	return 0;
}
