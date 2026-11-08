// Updater.h has the state machine; Update.h what is checked and why.
#include "Updater.h"
#include "Sha256.h"
#include "UpdateOs.h"
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

Updater::Updater(const std::string& runningVersion) : running_(update::Version::parse(runningVersion)) {}

Updater::~Updater() {
	cancel_ = true;
	if (worker_.joinable()) worker_.join();
}

Updater::Status Updater::status() const {
	std::lock_guard<std::mutex> l(mu_);
	return st_;
}

void Updater::set(State s, const std::string& error) {
	std::lock_guard<std::mutex> l(mu_);
	st_.state = s;
	st_.error = error;
}

// One worker at a time: the previous one has finished (busy_ is false) by the time a new one may start, so joining it
// here never waits for long.
void Updater::start(void (Updater::*fn)()) {
	if (worker_.joinable()) worker_.join();
	busy_ = true;
	cancel_ = false;
	worker_ = std::thread([this, fn] {
		(this->*fn)();
		busy_ = false;
	});
}

void Updater::check(bool manual) {
	if (busy_) return;
	{
		std::lock_guard<std::mutex> l(mu_);
		if (st_.state == State::Downloading || st_.state == State::Verifying || st_.state == State::Ready) return;
		st_.manual = manual;
		st_.state = State::Checking;
		st_.error.clear();
	}
	start(&Updater::doCheck);
}

void Updater::doCheck() {
	std::string why;
	bool can = updateos::canInstall(why);
	std::string body, error;
	std::string url = std::string("https://api.github.com/repos/") + update::kRepo + "/releases/latest";
	update::Release rel;
	bool ok = updateos::fetch(url, &body, "", nullptr, error) && update::parseLatest(body, update::thisPlatform(), rel, error);
	std::lock_guard<std::mutex> l(mu_);
	st_.canInstall = can;
	st_.cantInstall = can ? std::string() : why;
	if (!ok) {
		st_.state = State::Failed;
		st_.error = "Couldn't check for updates: " + error + ".";
		return;
	}
	st_.release = rel;
	st_.state = rel.version.newerThan(running_) ? State::Available : State::UpToDate;
}

void Updater::download() {
	if (busy_) return;
	{
		std::lock_guard<std::mutex> l(mu_);
		bool fromFailed = st_.state == State::Failed && st_.release.package.found();
		if (st_.state != State::Available && !fromFailed) return;
		if (!st_.canInstall) return;
		st_.manual = true;
		st_.state = State::Downloading;
		st_.error.clear();
		st_.got = 0;
		st_.total = st_.release.package.size;
	}
	start(&Updater::doDownload);
}

void Updater::cancel() { cancel_ = true; }

void Updater::doDownload() {
	update::Release rel = status().release;
	const update::Asset& pkg = rel.package;
	auto fail = [&](const std::string& why) {
		if (cancel_) set(State::Available);
		else set(State::Failed, why);
	};
	// 1. The checksum list (a few hundred bytes), and the line for our package in it.
	std::string sums, error;
	if (!updateos::fetch(rel.sums.url, &sums, "", nullptr, error)) { fail("Couldn't download the release's checksums: " + error + "."); return; }
	std::string listed = update::sumFor(sums, pkg.name);
	if (listed.empty()) { fail("The release's SHA256SUMS doesn't list " + pkg.name + ", so it wasn't downloaded."); return; }
	if (!pkg.sha256.empty() && pkg.sha256 != listed) { fail("GitHub's checksum for " + pkg.name + " doesn't match the release's SHA256SUMS, so it wasn't downloaded."); return; }

	// 2. The package, unless the same file is already here from an earlier try.
	fs::path dir = fs::u8path(updateos::downloadDir());
	fs::path file = dir / fs::u8path(pkg.name), part = dir / fs::u8path(pkg.name + ".part");
	std::error_code ec;
	if (!(fs::exists(file, ec) && sha256::hexOfPath(file) == listed)) {
		fs::remove(file, ec);
		bool got = updateos::fetch(pkg.url, nullptr, part.u8string(), [this](std::uint64_t n, std::uint64_t total) {
			std::lock_guard<std::mutex> l(mu_);
			st_.got = n;
			if (total) st_.total = total;
			return !cancel_.load();
		}, error);
		if (!got) { fs::remove(part, ec); fail("The download didn't finish: " + error + "."); return; }
		if (pkg.size && fs::file_size(part, ec) != pkg.size) { fs::remove(part, ec); fail("The download is the wrong size, so it was thrown away."); return; }
		if (sha256::hexOfPath(part) != listed) { fs::remove(part, ec); fail("The download doesn't match the release's checksum, so it was thrown away."); return; }
		fs::rename(part, file, ec);
		if (ec) { fail("Couldn't save the download: " + ec.message() + "."); return; }
	}

	// 3. The code signature, against whoever signed the copy that is running.
	set(State::Verifying);
	std::string signer = updateos::selfSigner();
	std::string staged;
	if (!updateos::verifyAndStage(file.u8string(), signer, staged, error)) {
		fs::remove(file, ec);   // never offer an untrusted file again
		set(State::Failed, error);
		return;
	}
	{
		std::lock_guard<std::mutex> l(mu_);
		staged_ = staged;
		st_.state = State::Ready;
	}
}

bool Updater::install(bool& quitNow, std::string& error) {
	quitNow = false;
	std::string staged;
	{
		std::lock_guard<std::mutex> l(mu_);
		if (st_.state != State::Ready || busy_) { error = "The update isn't ready yet."; return false; }
		staged = staged_;
	}
	return updateos::install(staged, quitNow, error);
}

void Updater::pretend(const Status& s) {
	std::lock_guard<std::mutex> l(mu_);
	st_ = s;
}
