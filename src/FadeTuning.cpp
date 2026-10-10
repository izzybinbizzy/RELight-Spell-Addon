// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// THE ALL-IN-ONE ADVANCED SETTINGS FILE - HIS RULE 2026-10-10: "illuminated and relight should also both be dumping inis for all
// settings that can be changed that make sense like illuminated does for fading. nothing that the skse menu already handles
// though. make them aio and seperated by category such as fading." So: ONE file per mod (Mod::kAdvancedPath), a section per
// category ([Fading], [HUD], and the mod's own), holding the values worth tuning that the menu does NOT show - the menu's
// settings stay in the menu and its own files. At data load every value the mod registered is read, and the whole file is
// written back with every value and a note (so a player finds every knob, at its default, in one place); a value out of range
// is clamped and said in the log. Read once: a change takes effect the next time the game starts.

#include "Fade.h"

#include <sstream>

namespace Fade::Tuning
{
	namespace
	{
		struct Knob
		{
			std::string         section, key, note;
			float               def{ 0.0f }, lo{ 0.0f }, hi{ 0.0f };
			std::atomic<float>* value{ nullptr };
		};
		std::vector<Knob>& Knobs()
		{
			static std::vector<Knob> knobs;
			return knobs;
		}
	}

	void Register(std::string_view a_section, std::string_view a_key, float a_default, float a_lo, float a_hi, std::string_view a_note,
		std::atomic<float>& a_value)
	{
		a_value.store(a_default, std::memory_order_relaxed);
		Knobs().push_back({ std::string(a_section), std::string(a_key), std::string(a_note), a_default, a_lo, a_hi, &a_value });
	}

	void Load()
	{
		auto&                              knobs = Knobs();
		std::map<std::string, std::string> found;  // "section|key" lower case -> the value as written
		{
			std::ifstream in(Mod::kAdvancedPath);
			std::string   line, section;
			while (std::getline(in, line)) {
				auto t = SettingsText::Trim(line);
				if (const auto c = t.find_first_of(";#"); c != std::string_view::npos) {
					t = SettingsText::Trim(t.substr(0, c));
				}
				if (t.empty()) {
					continue;
				}
				if (t.front() == '[' && t.back() == ']') {
					section = Lower(SettingsText::Trim(t.substr(1, t.size() - 2)));
					continue;
				}
				if (const auto eq = t.find('='); eq != std::string_view::npos) {
					found[section + "|" + Lower(SettingsText::Trim(t.substr(0, eq)))] = std::string(SettingsText::Trim(t.substr(eq + 1)));
				}
			}
		}
		std::size_t taken = 0;
		for (auto& k : knobs) {
			const auto it = found.find(Lower(k.section) + "|" + Lower(k.key));
			if (it == found.end()) {
				continue;
			}
			float       v = 0.0f;
			const auto& s = it->second;
			if (std::from_chars(s.data(), s.data() + s.size(), v).ec != std::errc{} || !std::isfinite(v)) {
				SKSE::log::warn("advanced settings: [{}] {}={} is not a number; the default {} stays", k.section, k.key, s, k.def);
				continue;
			}
			if (v < k.lo || v > k.hi) {
				SKSE::log::warn("advanced settings: [{}] {}={} is outside {} .. {}; clamped", k.section, k.key, v, k.lo, k.hi);
				v = std::clamp(v, k.lo, k.hi);
			}
			k.value->store(v, std::memory_order_relaxed);
			++taken;
		}
		// the whole file again: every knob, in its section, with its note - the "dump" (his word)
		std::ostringstream text;
		text << "; " << Mod::kName << " - advanced settings: what the SKSE menu does not show, one file, a section per category.\n"
			 << "; Read when the game starts. A value out of its range is clamped. Delete a line to get its default back.\n";
		std::string section;
		auto        order = knobs;
		std::ranges::stable_sort(order, {}, &Knob::section);
		for (const auto& k : order) {
			if (k.section != section) {
				section = k.section;
				text << "\n[" << section << "]\n";
			}
			text << "; " << k.note << " (" << k.lo << " to " << k.hi << ", default " << k.def << ")\n"
				 << k.key << "=" << k.value->load(std::memory_order_relaxed) << "\n";
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(Mod::kAdvancedPath).parent_path(), ec);
		const std::string tmp = std::string(Mod::kAdvancedPath) + ".tmp";
		{
			std::ofstream out(tmp, std::ios::trunc);
			out << text.str();
			if (!out.flush()) {
				SKSE::log::warn("advanced settings: {} could not be written", Mod::kAdvancedPath);
				return;
			}
		}
		if (std::filesystem::rename(tmp, Mod::kAdvancedPath, ec); ec) {
			SKSE::log::warn("advanced settings: {} could not be replaced ({})", Mod::kAdvancedPath, ec.message());
			std::filesystem::remove(tmp, ec);
		}
		SKSE::log::info("advanced settings: {} value(s) of {} read from {}; the file is written back whole", taken, knobs.size(), Mod::kAdvancedPath);
	}
}
