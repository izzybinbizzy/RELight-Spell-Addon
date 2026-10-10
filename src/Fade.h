// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// What the files share. The file map is at the top of FadeMain.cpp.

#pragma once

#include "FadeGlow.h"
#include "FadeSettingsText.h"

namespace Fade
{
	// std::string keys looked up by std::string_view without a temporary string
	struct StringHash
	{
		using is_transparent = void;
		[[nodiscard]] std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
	};
	template <class T>
	using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

	[[nodiscard]] std::string Lower(std::string_view a_text);

	// A path as UTF-8 text. path::string() converts to the ANSI code page on Windows and THROWS for a name that code
	// page cannot show (a Japanese file name on a Western system); this cannot.
	[[nodiscard]] inline std::string PathText(const std::filesystem::path& a_path)
	{
		const auto u = a_path.generic_u8string();
		return std::string(u.begin(), u.end());
	}

	// ------------------------------------------------------------------ FadeSettings.cpp: the settings (SettingsText.h)
	// The one shared copy lives in FadeSettings.cpp behind a lock: the menu (render thread) and DevBench (its own thread)
	// change it while the main thread reads it every frame, so everyone works on a copy. The two switches the per-call hooks
	// read (the HUD charge bar, every effect update) are kept beside it as atomics, so those hooks never lock or copy.
	[[nodiscard]] Settings Config();                                           // a copy, safe from any thread
	void                   SetConfig(const Settings& a_settings);              // replaces it whole; live on the next frame
	bool                   ApplySetting(std::string_view a_key, int a_value);  // one Fading.ini line; false if unknown
	[[nodiscard]] bool     DebugLogOn() noexcept;                              // lock-free
	[[nodiscard]] bool     HideChargeBarOn() noexcept;                         // lock-free
	void                   LoadSettings();
	void                   SaveSettings();

	// the Debug page's preview: never saved. The menu (or DevBench) sets it; the next frame reads it.
	struct Preview
	{
		std::atomic<bool>  on{ false };
		std::atomic<float> fraction{ 0.5f };                // every tracked hand shows this charge instead of its own
		std::atomic<bool>  pulse{ false }, flare{ false };  // one-shot: start a pulse / flare on every tracked hand
	};
	[[nodiscard]] Preview& PreviewState();

	// ------------------------------------------------------------------ FadeEditorIDs.cpp: names the game throws away
	void                      RememberEditorID(const RE::TESForm* a_form, const char* a_id);
	void                      InstallEditorIDHooks();               // weapons, enchantments, magic effects: before the game reads its plugins
	void                      ForgetPassEditorIDs();                // after data load: only what a rule file can name is kept
	[[nodiscard]] std::string EditorID(const RE::TESForm* a_form);  // "" when unknown (a copy: the table may be rewritten)
	[[nodiscard]] std::string Label(const RE::TESForm* a_form);     // "Name [EditorID] Plugin.esp|0x001234" for the log

	// the hook RecordEditorIDs<T> installs: SetFormEditorID (vtable slot 0x33) on T, recording the ID, then the game's own
	template <class T>
	struct EditorIDRecorder
	{
		static bool thunk(RE::TESForm* a_this, const char* a_id)
		{
			RememberEditorID(a_this, a_id);
			return func(a_this, a_id);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline bool                             installed{ false };  // SKSE's post-load message, main thread
	};

	// the editor IDs of form type T recorded as they load; each type hooked once, whoever asks
	template <class T>
	void RecordEditorIDs()
	{
		if (!std::exchange(EditorIDRecorder<T>::installed, true)) {
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			EditorIDRecorder<T>::func = vtbl.write_vfunc(0x33, EditorIDRecorder<T>::thunk);
		}
	}

	// ------------------------------------------------------------------ FadeRules.cpp: rule files
	void                                   LoadRules();  // at data load, after the settings
	[[nodiscard]] Verdict                  Judge(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_ench, const Settings& a_settings);
	void                                   ForgetRuleMatches();  // a game loads: the forms made in play are new ones
	[[nodiscard]] std::uint32_t            RulesGeneration();    // changes whenever the rule files are read again
	[[nodiscard]] std::size_t              RuleCount();
	[[nodiscard]] std::size_t              RuleFileCount();
	[[nodiscard]] std::vector<std::string> RuleProblems();  // a copy: the menu reads it while a reload may run

	// ------------------------------------------------------------------ FadeSpells.cpp: spells in hand
	[[nodiscard]] Kind KindOf(const RE::MagicItem* a_item);  // its strongest effect's kind; Element::kOther, no school, for none
	// a soul trap effect: the Soul Trap archetype, or - as every vanilla one is - a Script effect whose editor ID says so
	[[nodiscard]] bool IsSoulTrap(const RE::EffectSetting* a_effect);
	// this frame, every tracked actor's spell hands: their lights and glow follow the caster's magicka (from UpdateHands,
	// under its lock)
	struct SpellHand
	{
		const RE::SpellItem* spell{ nullptr };
		float                fraction{ 1.0f };
		float                current{ 0.0f }, max{ 0.0f };  // magicka
		RE::NiAVObject*      nodes[2]{};                    // the hand's magic node, third and first person
		RE::NiPointLight*    casterLight{ nullptr };        // the game's casting light (the effect's hand light)
	};
	[[nodiscard]] bool ReadSpellHand(RE::Actor* a_actor, bool a_left, SpellHand& a_out);

	// ------------------------------------------------------------------ FadeCharge.cpp: what a hand holds
	struct Reading
	{
		const RE::TESObjectWEAP*   weapon{ nullptr };
		const RE::EnchantmentItem* ench{ nullptr };
		const RE::ExtraDataList*   instance{ nullptr };  // which copy of the weapon: a swap to an identical one is still a swap
		bool                       tracked{ false };     // false: nothing of ours to do with this hand
		bool                       bound{ false };
		float                      fraction{ 1.0f };
		float                      current{ 0.0f }, max{ 0.0f };  // charge points, or seconds for a bound weapon
	};

	[[nodiscard]] Reading ReadHand(RE::Actor* a_actor, bool a_left);

	// ------------------------------------------------------------------ FadeLights.cpp: the lights on a weapon
	void UpdateHands(float a_delta);                                         // every frame, from the player's update
	void AfterReferenceEffect(RE::ReferenceEffect* a_effect, bool a_own3D);  // after the effect's own update; a_own3D: search
																			 // the effect's own model too
	void AfterShaderEffect(RE::ShaderReferenceEffect* a_effect);             // after the game animates an enchantment's shader
	void ReapplyAll();                                                       // after the cell's light animation: this frame's numbers again
	void Rebase(RE::NiPointLight* a_light, float a_ratio, float a_now);      // the plugin's Brightness moved a steady light
	void ReleaseAll();                                                       // every light back as we found it
	// Lights by Item wrote this light (FadeItems.cpp, under gLock): the fading pass and Brightness keep their own base
	void NoteItemWrite(RE::NiPointLight* a_light, float a_fadeBefore, float a_fadeAfter, const RE::NiColor& a_colorBefore,
		const RE::NiColor& a_colorAfter);

	struct HandView
	{
		std::string actor, weapon, enchantment, why;
		bool        left{ false }, bound{ false }, exempt{ false }, spell{ false }, player{ false };
		float       fraction{ 1.0f }, current{ 0.0f }, max{ 0.0f };
		float       brightness{ 1.0f }, reach{ 1.0f }, cool{ 0.0f };
		std::size_t lights{ 0 }, roots{ 0 };
		float       chargeAV{ -1.0f };  // the game's own RightItemCharge / LeftItemCharge, to compare
	};
	[[nodiscard]] std::vector<HandView> Snapshot();  // the Debug page and DevBench (takes the per-frame lock)
	struct LightNow
	{
		float fade{ 0.0f }, base{ 0.0f }, radius{ 0.0f };
		bool  frozen{ false };
	};
	[[nodiscard]] std::vector<LightNow>                LightsNow();  // every light being scaled: what it held after our last write, and its base
	[[nodiscard]] std::size_t                          ScaledLightCount();
	[[nodiscard]] std::size_t                          DimmedGlowCount();  // enchantment shaders and art swirls being dimmed
	[[nodiscard]] std::vector<std::pair<float, float>> ArtNow();           // each art mesh's emissive scale: its base, and what we wrote
	[[nodiscard]] std::size_t                          FrozenLightCount();

	// The HUD (the charge gems and the reticle), published once a frame at the end of UpdateHands: the render thread draws
	// from this small copy and never waits on the per-frame pass. [0] is the player's left hand, [1] the right.
	struct HudHand
	{
		bool      shown{ false };  // a tracked spell or weapon in that hand that fades (not left alone)
		bool      spell{ false }, bound{ false };
		float     fraction{ 1.0f };
		Glow::Rgb color{ 0.32f, 0.58f, 1.0f };  // a spell's own light at full (his ask 2026-10-10: the reticle in the spell's color)
		int       element{ -1 };                // Element of a spell's strongest effect, -1 none (the Crossfire bar's sigil)
		int       school{ -1 };                 // 0 Destruction .. 4 Illusion, -1 none
		int       skill{ 0 };                   // the player's level in that school
	};
	struct HudState
	{
		bool                   gems{ false }, reticle{ false };  // the two switches (and the module is on)
		bool                   drawn{ false };                   // the player's weapons or spells are out
		int                    reticleStyle{ 0 };                // 0 the bars beside the crosshair, 1 Crossfire's bar
		float                  reticleSize{ 1.0f }, reticleOpacity{ 0.85f };
		float                  gemSize{ 1.0f }, gemOpacity{ 0.9f };
		bool                   gemPercent{ true };
		float                  low{ 0.15f };  // the sputter point: under it the reticle turns ember
		std::array<HudHand, 2> hands{};
	};
	[[nodiscard]] HudState Hud();
	// a game menu is open (inventory, map, dialogue, the console ...): the HUD elements hide, as the game's own HUD does
	// (his report 2026-10-10). Kept by a menu open / close watcher (FadeMenu.cpp), read by the render thread
	[[nodiscard]] bool MenusHideHud() noexcept;

	// what the API hands other plugins: the charge fraction and the multiplier on this hand's lights; false if untracked
	[[nodiscard]] bool Query(RE::Actor* a_actor, bool a_left, float& a_fraction, float& a_brightness);

	// ------------------------------------------------------------------ FadeOwnLight.cpp: our own light, on a weapon no mod lights
	// (made only where FadeConfig.h offers it - Mod::kOwnLight; elsewhere these do nothing)
	inline constexpr const char* kOwnLightName = Mod::kOwnLightName;
	// this frame, the hand `a_key` wants its own light on `a_model` (nullptr: it wants none): hung (in `a_ench`'s color),
	// moved or dropped. Returns the light when one was hung just now (so it can take this frame's numbers), else nullptr
	RE::NiPointLight* KeepOwnLight(std::uint64_t a_key, RE::NiAVObject* a_model, const RE::EnchantmentItem* a_ench);
	// the colour of the spell's own light at this hand (see FadeLights.cpp OwnLightHint): our light takes its hue
	void                      TintOwnLight(std::uint64_t a_key, const RE::NiColor& a_color);
	void                      SweepOwnLights();  // after every hand was seen this frame: the lights of hands that did not ask go
	void                      DropOwnLights();   // every one of them (switched off, a load)
	[[nodiscard]] std::size_t OwnLightCount();
	[[nodiscard]] const char* OwnLightLighting();  // the lighting our light is drawn for: Community Shaders, ENB or Vanilla

	// ------------------------------------------------------------------ FadeTuning.cpp: the all-in-one advanced settings file
	// (his rule 2026-10-10: every value worth tuning that the menu does not show, one file, a section per category). A value is
	// registered with its default and range before Load (data load); Load reads the file, then writes it back whole.
	namespace Tuning
	{
		void Register(std::string_view a_section, std::string_view a_key, float a_default, float a_lo, float a_hi, std::string_view a_note,
			std::atomic<float>& a_value);
		void Load();
		// the fading module's own: [Fading] (Glow::Tuning's values the menu does not show) and [HUD] (where the HUD sits)
		inline std::atomic<float>  gMinReach{ 0.35f }, gPulseSeconds{ 0.30f }, gFlareSeconds{ 0.70f }, gFallSeconds{ 0.15f }, gRiseSeconds{ 0.35f };
		inline std::atomic<float>  gGemDistance{ 215.0f }, gGemHeight{ 62.0f }, gReticleGap{ 30.0f }, gReticleLength{ 56.0f };
		inline std::atomic<float>  gBarWidth{ 300.0f }, gBarDrop{ 70.0f }, gEmptyPulseSpeed{ 2.6f }, gFullPulseSeconds{ 0.9f };
		[[nodiscard]] inline float Get(const std::atomic<float>& a_v) noexcept { return a_v.load(std::memory_order_relaxed); }
	}

	// ------------------------------------------------------------------ FadeItems.cpp: Lights by Item (his order 2026-10-10)
	namespace Items
	{
		void           BuildCatalog();                                           // data load, main thread: every spell, staff and enchanted / bound weapon, for the search
		void           Load();                                                   // data load: the choices in Mod::kItemsPath
		void           Apply();                                                  // every frame, main thread, last in the fading pass: each choice onto the lights at its hand
		void           Reapply();                                                // under gLock, after ReapplyAll wrote the lights again: this frame's choices once more
		bool           SetFromText(RE::FormID a_id, std::string_view a_choice);  // "auto", "off" or "r,g,b", as the menu would
		void           Release();                                                // every light back as its owner left it (a load)
		void __stdcall Render();                                                 // the page
	}

	// ------------------------------------------------------------------ FadeMenu.cpp
	void RegisterMenu();

	// ------------------------------------------------------------------ FadeDevBench.cpp
	void OfferToDevBench();
	void RulesLoaded();

	// ------------------------------------------------------------------ FadeMain.cpp: called by the plugin's main.cpp
	void OnPluginLoad();  // SKSE's post-load (every plugin loaded, the game's data not read yet)
	void OnPostLoad();
	// Reticle Arcs is loaded: our reticle stays off beside it, everything else works (his order 2026-10-08 ~22:50).
	// Read once at OnPluginLoad; any thread may ask
	[[nodiscard]] bool OtherReticleLoaded();
	void               OnDataLoaded();
	void               OnGameLoading();  // a save is about to load, or a new game starts
}
