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
//   [Switches]
//   Spells - Runes=1            one line per switch; a switch with no line is on
// (A [Brightness] section from the old per-option sliders is ignored - his call 2026-09-28 late night, the sliders are gone.)

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/RelightSpellAddon.ini";
		constexpr int         kMin = 10, kMax = 200;
		constexpr int         kReachMin = 50, kReachMax = 150;

		int  gBrightness = 100;
		int  gReach = 100;
		bool gSneak = false;
		bool gHands = true;
		bool gWeapons = true;
		int  gWard = 0;

		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
				s.pop_back();
			}
			std::size_t i = 0;
			while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
				++i;
			}
			return s.substr(i);
		}
	}

	void LoadSettings()
	{
		std::ifstream in(kPath);
		std::string   line, section;
		std::size_t   read = 0;
		while (in && std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = line.substr(1, line.size() - 2);
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Trim(line.substr(0, eq));
			const auto val = Trim(line.substr(eq + 1));
			int        v = 0;
			std::from_chars(val.data(), val.data() + val.size(), v);
			++read;
			if (section == "Settings" && key == "Brightness") {
				gBrightness = std::clamp(v, kMin, kMax);
			} else if (section == "Settings" && key == "Reach") {
				gReach = std::clamp(v, kReachMin, kReachMax);
			} else if (section == "Settings" && key == "LightsOffWhileSneaking") {
				gSneak = v != 0;
			} else if (section == "Settings" && key == "HandLights") {
				gHands = v != 0;
			} else if (section == "Settings" && key == "WeaponLights") {
				gWeapons = v != 0;
			} else if (section == "Settings" && key == "WardColour") {
				gWard = std::clamp(v, 0, 1);
			} else if (section == "Switches") {
				for (auto& o : Options()) {
					if (o.switchable && o.id == key) {
						o.on = v != 0;
					}
				}
			}
		}
		std::size_t off = 0;
		for (auto& o : Options()) {
			off += (o.switchable && !o.on) ? 1 : 0;
		}
		SKSE::log::info(
			"settings: brightness {}%, reach {}%, lights off while sneaking {}, hand lights {}, weapon lights {}, {} switch(es) off "
			"({} line(s) read)",
			gBrightness, gReach, gSneak ? "on" : "off", gHands ? "on" : "off", gWeapons ? "on" : "off", off, read);
	}

	void SaveSettings()
	{
		std::ofstream out(kPath, std::ios::trunc);
		if (!out) {
			SKSE::log::warn("settings: {} could not be written", kPath);
			return;
		}
		out << "; RELight - Spell Addon - written by its menu (SKSE Menu Framework)\n";
		out << "[Settings]\nBrightness=" << gBrightness << "\nReach=" << gReach
			<< "\nLightsOffWhileSneaking=" << (gSneak ? 1 : 0) << "\nHandLights=" << (gHands ? 1 : 0)
			<< "\nWeaponLights=" << (gWeapons ? 1 : 0) << "\nWardColour=" << gWard << "\n";
		out << "[Switches]\n";
		std::unordered_set<std::string> written;  // a pack's files share one switch, so one line
		for (const auto& o : Options()) {
			if (o.switchable && written.insert(o.id).second) {
				out << o.id << "=" << (o.on ? 1 : 0) << "\n";
			}
		}
	}

	int   BrightnessPercent() { return gBrightness; }
	float Brightness() { return static_cast<float>(gBrightness) / 100.0f; }

	void SetBrightnessPercent(int a_percent)
	{
		a_percent = std::clamp(a_percent, kMin, kMax);
		if (a_percent != gBrightness) {
			gBrightness = a_percent;
			SKSE::log::info("brightness set to {}%", gBrightness);
		}
	}

	int   ReachPercent() { return gReach; }
	float Reach() { return static_cast<float>(gReach) / 100.0f; }

	void SetReachPercent(int a_percent)
	{
		a_percent = std::clamp(a_percent, kReachMin, kReachMax);
		if (a_percent != gReach) {
			gReach = a_percent;
			SKSE::log::info("reach set to {}%", gReach);
		}
	}

	bool SneakOn() { return gSneak; }

	void SetSneakOn(bool a_on)
	{
		if (gSneak != a_on) {
			gSneak = a_on;
			SKSE::log::info("lights off while sneaking turned {}", a_on ? "on" : "off");
		}
	}

	bool HandLightsOn() { return gHands; }

	void SetHandLightsOn(bool a_on)
	{
		if (gHands != a_on) {
			gHands = a_on;
			SKSE::log::info("hand lights turned {}", a_on ? "on" : "off");
		}
	}

	bool WeaponLightsOn() { return gWeapons; }

	void SetWeaponLightsOn(bool a_on)
	{
		if (gWeapons != a_on) {
			gWeapons = a_on;
			SKSE::log::info("weapon lights turned {}", a_on ? "on" : "off");
		}
	}

	int WardColour() { return gWard; }

	void SetWardColour(int a_colour)
	{
		a_colour = std::clamp(a_colour, 0, 1);
		if (a_colour != gWard) {
			gWard = a_colour;
			SKSE::log::info("ward colour set to {}", gWard == 1 ? "white" : "vanilla blue");
		}
	}

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
