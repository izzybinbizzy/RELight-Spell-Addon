// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// What the files share. The file map is at the top of main.cpp.

#pragma once

#include "LightKit.h"

namespace Plugin
{
	// ------------------------------------------------------------------ Data.cpp: what the installed options light
	//
	// Every layer the installer put down (Core, and each ticked option) brings one small text file into
	// Data\SKSE\Plugins\RelightSpellAddon\. The build writes them; this reads them once, when the game has loaded its
	// data. They say which objects this mod lights (the sliders scale those and nothing else), which layers are
	// tick-box options (the menu draws a switch for each), the sprays and beams whose lights this plugin hangs itself,
	// the hand lights, and the colour an art pick gives the objects it recolours.
	inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

	// std::string keys looked up by std::string_view without a temporary string
	struct StringHash
	{
		using is_transparent = void;
		[[nodiscard]] std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
	};
	template <class T>
	using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

	using LightKit::Relaxed;  // a value one thread writes and another reads (LightKit.h)

	struct Option
	{
		std::string download, name, id;  // id: "Spells - Runes", the settings file's key
		std::string category, author;    // a patch's place on the Patches page (`menugroup`); author empty: listed on its own
		std::string desc;                // what it lights, broadly (`desc`), shown when the mouse is over its switch
		bool        weapons{ false };    // it is a weapon option (`download`): the Weapon lights setting puts it out
		int         order{ 0 };          // a later layer overrides an earlier one
		int         menu{ 0 };           // where it sits in the menu, which is NOT the override order
		bool        switchable{ false };
		// its switch: flipped by the menu (render thread), read every frame and by the 3D-load hooks (loader threads)
		Relaxed<bool> on{ true };
		std::size_t   meshes{ 0 }, bases{ 0 }, streams{ 0 };
		// counted every frame on the main thread, shown in the menu (render thread)
		Relaxed<std::size_t> lit{ 0 }, heldOut{ 0 };
	};

	struct Stream
	{
		std::string               key, node;  // node "-": the light sits at the projectile's own origin
		RE::NiColor               color{ 1.0f, 1.0f, 1.0f };
		float                     fade{ 1.0f }, radius{ 133.0f }, size{ 2.5f }, cutoff{ 0.3f };
		std::vector<RE::NiPoint3> positions;  // every position the winning layer gives this mesh (a ladder)
		int                       order{ 0 };
		std::size_t               option{ kNone };
	};

	// one hand light, as one layer lights it: the layer's own light for the casting-art mesh `key`
	struct Hand
	{
		std::string  key;
		RE::NiColor  color{ 1.0f, 1.0f, 1.0f };
		std::uint8_t rgb[3]{ 255, 255, 255 };
		float        fade{ 1.0f }, radius{ 133.0f }, size{ 2.0f }, cutoff{ 0.2215f };
		bool         inverseSquare{ true }, portalStrict{ true };
		int          order{ 0 };
		std::size_t  option{ kNone };
	};

	// a hand light's Dynamic Lighting: a Pulse (breathe) or a Flicker (crackle), run on our hand light (Brightness.cpp)
	struct HandFx
	{
		bool  flicker{ false };
		float perSecond{ 0.0f }, intensity{ 0.0f };
		int   order{ 0 };
	};

	// RE::Light marks every light that moves with its owner (torches, spell lights) `fadeAmount = 4`: its flicker prevention
	// (IsLightAffectingSurface, disableLights.cpp) lets such a light reach every surface instead of only the surfaces whose
	// seven closest lights it was among when they were first counted. Our hand, held and travelling lights never pass through
	// RE::Light, so they carry the mark themselves - without it they lit only actors with flicker prevention on (a user's
	// report, 2026-10-04: "some magic doesn't work in 1st person")
	inline constexpr float kMovingLightMark = 4.0f;

	// RE::Light's marks on a light it made: its name starts "RL" (a config's light, LightManager.cpp), and an enchantment
	// light also carries fadeAmount 5 (ShaderReferenceEffect::Init; its sheathe handler reads the same two)
	[[nodiscard]] inline bool IsReLightLight(const RE::NiLight* a_light) noexcept
	{
		const char* n = a_light ? a_light->name.c_str() : nullptr;
		return n && n[0] == 'R' && n[1] == 'L';
	}
	[[nodiscard]] inline bool IsEnchantLight(const RE::NiLight* a_light) noexcept
	{
		return IsReLightLight(a_light) && a_light->fadeAmount == 5.0f;
	}

	// the build's cutoff: K * fade / (reach² + size²), clamped where Community Shaders reads one (LightKit.h)
	using LightKit::CutoffFor;
	using LightKit::kK;

	// Community Shaders' inverse square lighting reads a flag and the cutoff from two words of a light (LightKit.h has
	// how they are written). RE::Light's own test for it (the shader file), read at data load (HandLights.cpp): only
	// then are the words written
	[[nodiscard]] bool IslShader();
	namespace Isl = LightKit::Isl;

	void                                                LoadData();
	[[nodiscard]] std::vector<Option>&                  Options();
	[[nodiscard]] const std::vector<std::size_t>&       OptionsInMenuOrder();  // by `menu`, never the alphabetical file order
	[[nodiscard]] std::size_t                           DataFiles();
	[[nodiscard]] std::size_t                           OptionOf(const RE::TESForm* a_base);                  // kNone when this mod does not light it
	[[nodiscard]] std::size_t                           OptionOfShader(const RE::TESEffectShader* a_shader);  // an enchantment's, by editor ID
	[[nodiscard]] bool                                  OptionLit(std::size_t a_option);                      // its switch is on, and Weapon lights if it is a weapon's
	[[nodiscard]] const Stream*                         StreamOf(const RE::TESForm* a_base);                  // nullptr unless it is one of our streams
	[[nodiscard]] const RE::NiColor*                    TintOf(const RE::TESForm* a_base);                    // an art pick's colour, or nullptr
	[[nodiscard]] std::string                           MeshKey(std::string_view a_path);
	[[nodiscard]] std::string                           PathKey(std::string_view a_path);
	[[nodiscard]] const StringMap<std::vector<Hand>>&   Hands();                           // hand key -> the layers that light it, highest order first
	[[nodiscard]] const std::unordered_set<RE::FormID>& SprayLightRecords();               // spray light records, lit through our configs
	[[nodiscard]] const HandFx*                         HandFxOf(std::string_view a_key);  // nullptr: this hand does not breathe
	// nullptr unless we light this weapon in the hand: its form first, then (first person) its first-person model, then its model
	[[nodiscard]] const Stream* HeldOf(const RE::TESObjectWEAP* a_weapon, bool a_firstPerson);
	[[nodiscard]] std::size_t   OptionOfSprayLight(const RE::TESObjectLIGH* a_light);  // the option whose `spraylight` line names it

	// ------------------------------------------------------------------ Keep.cpp: the one place a game light is held
	using GoneLights = std::unordered_set<const RE::NiLight*>;
	void                      KeepLight(RE::NiLight* a_light);  // any thread: alive for our lists while the game has it
	void                      SweepKeptLights();                // main thread, first each frame: what left the game is forgotten, then freed
	[[nodiscard]] std::size_t KeptLights();
	void                      ForgetSpellLights(const GoneLights& a_gone);   // main.cpp (it calls Options.cpp's too)
	void                      ForgetOptionLights(const GoneLights& a_gone);  // Options.cpp: called by ForgetSpellLights only
	void                      ForgetHandLights(const GoneLights& a_gone);    // HandLights.cpp
	void                      ForgetSliderLights(const GoneLights& a_gone);  // Brightness.cpp

	// ------------------------------------------------------------------ HandLights.cpp: lights on the caster's hands
	void                      MakeHandLights();                               // once, after the data and the settings are read
	void                      RefindHandLights();                             // when a save loads: casting art may have changed
	void                      ApplyHandLights(bool a_log);                    // after any switch that decides who lights a hand
	[[nodiscard]] const Hand* HandOfLight(const RE::TESObjectLIGH* a_light);  // nullptr unless it is one of ours, in use
	void                      DressHandLight(RE::NiLight* a_light, const Hand& a_hand);
	void                      NoteHandLight(RE::NiLight* a_light, const std::string& a_key);  // a lit hand light, so a menu change reaches it (main thread)
	[[nodiscard]] bool        HandLightHeldOut(const RE::NiLight* a_light);                   // put out by a switch until the next cast
	[[nodiscard]] std::size_t HandLightsMade();
	[[nodiscard]] std::size_t HandEffects();
	[[nodiscard]] int         ElementOfHandKey(std::string_view a_key);  // the element of the effects that wear it (0 if they disagree)
	[[nodiscard]] std::size_t AutoHandEffects();                         // spells lit by an automatic hand light now

	// ------------------------------------------------------------------ Settings.cpp: the settings file
	void                LoadSettings();
	void                SaveSettings();
	[[nodiscard]] int   BrightnessPercent();
	void                SetBrightnessPercent(int a_percent);
	[[nodiscard]] float Brightness();
	[[nodiscard]] int   ReachPercent();
	void                SetReachPercent(int a_percent);
	[[nodiscard]] float Reach();
	[[nodiscard]] bool  SneakOn();
	void                SetSneakOn(bool a_on);
	[[nodiscard]] bool  HandLightsOn();
	void                SetHandLightsOn(bool a_on);
	[[nodiscard]] bool  WeaponLightsOn();
	void                SetWeaponLightsOn(bool a_on);
	[[nodiscard]] int   WardColour();  // 0 vanilla blue, 1 white (Wards.cpp)
	void                SetWardColour(int a_colour);
	void                SetOptionOn(std::size_t a_index, bool a_on);
	// Illuminated's settings, ported 2026-10-08 (his "add everything from illuminated into relight ... sister mods")
	// - never its lighting picks (his word: "Relight doesn't get vanilla or enb light just relight and the added features")
	[[nodiscard]] int  DimInDaylight();  // 0 off, 1 a little, 2 more
	void               SetDimInDaylight(int a_v);
	[[nodiscard]] int  HandLightsFor();  // 0 everyone, 1 everyone nearby, 2 player and followers, 3 player only
	void               SetHandLightsFor(int a_v);
	[[nodiscard]] bool AutoLightsOn();
	void               SetAutoLightsOn(bool a_on);

	// ------------------------------------------------------------------ Brightness.cpp: elements and their colors
	// 0 none, 1 fire, 2 frost, 3 shock - what the effect is resisted by; a projectile or explosion takes the element of the
	// effects that fire it (none if they disagree). The named colors are Dynamic Wards' preset hues, as in Illuminated.
	inline constexpr int         kNamedColorCount = LightKit::kNamedColorCount;
	inline constexpr const char* kElementNames[] = { "", "Fire", "Frost", "Shock" };
	[[nodiscard]] int            ElementOf(const RE::EffectSetting* a_effect);
	[[nodiscard]] int            ElementOfForm(const RE::TESForm* a_form);
	using LightKit::NamedColor;  // a_pick 1..kNamedColorCount (their names: Menu.cpp)

	// ------------------------------------------------------------------ Options.cpp: the switches
	// lights handed from a hook that may run on a loader thread to the main thread (an SKSE task holds them until then)
	using LightList = std::vector<RE::NiPointer<RE::NiLight>>;
	void UpdateOptionLights();  // every frame, after the sneaking pass (main thread)
	// on a 3D that has just loaded (any thread): a switched-off option's lights under it are put out at once and returned,
	// with that option in a_option, for AdoptOptionLights to take into the list on the main thread
	[[nodiscard]] LightList          CullOptionLightsUnder(RE::NiAVObject* a_root, std::size_t& a_option);
	void                             AdoptOptionLights(std::size_t a_option, const LightList& a_lights);  // main thread
	[[nodiscard]] bool               HeldOutForOption(const RE::NiLight* a_light);                        // main thread
	[[nodiscard]] RE::TESObjectREFR* ReferenceOf(RE::NiAVObject* a_obj);                                  // the reference a scene-graph object belongs to
	[[nodiscard]] std::size_t        EnchantOptionOf(const RE::NiLight* a_light);                         // kNone unless it is an enchantment light of ours

	// ------------------------------------------------------------------ Brightness.cpp: our own sliders
	void UpdateBrightness(float a_delta);  // every frame, last; a_delta is the game's frame time
	// the fading module (Fade*.cpp) wrote this light's fade: taken only when it started from our value (never scaled twice)
	void NoteFadeWrite(const RE::NiPointLight* a_light, float a_before, float a_after);
	void RememberLight(RE::NiLight* a_light, const HandFx* a_fx = nullptr, int a_element = 0);
	// what this pass gives every light of ours, for the lights another file scales (Held.cpp): the daylight factor (looked
	// at every two seconds, main thread) and the color it draws (the light's own since the color picks left, 2026-10-09)
	[[nodiscard]] float       DaylightFactor();
	[[nodiscard]] RE::NiColor DrawnColor(int a_element, const RE::NiColor& a_base);

	// ------------------------------------------------------------------ Streams.cpp: sprays, beams, breath shouts
	void               ClaimSprayLights();  // once, after the data is read, BEFORE MakeHandLights
	[[nodiscard]] bool IsSprayLight(const RE::TESObjectLIGH* a_light);
	// the light RE::Light (or the game) makes for a projectile a `stream` line names: ours for the sliders
	[[nodiscard]] bool IsStreamObject(const RE::TESObjectREFR* a_ref);
	// one light made and registered the ReLight way, on the main thread (Held.cpp); nullptr when it could not be
	[[nodiscard]] RE::BSLight* MakeOurLight(const Stream& a_s, const RE::NiColor& a_colour, const RE::NiPoint3& a_at, float a_fade,
		float a_reach, RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene, RE::NiPointLight*& a_made);

	// ------------------------------------------------------------------ Held.cpp: a weapon's own light while it is drawn
	void                      UpdateHeldLights();  // every frame, after the travelling lights
	[[nodiscard]] std::size_t LiveHeldLights();

	// ------------------------------------------------------------------ Wards.cpp: one ward, one dome, and its colour
	void               ApplyWards(const char* a_why);  // one dome per ward and the colour pick; nothing while Dynamic Wards is loaded
	[[nodiscard]] bool WardsSteppedDown();             // Dynamic Wards is loaded: the wards are its

	// ------------------------------------------------------------------ VaerSwirls.cpp: VAER Reborn's swirl on Thaumaturgy's effects
	void VaerSwirls();  // once, after the data is read: Thaumaturgy's nine effect copies wear VAER's swirl art and shader

	// ------------------------------------------------------------------ Menu.cpp
	void RegisterMenu();
}
