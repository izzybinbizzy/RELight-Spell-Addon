// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Two pages in SKSE Menu Framework's Mod Control Panel, under their own section so nothing of RE::Light's own menu is
// touched. Settings: Brightness, Reach, lights off while sneaking, hand lights, and a switch per option the installer put
// down. Patches: a switch per mod patch. Every change is saved at once (Settings.cpp) and reaches lights already lit.

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
				tasks->AddTask([]() {
					ApplyHandLights(true);
					ApplyStreamProjectileLights(true);
				});
			}
		}

		constexpr std::string_view kPatches = "Patch Collection";

		// the switches. Settings: everything but the patches, under each download's heading, in the build's menu order.
		// Patches (his call, 2026-09-23): a heading per category in the build's order, and under it each author's mods
		// together under the author's name, then the mods with no author group - by name within each.
		void DrawSwitches(bool a_patches)
		{
			auto&                    opts = Options();
			std::vector<std::size_t> order;
			for (const auto i : OptionsInMenuOrder()) {
				if (opts[i].switchable && (opts[i].download == kPatches) == a_patches) {
					order.push_back(i);
				}
			}
			if (a_patches) {
				std::ranges::stable_sort(order, {}, [&opts](std::size_t a_i) {
					const auto& o = opts[a_i];
					return std::make_tuple(o.menu, o.author.empty(), o.author, o.name);
				});
			}
			std::string shown, author;
			for (const auto i : order) {
				auto& o = opts[i];
				const auto& heading = a_patches ? (o.category.empty() ? o.download : o.category) : o.download;
				if (heading != shown) {
					shown = heading;
					author.clear();
					ImGuiMCP::Separator();
					ImGuiMCP::TextColored(kNote, "%s", shown.c_str());
				}
				if (a_patches && o.author != author) {
					author = o.author;
					if (!author.empty()) {
						ImGuiMCP::TextDisabled("%s", author.c_str());
					}
				}
				const bool indented = a_patches && !o.author.empty();  // an author's mods sit under the author's name
				if (indented) {
					ImGuiMCP::Indent();
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
				if (indented) {
					ImGuiMCP::Unindent();
				}
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
				"A light on your hands while you cast, in the color of the spell. Changes reach a spell you are already holding.");

			DrawSwitches(false);
			ImGuiMCP::Separator();
			ImGuiMCP::TextDisabled("%zu data file(s), %zu travelling light(s) right now, %zu spell(s) with a hand light",
				DataFiles(), LiveStreamLights(), HandEffects());
		}

		// his ask, 2026-09-23: the mod patches on a page of their own
		void __stdcall RenderPatches()
		{
			ImGuiMCP::TextDisabled("%s", "Lights for other mods' spells. Each switch only matters if you have that mod.");
			DrawSwitches(true);
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
		SKSEMenuFramework::AddSectionItem("Patches", RenderPatches);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
