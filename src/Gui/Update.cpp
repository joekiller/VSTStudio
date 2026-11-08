// Update.h has the design.  Pure functions only, so tests/update_test.cpp covers them without a network.
#include "Update.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace update {

Version Version::parse(const std::string& text) {
	Version v;
	std::string s = text;
	if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
	// Exactly three dot-separated numbers and nothing after: "2.1.0".  "0.0.0~pr75" and "2.1.0-rc1" are not releases.
	int parts[3] = {};
	size_t i = 0;
	for (int k = 0; k < 3; k++) {
		size_t start = i;
		while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
		if (i == start || i - start > 6) return v;
		parts[k] = std::stoi(s.substr(start, i - start));
		if (k < 2) {
			if (i >= s.size() || s[i] != '.') return v;
			i++;
		}
	}
	if (i != s.size()) return v;
	v.major = parts[0];
	v.minor = parts[1];
	v.patch = parts[2];
	// 0.0.0 is what every unreleased build is numbered from; it is never a release.
	v.valid = v.major || v.minor || v.patch;
	return v;
}

std::string Version::str() const {
	return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool Version::newerThan(const Version& o) const {
	if (!valid || !o.valid) return false;
	if (major != o.major) return major > o.major;
	if (minor != o.minor) return minor > o.minor;
	return patch > o.patch;
}

Platform thisPlatform() {
#if defined(_WIN32)
	return Platform::Windows;
#elif defined(__APPLE__)
	return Platform::Mac;
#else
	return Platform::Linux;
#endif
}

// The names release.yml gives the packages (windows.yml "Build the installer", macos.yml "Tar and disk image").
std::string packageName(Platform p, const Version& v) {
	switch (p) {
	case Platform::Windows: return "VSTStudio-" + v.str() + "-windows-x64-setup.exe";
	case Platform::Mac: return "VSTStudio-" + v.str() + "-macos-arm64.tar.gz";
	default: return "";
	}
}

namespace {

std::string lower(std::string s) {
	for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
	return s;
}

bool isHex64(const std::string& s) {
	return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

std::string str(const nlohmann::json& j, const char* key) {
	auto it = j.find(key);
	return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

Asset assetOf(const nlohmann::json& a) {
	Asset out;
	out.name = str(a, "name");
	out.url = str(a, "browser_download_url");
	auto size = a.find("size");
	if (size != a.end() && size->is_number_unsigned()) out.size = size->get<std::uint64_t>();
	// "digest": "sha256:<hex>" (GitHub has recorded one for every release asset since mid-2025).
	std::string d = lower(str(a, "digest"));
	if (d.rfind("sha256:", 0) == 0 && isHex64(d.substr(7))) out.sha256 = d.substr(7);
	return out;
}

}  // namespace

bool parseLatest(const std::string& json, Platform p, Release& out, std::string& error) {
	out = Release();
	nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
	if (j.is_discarded() || !j.is_object()) { error = "GitHub's answer wasn't readable"; return false; }
	// An error answer ({"message": "API rate limit exceeded ..."}) has a message and no tag.
	out.tag = str(j, "tag_name");
	if (out.tag.empty()) {
		std::string msg = str(j, "message");
		error = msg.empty() ? "GitHub didn't name a latest release" : "GitHub said: " + msg;
		return false;
	}
	if (j.value("draft", false) || j.value("prerelease", false)) { error = out.tag + " isn't a finished release"; return false; }
	out.version = Version::parse(out.tag);
	if (!out.version.valid) { error = "the latest release's tag (" + out.tag + ") isn't a version number"; return false; }
	out.page = str(j, "html_url");
	out.notes = str(j, "body");
	out.published = str(j, "published_at");
	std::string want = packageName(p, out.version);
	auto assets = j.find("assets");
	if (assets != j.end() && assets->is_array())
		for (const nlohmann::json& a : *assets) {
			if (!a.is_object()) continue;
			Asset as = assetOf(a);
			if (as.name == "SHA256SUMS") out.sums = as;
			else if (!want.empty() && as.name == want) out.package = as;
		}
	if (p == Platform::Linux) return true;   // Linux only reports the version
	if (!out.package.found()) { error = out.tag + " has no " + want + " yet"; return false; }
	if (!out.sums.found()) { error = out.tag + " has no SHA256SUMS, so its download can't be checked"; return false; }
	return true;
}

std::string sumFor(const std::string& sha256sums, const std::string& name) {
	std::istringstream in(sha256sums);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.size() < 66) continue;
		std::string hash = lower(line.substr(0, 64));
		if (!isHex64(hash) || line[64] != ' ') continue;
		std::string rest = line.substr(65);
		if (!rest.empty() && (rest[0] == ' ' || rest[0] == '*')) rest.erase(0, 1);
		if (rest == name) return hash;
	}
	return "";
}

std::string notesPreview(const std::string& markdown, int maxLines) {
	std::istringstream in(markdown);
	std::string line, out;
	int lines = 0;
	bool cut = false;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		// GitHub's generated notes end with "**Full Changelog**: https://github.com/.../compare/a...b".
		if (line.find("Full Changelog") != std::string::npos) continue;
		size_t b = line.find_first_not_of(" \t");
		if (b == std::string::npos) continue;
		line.erase(0, b);
		if (line[0] == '#') continue;   // headings ("What's Changed"): the prompt has its own
		// GitHub's generated lines end " by @someone in https://github.com/.../pull/72": keep what changed.
		size_t by = line.find(" by @");
		if (by != std::string::npos && line.find(" in https://", by) != std::string::npos) line.erase(by);
		if (line.rfind("* ", 0) == 0 || line.rfind("- ", 0) == 0) line = "\xe2\x80\xa2 " + line.substr(2);   // bullets
		// [text](url) -> text, and drop ** and `.
		std::string plain;
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] == '[') {
				size_t close = line.find("](", i);
				size_t end = close == std::string::npos ? close : line.find(')', close);
				if (end != std::string::npos) { plain += line.substr(i + 1, close - i - 1); i = end; continue; }
			}
			if (line.compare(i, 2, "**") == 0) { i++; continue; }
			if (line[i] == '`') continue;
			plain += line[i];
		}
		size_t e = plain.find_last_not_of(" \t");
		plain = e == std::string::npos ? "" : plain.substr(0, e + 1);
		if (plain.empty()) continue;
		if (lines == maxLines) { cut = true; break; }
		out += (lines ? "\n" : "") + plain;
		lines++;
	}
	if (cut) out += "\n\xe2\x80\xa6";
	return out;
}

bool checkDue(bool autoCheck, const Version& running, double lastCheckEpoch, double nowEpoch) {
	if (!autoCheck || !running.valid) return false;
	return lastCheckEpoch <= 0 || nowEpoch < lastCheckEpoch || nowEpoch - lastCheckEpoch >= kCheckEverySeconds;
}

bool shouldOffer(const Version& running, const Version& latest, const std::string& skipped) {
	if (!latest.newerThan(running)) return false;
	return Version::parse(skipped).str() != latest.str() || !Version::parse(skipped).valid;
}

}  // namespace update
