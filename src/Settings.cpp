// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The settings file, Data\SKSE\Plugins\RelightSpellAddon.ini. Read once at start, written whenever the menu changes one.
//
//   [Settings]
//   Brightness=100              percent, 10 to 200 - this mod's lights only; the reach is held
//   Reach=100                   percent, 50 to 150 - the peak is held
//   LightsOffWhileSneaking=0
//   HandLights=1                the light on your hands while you cast (HandLights.cpp)
//   WeaponLights=1              every weapon light, the enchantment lights included (Options.cpp)
//   WardColour=0                0 vanilla blue, 1 white - the ward's art and light (Wards.cpp)
//   DimInDaylight=1             0 off, 1 a little, 2 more (Brightness.cpp)
//   HandLightsFor=0             0 everyone, 1 everyone nearby, 2 player and followers, 3 player only (main.cpp)
//   AutoLights=1                spells from mods with no patch get a hand light too (HandLights.cpp)
//   [Switches]
//   Spells - Runes=1            one line per switch; a switch with no line is on
// Keys match case-insensitively. A value that is not a whole number keeps its default and is named in the log. An old [Brightness] section (the per-option
// sliders, retired) is passed over.
//
// Threads: the menu changes a setting on the render thread while the hooks read it on the main thread and on the game's
// loader threads, so each one is an atomic (relaxed: no setting depends on another being written first). The file is written
// beside itself and moved over it, so a crash or a full disk mid-write leaves the old settings, never half a file.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/RelightSpellAddon.ini";
		constexpr int         kMin = 10, kMax = 200;
		constexpr int         kReachMin = 50, kReachMax = 150;

		std::atomic<int>  gBrightness{ 100 };
		std::atomic<int>  gReach{ 100 };
		std::atomic<bool> gSneak{ false };
		std::atomic<bool> gHands{ true };
		std::atomic<bool> gWeapons{ true };
		std::atomic<int>  gWard{ 0 };
		std::atomic<int>  gDaylight{ 1 };
		std::atomic<int>  gHandsFor{ 0 };
		std::atomic<bool> gAuto{ true };

		[[nodiscard]] std::string_view Trim(std::string_view a_s) noexcept
		{
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.front()))) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.back()))) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		// a whole number and nothing else ("15abc" and "1.5" are not)
		[[nodiscard]] std::optional<int> WholeNumber(std::string_view a_v) noexcept
		{
			int v = 0;
			const auto [end, ec] = std::from_chars(a_v.data(), a_v.data() + a_v.size(), v);
			if (a_v.empty() || ec != std::errc{} || end != a_v.data() + a_v.size()) {
				return std::nullopt;
			}
			return v;
		}

		[[nodiscard]] bool SameText(std::string_view a, std::string_view b) noexcept
		{
			return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// Every [Settings] key: its name in the file, how it reads back as a whole number, and how a whole number sets it
		// (clamped to what the menu allows). ONE table, so the reader, the writer and the log can never disagree - the
		// fading module's way (FadeSettingsText.h kKeys; the re-score's RELight issue 3). Keys match case-insensitively.
		struct Key
		{
			const char* name;
			int (*get)();
			void (*set)(int);
		};

		inline constexpr Key kKeys[]{
			{ "Brightness", [] { return gBrightness.load(); }, [](int v) { gBrightness = std::clamp(v, kMin, kMax); } },
			{ "Reach", [] { return gReach.load(); }, [](int v) { gReach = std::clamp(v, kReachMin, kReachMax); } },
			{ "LightsOffWhileSneaking", [] { return gSneak ? 1 : 0; }, [](int v) { gSneak = v != 0; } },
			{ "HandLights", [] { return gHands ? 1 : 0; }, [](int v) { gHands = v != 0; } },
			{ "WeaponLights", [] { return gWeapons ? 1 : 0; }, [](int v) { gWeapons = v != 0; } },
			{ "WardColour", [] { return gWard.load(); }, [](int v) { gWard = std::clamp(v, 0, 1); } },
			{ "DimInDaylight", [] { return gDaylight.load(); }, [](int v) { gDaylight = std::clamp(v, 0, 2); } },
			{ "HandLightsFor", [] { return gHandsFor.load(); }, [](int v) { gHandsFor = std::clamp(v, 0, 3); } },
			{ "AutoLights", [] { return gAuto ? 1 : 0; }, [](int v) { gAuto = v != 0; } },
		};

		// one [Settings] line; false when the key is not one of ours
		bool ApplySetting(std::string_view a_key, int a_v)
		{
			for (const auto& k : kKeys) {
				if (SameText(a_key, k.name)) {
					k.set(a_v);
					return true;
				}
			}
			return false;
		}

		// the file itself: written beside itself and moved over, on the thread that calls it (the main thread - SaveSettings)
		void WriteSettingsFile()
		{
			std::ostringstream text;
			text << "; RELight - Spell Addon - written by its menu (SKSE Menu Framework)\n[Settings]\n";
			for (const auto& k : kKeys) {
				text << k.name << "=" << k.get() << "\n";
			}
			text << "[Switches]\n";
			std::unordered_set<std::string> written;  // a pack's files share one switch, so one line
			for (const auto& o : Options()) {
				if (o.switchable && written.insert(o.id).second) {
					text << o.id << "=" << (o.on ? 1 : 0) << "\n";
				}
			}
			const std::string tmp = std::string(kPath) + ".tmp";
			std::error_code   ec;
			std::ofstream     out(tmp, std::ios::trunc);
			if (!out) {
				SKSE::log::warn("settings: {} could not be written", kPath);
				return;
			}
			out << text.str();
			out.close();
			if (!out) {
				SKSE::log::warn("settings: {} could not be written", kPath);
			} else if (std::filesystem::rename(tmp, kPath, ec); ec) {
				SKSE::log::warn("settings: {} could not be replaced ({})", kPath, ec.message());
			}
			std::filesystem::remove(tmp, ec);  // nothing left behind when the move failed
		}
	}

	void LoadSettings()
	{
		std::ifstream            in(kPath);
		std::string              line;
		std::string              section;
		std::size_t              read = 0;
		std::vector<std::string> problems;
		while (in && std::getline(in, line)) {
			const auto l = Trim(line);
			if (l.empty() || l.front() == ';' || l.front() == '#') {
				continue;
			}
			if (l.front() == '[' && l.back() == ']') {
				section = Trim(l.substr(1, l.size() - 2));
				continue;
			}
			const auto eq = l.find('=');
			if (eq == std::string_view::npos) {
				continue;
			}
			const auto key = Trim(l.substr(0, eq));
			const auto val = Trim(l.substr(eq + 1));
			if (section != "Settings" && section != "Switches") {
				continue;  // the retired [Brightness] section, or a section of someone else's
			}
			const auto v = WholeNumber(val);
			if (!v) {
				problems.push_back(std::format("{}={}: not a whole number; the default stays", key, val));
				continue;
			}
			++read;
			if (section == "Settings") {
				if (!ApplySetting(key, *v)) {
					problems.push_back(std::format("{}: not a setting", key));
				}
			} else {
				for (auto& o : Options()) {
					if (o.switchable && SameText(o.id, key)) {
						o.on = *v != 0;
					}
				}
			}
		}
		for (const auto& p : problems) {
			SKSE::log::warn("settings: {}", p);
		}
		std::size_t off = 0;
		for (const auto& o : Options()) {
			off += (o.switchable && !o.on) ? 1 : 0;
		}
		SKSE::log::info(
			"settings: brightness {}%, reach {}%, lights off while sneaking {}, hand lights {}, weapon lights {}, {} switch(es) off "
			"({} line(s) read, {} problem(s))",
			gBrightness.load(), gReach.load(), gSneak ? "on" : "off", gHands ? "on" : "off", gWeapons ? "on" : "off", off, read,
			problems.size());
	}

	// The menu calls this on the RENDER thread, often many times while a slider is dragged: the file is written once, by an
	// SKSE task on the main thread (the re-score: no file work on the render thread). The values are atomics, so the task
	// writes whatever they hold when it runs.
	void SaveSettings()
	{
		static std::atomic<bool> queued{ false };
		if (queued.exchange(true)) {
			return;
		}
		const auto write = [] {
			queued = false;
			WriteSettingsFile();
		};
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask(write);
		} else {
			write();
		}
	}

	int   BrightnessPercent() { return gBrightness; }
	float Brightness() { return static_cast<float>(gBrightness) / 100.0f; }

	void SetBrightnessPercent(int a_percent)
	{
		a_percent = std::clamp(a_percent, kMin, kMax);
		if (gBrightness.exchange(a_percent) != a_percent) {
			SKSE::log::info("brightness set to {}%", a_percent);
		}
	}

	int   ReachPercent() { return gReach; }
	float Reach() { return static_cast<float>(gReach) / 100.0f; }

	void SetReachPercent(int a_percent)
	{
		a_percent = std::clamp(a_percent, kReachMin, kReachMax);
		if (gReach.exchange(a_percent) != a_percent) {
			SKSE::log::info("reach set to {}%", a_percent);
		}
	}

	bool SneakOn() { return gSneak; }

	void SetSneakOn(bool a_on)
	{
		if (gSneak.exchange(a_on) != a_on) {
			SKSE::log::info("lights off while sneaking turned {}", a_on ? "on" : "off");
		}
	}

	bool HandLightsOn() { return gHands; }

	void SetHandLightsOn(bool a_on)
	{
		if (gHands.exchange(a_on) != a_on) {
			SKSE::log::info("hand lights turned {}", a_on ? "on" : "off");
		}
	}

	bool WeaponLightsOn() { return gWeapons; }

	void SetWeaponLightsOn(bool a_on)
	{
		if (gWeapons.exchange(a_on) != a_on) {
			SKSE::log::info("weapon lights turned {}", a_on ? "on" : "off");
		}
	}

	int WardColour() { return gWard; }

	void SetWardColour(int a_colour)
	{
		a_colour = std::clamp(a_colour, 0, 1);
		if (gWard.exchange(a_colour) != a_colour) {
			SKSE::log::info("ward color set to {}", a_colour == 1 ? "white" : "vanilla blue");
		}
	}

	// a preset (Menu.cpp, his word 2026-10-10: presets kept per lighting): every [Settings] key, and every switch as "switch:<id>"
	std::vector<std::pair<std::string, int>> SettingsSnapshot()
	{
		std::vector<std::pair<std::string, int>> out;
		for (const auto& k : kKeys) {
			out.emplace_back(k.name, k.get());
		}
		std::unordered_set<std::string> seen;
		for (const auto& o : Options()) {
			if (o.switchable && seen.insert(o.id).second) {
				out.emplace_back("switch:" + o.id, o.on ? 1 : 0);
			}
		}
		return out;
	}

	void ApplySettingsSnapshot(const std::vector<std::pair<std::string, int>>& a_values)
	{
		for (const auto& [key, v] : a_values) {
			if (key.starts_with("switch:")) {
				const auto id = std::string_view(key).substr(7);
				for (std::size_t i = 0; i < Options().size(); ++i) {
					if (Options()[i].switchable && SameText(Options()[i].id, id)) {
						SetOptionOn(i, v != 0);
					}
				}
			} else {
				ApplySetting(key, v);
			}
		}
	}

	int  DimInDaylight() { return gDaylight; }
	void SetDimInDaylight(int a_v) { gDaylight = std::clamp(a_v, 0, 2); }
	int  HandLightsFor() { return gHandsFor; }
	void SetHandLightsFor(int a_v) { gHandsFor = std::clamp(a_v, 0, 3); }
	bool AutoLightsOn() { return gAuto; }
	void SetAutoLightsOn(bool a_on) { gAuto = a_on; }

	// his call, 2026-09-26: a pack the build split across the downloads is ONE switch - its files share a `file` line, so
	// every option with that id turns together
	void SetOptionOn(std::size_t a_index, bool a_on)
	{
		auto& opts = Options();
		if (a_index >= opts.size() || !opts[a_index].switchable) {
			return;
		}
		const auto  id = opts[a_index].id;
		std::size_t n = 0;
		for (auto& o : opts) {
			if (o.switchable && o.id == id && o.on != a_on) {
				o.on = a_on;
				++n;
			}
		}
		if (n) {
			SKSE::log::info("switch: {} turned {} ({} file(s))", id, a_on ? "on" : "off", n);
		}
	}
}
