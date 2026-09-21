// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The one tick box the probe needs, in SKSE Menu Framework's Mod Control Panel, under its own section so
// nothing of RE::Light's own menu is touched.
//
// It says what it is doing while it does it: how many of this option's lights are lit, and how many it is
// holding out. That is there so the answer does not have to be guessed from what the room looks like.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"

namespace Plugin
{
	namespace
	{
		const ImGuiMCP::ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };

		void __stdcall RenderProbe()
		{
			ImGuiMCP::TextColored(kNote, "%s", "Probe - one option, switched while the game runs");
			ImGuiMCP::TextWrapped("%s",
				"This is a test of one thing: whether an installer option can be turned on and off from a menu "
				"instead of at install time. Only Runes is wired up. Nothing is written to disk and nothing else "
				"in the mod changes.");
			ImGuiMCP::Separator();

			bool on = RunesOn();
			if (ImGuiMCP::Checkbox("Runes", &on)) {
				SetRunesOn(on);
			}
			ImGuiMCP::SetItemTooltip("%s",
				"Rune lights, the three rune explosions and the fireball explosion flash. Untick it while a rune "
				"is on the ground in front of you.");

			ImGuiMCP::Separator();
			ImGuiMCP::TextDisabled("lit right now: %zu", RunesLit());
			ImGuiMCP::TextDisabled("held out by the probe: %zu", RunesHeldOut());

			ImGuiMCP::Separator();
			ImGuiMCP::TextWrapped("%s",
				"What to look for. Place a Fire Rune and leave it on the ground, then untick the box: the rune "
				"should go dark without a flicker, and tick it again and the light should come straight back. "
				"Then, with the box unticked, cast a Fireball at a wall - the explosion should never flash.");
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so the probe has no tick box; the option stays on");
			return;
		}
		SKSEMenuFramework::SetSection("RELight - Spell Addon");
		SKSEMenuFramework::AddSectionItem("Probe", RenderProbe);
		SKSE::log::info("probe: one tick box added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
