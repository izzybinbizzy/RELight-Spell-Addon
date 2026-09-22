// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings page, in SKSE Menu Framework's Mod Control Panel, under its own section so nothing of
// RE::Light's own menu is touched: Brightness, lights off while sneaking, and one switch per option the
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
				"Every light this mod makes, and only those. 100% is the measured brightness; about 30% is what the "
				"old Reduced download was. The reach does not change.");

			bool sneak = SneakOn();
			if (ImGuiMCP::Checkbox("Lights off while sneaking", &sneak)) {
				SetSneakOn(sneak);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"While you sneak, no spell light turns on - hand lights, projectiles, runes, explosions and "
				"hazards - and the ones already lit go out. They come back when you stand up.");

			auto&       opts = Options();
			std::string shown;
			for (std::size_t i = 0; i < opts.size(); ++i) {
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
			ImGuiMCP::TextDisabled("%zu data file(s), %zu travelling light(s) right now", DataFiles(), LiveStreamLights());
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
