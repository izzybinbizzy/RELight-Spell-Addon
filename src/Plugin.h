// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What the files share. The file map is at the top of main.cpp.

#pragma once

namespace Plugin
{
	// ------------------------------------------------------------------ Data.cpp: what the installed options light
	//
	// Every layer the installer put down (Core, and each option he ticked) brings one small text file into
	// Data\SKSE\Plugins\RelightSpellAddon\. The build writes them; this reads them once, when the game has
	// loaded its data. They say three things: which objects this mod lights (the brightness slider scales
	// those and nothing else), which layers are plain tick-box options (the menu draws a switch for each),
	// and the sprays, breath shouts and beams whose lights this plugin hangs itself so they travel.
	inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

	struct Option
	{
		std::string              download, name, id;  // id: "Spells - Runes", the settings file's key
		int                      order{ 0 };          // a later layer overrides an earlier one
		bool                     switchable{ false };
		bool                     on{ true };
		std::size_t              meshes{ 0 }, bases{ 0 }, streams{ 0 };
		std::size_t              lit{ 0 }, heldOut{ 0 };  // counted every frame, shown in the menu
	};

	struct Stream
	{
		std::string   key, node;  // node "-": the light sits at the projectile's own origin
		RE::NiColor   color{ 1.0f, 1.0f, 1.0f };
		float         fade{ 1.0f }, radius{ 133.0f }, size{ 2.5f }, cutoff{ 0.3f };
		RE::NiPoint3  position{};
		int           order{ 0 };
		std::size_t   option{ kNone };
	};

	void                  LoadData();
	std::vector<Option>&  Options();
	std::size_t           DataFiles();
	std::size_t           OptionOf(RE::TESForm* a_base);  // kNone when this mod does not light it
	const Stream*         StreamOf(RE::TESForm* a_base);  // nullptr when it is not one of our streams
	std::string           MeshKey(std::string_view a_path);
	bool                  IsHandLightRecord(RE::TESObjectLIGH* a_light);

	// ------------------------------------------------------------------ Settings.cpp: the settings file
	void  LoadSettings();
	void  SaveSettings();
	int   BrightnessPercent();
	void  SetBrightnessPercent(int a_percent);
	float Brightness();
	bool  SneakOn();
	void  SetSneakOn(bool a_on);
	void  SetOptionOn(std::size_t a_index, bool a_on);

	// ------------------------------------------------------------------ Options.cpp: the switches
	void UpdateOptionLights();                         // every frame, after the sneaking pass
	void CullOptionLightsUnder(RE::NiAVObject* a_root);  // on a 3D that has just loaded
	bool HeldOutForOption(RE::NiLight* a_light);

	// ------------------------------------------------------------------ Brightness.cpp: our own slider
	void UpdateBrightness();                  // every frame, last
	void RememberHandLight(RE::NiLight* a_light);

	// ------------------------------------------------------------------ Streams.cpp: lights that travel
	void        HangStreamLights(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root);
	void        UpdateStreamLights();
	std::size_t LiveStreamLights();

	// ------------------------------------------------------------------ Menu.cpp
	void RegisterMenu();
}
