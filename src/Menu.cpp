// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings page, in SKSE Menu Framework's Mod Control Panel, under its own section so nothing of
// RE::Light's own menu is touched: Brightness, Reach, lights off while sneaking, hand lights, and one switch per option the
// installer put down. Every change is saved at once (Settings.cpp).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"

namespace Plugin
{
	namespace
	{
		const ImGuiMCP::ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };

		// the menu draws off the game's main thread; which light a magic effect wears is changed on it
		void RehandSoon()
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() { ApplyHandLights(true); });
			}
		}

		void __stdcall RenderSettings()
		{
			int b = BrightnessPercent();
			if (ImGuiMCP::SliderInt("Brightness", &b, 10, 200, "%d%%")) {
				SetBrightnessPercent(b);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"How bright this mod's lights are, and only this mod's. 100% is the measured brightness; about 30% "
				"is what the old Reduced download was. How far they carry does not change - that is the slider below.");

			int r = ReachPercent();
			if (ImGuiMCP::SliderInt("Reach", &r, 50, 150, "%d%%")) {
				SetReachPercent(r);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"How far this mod's lights carry. 100% is the measured reach. How bright they are does not change - "
				"that is the slider above.");

			bool sneak = SneakOn();
			if (ImGuiMCP::Checkbox("Lights off while sneaking", &sneak)) {
				SetSneakOn(sneak);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"While you sneak, no spell light turns on - hand lights, projectiles, runes, explosions and "
				"hazards - and the ones already lit go out. They come back when you stand up.");

			bool hands = HandLightsOn();
			if (ImGuiMCP::Checkbox("Hand lights", &hands)) {
				SetHandLightsOn(hands);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"A light on your hands while you cast, in the color of the spell. It takes effect on the next cast.");

			// ⚫ HIS CALL, 2026-09-22: *"runes and wepaons need to show up before the patches in the skse menu."*
			// The options used to be drawn in the order Data.cpp read their files, which is the folder listing -
			// alphabetical by file name - so `Misc - ` and `Patch Collection - ` came before `Spells - ` and
			// `Weapons - `. They are drawn in MENU order now, which the build writes.
			auto&       opts = Options();
			std::string shown;
			for (const auto i : OptionsInMenuOrder()) {
				auto& o = opts[i];
				if (!o.switchable) {
					continue;
				}
				if (o.download != shown) {
					shown = o.download;
					ImGuiMCP::Separator();
					ImGuiMCP::TextColored(kNote, "%s", shown.c_str());
				}
				ImGuiMCP::PushID(static_cast<int>(i));
				bool on = o.on;
				if (ImGuiMCP::Checkbox(o.name.c_str(), &on)) {
					SetOptionOn(i, on);
					SaveSettings();
					RehandSoon();
				}
				ImGuiMCP::SameLine();
				if (o.on) {
					ImGuiMCP::TextDisabled("%zu lit", o.lit);
				} else {
					ImGuiMCP::TextDisabled("off - %zu held out", o.heldOut);
				}
				ImGuiMCP::PopID();
			}
			ImGuiMCP::Separator();
			ImGuiMCP::TextDisabled("%zu data file(s), %zu travelling light(s) right now, %zu spell(s) with a hand light",
				DataFiles(), LiveStreamLights(), HandEffects());
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection("RELight - Spell Addon");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
