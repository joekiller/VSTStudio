// Video plumbing for the GUI thread: OpenGL textures fed from cv::Mat, and a seekable Player for previewing a
// file, a camera or a stream while the zones are set up and calibrated.  Tracking itself runs in Tracker.
#pragma once
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include "FrameInput.h"   // the frame input port: every frame the preview shows comes in through it
#include "PiCamera.h"
#include <string>

// An RGBA OpenGL texture that follows a BGR/BGRA/gray Mat; reallocated only when the size changes.
class Texture {
public:
	Texture() = default;
	~Texture();
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	// Converts to RGBA in rgba_ and uploads.  `streaming` keeps rgba_ for the next update (video: no reallocation per frame);
	// a still uploaded once (a snapshot in Results' cache) passes false so only the GPU holds its pixels, not a CPU copy too.
	void update(const cv::Mat& image, bool streaming = true);
	void reset();
	bool valid() const { return id_ != 0; }
	void* imguiId() const { return (void*)(intptr_t)id_; }
	int width() const { return w_; }
	int height() const { return h_; }
private:
	unsigned id_ = 0;
	int w_ = 0, h_ = 0;
	cv::Mat rgba_;
};

// Textures for stills drawn in lists (Results' snapshots, by vehicle id or file path), loaded on demand and capped, so
// scrolling through thousands of rows over a long session doesn't keep a texture for every one.  When an insert takes it
// over kMax, the entries drawn longest ago go first; anything drawn this frame or the one before stays, since ImGui
// draws the frame's images after this code returns and must never be handed a texture that has been deleted.
// GUI thread only.  `frame` is ImGui::GetFrameCount() (any counter that goes up once a frame).
template <class Key>
class TextureCache {
public:
	static constexpr size_t kMax = 300;
	// The cached texture (null: tried before, couldn't load), marking it drawn this frame; `found` false = not cached yet.
	Texture* find(const Key& key, int frame, bool& found) {
		auto it = map_.find(key);
		found = it != map_.end();
		if (!found) return nullptr;
		it->second.drawn = frame;
		return it->second.tex.get();
	}
	// Caches `tex` (null to remember a failure), evicting as described above.
	Texture* put(const Key& key, std::unique_ptr<Texture> tex, int frame) {
		Texture* raw = tex.get();
		map_[key] = { std::move(tex), frame };
		while (map_.size() > kMax) {
			auto oldest = map_.end();
			for (auto it = map_.begin(); it != map_.end(); ++it)
				if (it->second.drawn < frame - 1 && (oldest == map_.end() || it->second.drawn < oldest->second.drawn)) oldest = it;
			if (oldest == map_.end()) break;   // all on screen right now: over the cap until they scroll away
			map_.erase(oldest);
		}
		return raw;
	}
	void clear() { map_.clear(); }
	size_t size() const { return map_.size(); }
private:
	struct Entry { std::unique_ptr<Texture> tex; int drawn = 0; };
	std::map<Key, Entry> map_;
};

// What a source is, as the rest of the app talks about it.
struct SourceInfo {
	enum Kind { None, File, Camera, Stream, Image } kind = None;
	std::string path;          // file path, camera index ("0"), Pi camera ("picam:0?...", see PiCamera.h) or URL
	std::string label;         // short name for the UI
	int width = 0, height = 0;
	double fps = 0;
	int frames = 0;            // 0 when unknown (cameras, streams, some H.264 files)
	std::string codec;
	// A camera's shutter as the opening policy left it, and the rate it really sent while opening (Facts, FrameInput.h).
	frameio::Shutter shutter = frameio::Shutter::Unknown;
	int shutterUs = 0;
	double measuredFps = 0;
	// A USB camera's own name, and whether VST picked its mode from the driver's list (and why) (Facts, FrameInput.h).
	std::string deviceName;
	bool modePicked = false;
	std::string modeWhy;
	double seconds() const { return fps > 0 && frames > 0 ? frames / fps : 0; }
};

// A Raspberry Pi camera ("picam...") is a Camera like a USB one: live, and shown with the camera icon.
SourceInfo::Kind classifySource(const std::string& path);
std::string displayName(const std::string& path);
// displayName for the UI: the same, except that the bundled 2016 sample says so ("Sample video (Locust Ave, 2016)"),
// so a preview of it is never mistaken for the user's own camera.
std::string friendlySourceName(const std::string& path);
// The same, cut to about `max` characters with an ellipsis in the middle, for headers and buttons (long URLs).
std::string shortSourceName(const std::string& path, size_t max = 44);
bool isVideoPath(const std::string& path);
bool isImagePath(const std::string& path);

// Frames are delivered at VST's 1280x720 working size so overlays use config coordinates directly.  Every frame comes in
// through the frame input port (FrameInput.h): frameio::open() picks the adapter for a file, camera, stream or photo with
// the same opening policy the tracker uses (so the preview shows a camera in the mode the run will get), and feed()
// pushes Live tracking's frames through a frameio::FeedSource.
//
// Nothing that can be slow runs on the GUI thread.  A camera can take seconds to open (or never answer), each read
// waits for the camera's next frame, and OpenCV serialises some backend work, so a file opened while a camera is
// still opening would wait for it.  A reader thread (LiveReader in Media.cpp) therefore does every open: for a camera
// or stream it then reads continuously and keeps only the newest frame, which tick() picks up without waiting; for a
// file it hands the opened capture back, and the GUI thread steps through it (reads and seeks are quick).  Photos
// load directly.  While an open is pending, opening() is true and the UI can show "Connecting…" with a Cancel.  close() (and Cancel, and opening something else) abandons the reader at
// once: it stops after its current call returns and releases the device itself, so a hung driver costs a thread,
// never the window.
class Player {
public:
	~Player();
	// Returns false only when a photo can't be read or the opener thread can't start.  A video, camera or stream's
	// failure arrives later through tick() as error, with opening() going false and isOpen() staying false.
	bool open(const std::string& path);
	void close();
	bool isOpen() const { return opened_; }
	bool opening() const { return live_ && !opened_ && error.empty(); }
	double openingSeconds(double now) const;   // how long the current live open has been going
	// close(), then wait up to maxS for an abandoned reader to let go of its device, for when something else is about
	// to open it (the tracker).  Usually one frame's wait; a hung driver is given up on after maxS.
	void closeAndWait(double maxS);
	struct LiveReader;                   // a camera or stream's reader thread state (Media.cpp)
	const SourceInfo& info() const { return info_; }
	bool seekable() const { return !fed_ && info_.kind == SourceInfo::File && info_.frames > 0; }
	// Reads the frame at index (files) or the next frame (cameras, streams).  Returns false at end or on error.
	bool seek(int index);
	bool next();
	// Called each GUI frame; when playing, advances in real time.  Returns true if a new frame arrived.
	bool tick(double now);
	bool playing = false;
	int position() const { return pos_; }
	// The shown frame, at 1280x720 and turned by the level angle: the same pixels the tracker will see (Level.h).
	const cv::Mat& frame() const { return frame_; }
	Texture& texture() { return tex_; }
	// Sets the software level angle (Globals::levelDegrees).  Call every GUI frame with the config's value; when it
	// changes, the current frame is re-turned from its unturned copy at once, so a paused video or a photo follows a
	// drag on the level slider.
	void setLevel(double degrees);
	// Shows a frame another reader already has, without a capture of its own: while Live tracking runs, its engine
	// holds the camera (a camera takes one reader at a time) and App::pumpTracker passes the tracker's frames on here,
	// so Connect the camera, Mark your street and Calibrate keep showing the same running feed.  `working` is the
	// engine's 1280x720 frame, already turned by `turnedBy` degrees (the run's levelDegrees); setLevel() turns it on by
	// the difference only, so dragging the level slider during a run still previews the new angle.  `about` replaces
	// info() when it names another source (the next video of a batch); for the same source the facts read when it
	// was opened (resolution, codec) are kept.  Fed frames are not seekable and tick()/next() never read; close() or
	// open() ends feeding.
	void feed(const cv::Mat& working, double turnedBy, const SourceInfo& about, int frameNumber);
	bool fed() const { return fed_; }
	std::string error;
private:
	bool readInto();
	bool tickLive();
	void show(const cv::Mat& working);   // keeps `working` (1280x720, unturned) as raw_, levels it into frame_, uploads it
	std::shared_ptr<LiveReader> live_;   // the reader thread's shared state, while opening and for cameras and streams
	std::uint64_t liveSeq_ = 0;          // the newest live frame already taken
	// The port the GUI thread reads: a file once the reader has opened it, or the FeedSource while fed.  Cameras and
	// streams are read on the reader thread, through the same port.
	std::unique_ptr<frameio::FrameSource> cap_;
	SourceInfo info_;
	bool opened_ = false;
	cv::Mat raw_;                        // the current frame before levelling, so a new angle needn't wait for a new frame
	cv::Mat frame_;
	double level_ = 0;
	bool fed_ = false;                   // cap_ is a frameio::FeedSource: frames handed in by feed(), not read from a device
	double fedLevel_ = 0;                // the angle the fed frames already carry; show() turns by level_ - fedLevel_
	Texture tex_;
	int pos_ = -1;
	double lastTick_ = 0, carry_ = 0;
};

// Resizes to 1280x720 like the tracker's readFrame() (frameio::toWorking).
inline void toWorkingSize(cv::Mat& frame) { frameio::toWorking(frame); }

// Opens a file or folder with the platform's default handler (Explorer/Finder/xdg-open).
void openWithSystem(const std::string& path);
