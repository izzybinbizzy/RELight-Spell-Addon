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
		int                      menu{ 0 };           // where it sits in the menu, which is NOT the override order
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
		// 🔴 EVERY position the winning layer gives this mesh - a beam's ladder, a spray's ladder. Until 2026-09-23 a
		// second line for the same mesh REPLACED the first, so lightning's five-light ladder came out as one light.
		std::vector<RE::NiPoint3> positions;
		int           order{ 0 };
		std::size_t   option{ kNone };
	};

	// 🖐 one hand light, as one layer lights it. The colour, strength and reach are the layer's own light for the
	// casting-art mesh `key`, so an option or art replacer that recolours the spell recolours the hand too.
	struct Hand
	{
		std::string   key;
		RE::NiColor   color{ 1.0f, 1.0f, 1.0f };
		std::uint8_t  rgb[3]{ 255, 255, 255 };
		float         fade{ 1.0f }, radius{ 133.0f }, size{ 2.0f }, cutoff{ 0.2215f };
		bool          inverseSquare{ true }, portalStrict{ true };
		int           order{ 0 };
		std::size_t   option{ kNone };
	};

	// ⚫ THE HOUSE CONSTANT, and it is the same number in gen.py, relightgen.py and Luminous Arcana:
	// 0.8 * 69.99². Every config's cutoff was written from it, so anything that re-derives a cutoff at
	// run time has to use it or the light changes reach the moment it is touched.
	inline constexpr float kK = 3918.88f;

	void                  LoadData();
	std::vector<Option>&  Options();
	// the option indices in the order the MENU draws them - `menu` from the data file, `order` when a
	// file is older than that field. Never the folder listing, which is alphabetical by file name.
	const std::vector<std::size_t>& OptionsInMenuOrder();
	std::size_t           DataFiles();
	std::size_t           OptionOf(RE::TESForm* a_base);  // kNone when this mod does not light it
	const Stream*         StreamOf(RE::TESForm* a_base);  // nullptr when it is not one of our streams
	std::string           MeshKey(std::string_view a_path);
	std::string           PathKey(std::string_view a_path);
	// every hand key -> the layers that light it, highest `order` first
	const std::unordered_map<std::string, std::vector<Hand>>& Hands();
	// 🔥 the light records a spray makes, which RE::Light lights through our `isPluginLight` configs (Truman's route)
	const std::unordered_set<RE::FormID>& SprayLightRecords();

	// ------------------------------------------------------------------ HandLights.cpp: lights on the caster's hands
	void        MakeHandLights();                       // once, after the data and the settings are read
	void        RefindHandLights();                     // when a save loads: another plugin may have set casting art since
	void        ApplyHandLights(bool a_log);            // after any switch that decides who lights a hand
	const Hand* HandOfLight(RE::TESObjectLIGH* a_light);  // nullptr unless it is one of ours, in use
	void        DressHandLight(RE::NiLight* a_light, const Hand& a_hand);
	std::size_t HandLightsMade();
	std::size_t HandEffects();

	// ------------------------------------------------------------------ Settings.cpp: the settings file
	void  LoadSettings();
	void  SaveSettings();
	int   BrightnessPercent();
	void  SetBrightnessPercent(int a_percent);
	float Brightness();
	int   ReachPercent();
	void  SetReachPercent(int a_percent);
	float Reach();
	bool  SneakOn();
	void  SetSneakOn(bool a_on);
	bool  HandLightsOn();
	void  SetHandLightsOn(bool a_on);
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
	void        TakeStreamProjectileLights();            // once, after the data is read
	void        ClaimSprayLights();                      // once, after the data is read, BEFORE MakeHandLights
	bool        IsSprayLight(RE::TESObjectLIGH* a_light);
	void        ApplyStreamProjectileLights(bool a_log);  // after any switch
	void        NoteExplosion(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root);

	// ------------------------------------------------------------------ Menu.cpp
	void RegisterMenu();
}
