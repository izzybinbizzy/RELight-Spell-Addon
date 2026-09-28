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

	struct Option
	{
		std::string download, name, id;  // id: "Spells - Runes", the settings file's key
		std::string category, author;    // a patch's place on the Patches page (`menugroup`); author empty: listed on its own
		std::string desc;                // what it lights, broadly (`desc`), shown when the mouse is over its switch
		bool        weapons{ false };    // it came in the Weapons download (`download`): the Weapon lights setting puts it out
		int         order{ 0 };          // a later layer overrides an earlier one
		int         menu{ 0 };           // where it sits in the menu, which is NOT the override order
		bool        switchable{ false };
		bool        on{ true };
		int         brightness{ 100 };  // percent, this option's own slider on top of the global one (Settings.cpp)
		std::size_t meshes{ 0 }, bases{ 0 }, streams{ 0 };
		std::size_t lit{ 0 }, heldOut{ 0 };  // counted every frame, shown in the menu
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

	// the build's cutoff constant (0.8 * 69.99², as in gen.py and relightgen.py): cutoff = kK * fade / (reach² + size²)
	inline constexpr float kK = 3918.88f;

	[[nodiscard]] inline float CutoffFor(float a_fade, float a_reach, float a_size) noexcept
	{
		return std::clamp(kK * a_fade / (a_reach * a_reach + a_size * a_size), 0.01f, 0.99f);
	}

	// Community Shaders' inverse square lighting reads a flag and the cutoff from the two words before a light's
	// colour - the words RE::Light's `Overlay` writes. Without Community Shaders those words are ambient colour.
	namespace Isl
	{
		inline constexpr std::uint32_t kFlag = 1u << 10;

		[[nodiscard]] inline std::uint32_t* Words(RE::NiLight* a_light) noexcept
		{
			return reinterpret_cast<std::uint32_t*>(&a_light->GetLightRuntimeData());
		}
		[[nodiscard]] inline bool  On(RE::NiLight* a_light) noexcept { return (Words(a_light)[0] & kFlag) != 0; }
		inline void                SetOn(RE::NiLight* a_light) noexcept { Words(a_light)[0] |= kFlag; }
		[[nodiscard]] inline float Cutoff(RE::NiLight* a_light) noexcept { return std::bit_cast<float>(Words(a_light)[1]); }
		inline void SetCutoff(RE::NiLight* a_light, float a_cutoff) noexcept
		{
			Words(a_light)[1] = std::bit_cast<std::uint32_t>(std::clamp(a_cutoff, 0.01f, 0.99f));
		}
	}

	void                                          LoadData();
	[[nodiscard]] std::vector<Option>&            Options();
	[[nodiscard]] const std::vector<std::size_t>& OptionsInMenuOrder();  // by `menu`, never the alphabetical file order
	[[nodiscard]] std::size_t                     DataFiles();
	[[nodiscard]] std::size_t                     OptionOf(const RE::TESForm* a_base);  // kNone when this mod does not light it
	[[nodiscard]] std::size_t                     OptionOfShader(const RE::TESEffectShader* a_shader);  // an enchantment's, by editor ID
	[[nodiscard]] bool                            OptionLit(std::size_t a_option);  // its switch is on, and Weapon lights if it is a weapon's
	[[nodiscard]] const Stream*                   StreamOf(const RE::TESForm* a_base);  // nullptr unless it is one of our streams
	[[nodiscard]] const RE::NiColor*              TintOf(const RE::TESForm* a_base);    // an art pick's colour, or nullptr
	[[nodiscard]] std::string                     MeshKey(std::string_view a_path);
	[[nodiscard]] std::string                     PathKey(std::string_view a_path);
	[[nodiscard]] const StringMap<std::vector<Hand>>& Hands();  // hand key -> the layers that light it, highest order first
	[[nodiscard]] const std::unordered_set<RE::FormID>& SprayLightRecords();  // spray light records, lit through our configs
	[[nodiscard]] const HandFx* HandFxOf(std::string_view a_key);  // nullptr: this hand does not breathe
	// nullptr unless we light this weapon in the hand: its form first, then (first person) its first-person model, then its model
	[[nodiscard]] const Stream* HeldOf(const RE::TESObjectWEAP* a_weapon, bool a_firstPerson);
	[[nodiscard]] std::size_t   OptionOfSprayLight(const RE::TESObjectLIGH* a_light);  // the option whose `spraylight` line names it

	// ------------------------------------------------------------------ HandLights.cpp: lights on the caster's hands
	void                      MakeHandLights();                        // once, after the data and the settings are read
	void                      RefindHandLights();                      // when a save loads: casting art may have changed
	void                      ApplyHandLights(bool a_log);             // after any switch that decides who lights a hand
	[[nodiscard]] const Hand* HandOfLight(const RE::TESObjectLIGH* a_light);  // nullptr unless it is one of ours, in use
	void                      DressHandLight(RE::NiLight* a_light, const Hand& a_hand);
	void                      NoteHandLight(RE::NiLight* a_light, const Hand& a_hand);  // a lit hand light, so a menu change reaches it
	[[nodiscard]] bool        HandLightHeldOut(const RE::NiLight* a_light);            // put out by a switch until the next cast
	[[nodiscard]] std::size_t HandLightsMade();
	[[nodiscard]] std::size_t HandEffects();

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
	void                SetOptionBrightness(std::size_t a_index, int a_percent);  // every file of a pack together
	[[nodiscard]] float OptionBrightness(std::size_t a_option);                    // 1.0 for kNone and for Core

	// ------------------------------------------------------------------ Options.cpp: the switches
	void                             UpdateOptionLights();                           // every frame, after the sneaking pass
	void                             CullOptionLightsUnder(RE::NiAVObject* a_root);  // on a 3D that has just loaded
	[[nodiscard]] bool               HeldOutForOption(const RE::NiLight* a_light);
	[[nodiscard]] RE::TESObjectREFR* ReferenceOf(RE::NiAVObject* a_obj);  // the reference a scene-graph object belongs to
	[[nodiscard]] std::size_t        EnchantOptionOf(const RE::NiLight* a_light);  // kNone unless it is an enchantment light of ours

	// ------------------------------------------------------------------ Brightness.cpp: our own sliders
	void UpdateBrightness(float a_delta);  // every frame, last; a_delta is the game's frame time
	void RememberLight(RE::NiLight* a_light, const HandFx* a_fx = nullptr, std::size_t a_option = kNone);

	// ------------------------------------------------------------------ Streams.cpp: lights that travel
	void                      HangStreamLights(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root);
	void                      UpdateStreamLights();
	void                      DropStreamLights(const RE::TESObjectREFR* a_ref);  // its 3D is being taken apart
	[[nodiscard]] std::size_t LiveStreamLights();
	void                      TakeStreamProjectileLights();  // once, after the data is read
	void                      ClaimSprayLights();            // once, after the data is read, BEFORE MakeHandLights
	[[nodiscard]] bool        IsSprayLight(const RE::TESObjectLIGH* a_light);
	void                      ApplyStreamProjectileLights(bool a_log);  // after any switch
	// one light made and registered the travelling-light way (ReLight's method); nullptr when it could not be
	[[nodiscard]] RE::BSLight* MakeOurLight(const Stream& a_s, const RE::NiColor& a_colour, const RE::NiPoint3& a_at, float a_fade,
		float a_reach, RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene, RE::NiPointLight*& a_made);

	// ------------------------------------------------------------------ Held.cpp: a weapon's own light while it is drawn
	void                      UpdateHeldLights();  // every frame, after the travelling lights
	[[nodiscard]] std::size_t LiveHeldLights();

	// ------------------------------------------------------------------ Wards.cpp: one ward, one dome, and its colour
	void ApplyWards(const char* a_why);  // one dome per ward and the colour pick; nothing while Dynamic Wards is loaded
	[[nodiscard]] bool WardsSteppedDown();  // Dynamic Wards is loaded: the wards are its

	// ------------------------------------------------------------------ Menu.cpp
	void RegisterMenu();
}
