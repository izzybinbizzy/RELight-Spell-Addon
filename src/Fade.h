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
		bool  shown{ false };  // a tracked spell or weapon in that hand that fades (not left alone)
		bool  spell{ false }, bound{ false };
		float fraction{ 1.0f };
	};
	struct HudState
	{
		bool                   gems{ false }, reticle{ false };  // the two switches (and the module is on)
		bool                   drawn{ false };                   // the player's weapons or spells are out
		float                  reticleSize{ 1.0f }, reticleOpacity{ 0.85f };
		float                  gemSize{ 1.0f }, gemOpacity{ 0.9f };
		bool                   gemPercent{ true };
		float                  low{ 0.15f };  // the sputter point: under it the reticle turns ember
		std::array<HudHand, 2> hands{};
	};
	[[nodiscard]] HudState Hud();

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
