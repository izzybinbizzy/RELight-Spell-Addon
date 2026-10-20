// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h and FadeDevBench.cpp are each mod's own)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// The module's bars as an add-on for Reticle Arcs (HIS ORDER 2026-10-18: "build all the reticle arc ideas and add them to
// profiles so i can test"; the author's yes: "build around his mod using his api but don't add or change any code. just
// utilize the resources he provides. color will be added when he releases that part."). Only Reticle Arcs' published
// interface is used - ReticleArcsAPI.h, Reticle Arcs by Chiktabba (https://codeberg.org/chiktabba/ReticleArcs), MIT, copied
// unchanged; Reticle Arcs draws each bar in the player's own look and asks for its state every frame. HIS ORDER 2026-10-19
// ~13:10: "completely remove our reticle options from illuminated and relight and only have reticle available through reticle
// arcs mod. no settings in our mod, we will re add the color stuff when he decides to add it to his mod." - so these bars are
// the module's only reticle, and no menu row or ini key of ours switches them (Reticle Arcs' own settings page shows / hides
// each bar). Five bars: each hand's spell light (it follows the caster's magicka), a bound weapon's time left, and each
// enchanted weapon's charge (what the charge gems show). They read the HUD's copy of the hands (Hud(), FadeLights.cpp), so
// they cost a copy and a few comparisons a frame. The ids are the same in both mods: should both be loaded, the first copy's
// bars register and the second is refused (Reticle Arcs takes one bar per id).
// THE COLOR - his ask 2026-10-19 ~18:00 with Reticle Arcs' 1.9.0 testing build ("i got the test api for the reticle arcs ...
// please adapt our plan to it"): the bars wear the color of the light the hand holds (the brightest light the fading pass
// scales for that hand, before any cooling), and with interface 2 (Reticle Arcs 1.9.0+) the module also colors Reticle Arcs'
// own spell bars that way - shown when the player picks one of its "Add-on" colors for them. The bars themselves ask only for
// interface 1, so they keep working with Reticle Arcs 1.8.

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Fade.h"

// Reticle Arcs' own header (theirs, MIT) needs GetModuleHandleW / GetProcAddress; its warnings are not ours
#include <Windows.h>
#pragma warning(push, 0)
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Weverything"
#endif
#include "ReticleArcsAPI.h"
#if defined(__clang__)
#	pragma clang diagnostic pop
#endif
#pragma warning(pop)

namespace Fade
{
	namespace
	{
		using ReticleArcsAPI::BarState;

		// a spell bar stays this long once magicka is full again (his 2026-10-11 rule: "when magicka is full it stays for a second")
		constexpr float kHoldSeconds = 1.0f;

		// how long each spell hand has been full (main thread only: Reticle Arcs calls the updates there)
		std::array<float, 2> gSinceFull{ 1.0e6f, 1.0e6f };

		// 0xRRGGBB, as Reticle Arcs takes a color
		std::uint32_t Packed(const Glow::Rgb& a_color)
		{
			const auto channel = [](float a_v) { return static_cast<std::uint32_t>(std::lround(Glow::Clamp01(a_v) * 255.0f)); };
			return channel(a_color.r) << 16 | channel(a_color.g) << 8 | channel(a_color.b);
		}

		void Show(BarState& a_state, float a_fraction, float a_low, const HudHand& a_hand)
		{
			a_state.active = true;
			a_state.drain = true;  // empties toward its start and follows the value as it falls
			a_state.percent = Glow::Clamp01(a_fraction);
			a_state.low = a_state.percent < a_low;
			a_state.alarm = a_state.percent <= 0.0f;
			a_state.useColor = a_hand.lit;  // the hand's light's color; none found: the player's color for the bar
			a_state.color = a_hand.lit ? Packed(a_hand.color) : 0;
		}

		// a spell hand's light: shown while magicka is spent, and for kHoldSeconds once it is full again
		template <std::size_t Hand>
		void SpellLight(float a_delta, BarState& a_state)
		{
			auto&       since = gSinceFull[Hand];
			const auto  hud = Hud();
			const auto& h = hud.hands[Hand];
			if (!hud.drawn || !h.shown || !h.spell) {
				since = 1.0e6f;
				return;
			}
			since = h.fraction >= 0.999f ? since + std::max(a_delta, 0.0f) : 0.0f;
			if (since <= kHoldSeconds) {
				Show(a_state, h.fraction, hud.low, h);
			}
		}

		// a bound weapon's time left (its spell's whole time, as its gem counts it); the right hand first
		void BoundTime(float, BarState& a_state)
		{
			const auto hud = Hud();
			for (const std::size_t hand : { std::size_t{ 1 }, std::size_t{ 0 } }) {
				if (const auto& h = hud.hands[hand]; hud.drawn && h.shown && h.bound) {
					Show(a_state, h.fraction, hud.low, h);
					return;
				}
			}
		}

		// an enchanted weapon's charge, once it starts dropping (no setting of ours changes that - his order of 2026-10-19; the
		// gems' Show at full charge is the gems' own)
		template <std::size_t Hand>
		void Charge(float, BarState& a_state)
		{
			const auto  hud = Hud();
			const auto& h = hud.hands[Hand];
			if (hud.drawn && h.shown && !h.spell && !h.bound && h.fraction < 0.999f) {
				Show(a_state, h.fraction, hud.low, h);
			}
		}

		void ArcReset() { gSinceFull = { 1.0e6f, 1.0e6f }; }

		// Reticle Arcs' settings page: a bar draining to empty, a moment empty, then full again (in the player's color)
		void ArcPreview(float a_time, BarState& a_state)
		{
			const float t = std::fmod(std::max(a_time, 0.0f) / 3.0f, 1.0f);
			Show(a_state, t < 0.7f ? 1.0f - t / 0.7f : 0.0f, 0.15f, HudHand{});
			a_state.active = t < 0.9f;
		}

		// interface 2: Reticle Arcs' own spell bars in the color of the light that hand holds; false (its fallback - the element's
		// or the school's color, or the player's) for the shout and the bow, and for a hand the fading pass does not follow
		bool SpellBarColor(const ReticleArcsAPI::ColorQuery& a_query, std::uint32_t& a_rgb)
		{
			if (a_query.bar != ReticleArcsAPI::Bar::kLeftSpell && a_query.bar != ReticleArcsAPI::Bar::kRightSpell) {
				return false;
			}
			const auto  hud = Hud();
			const auto& h = hud.hands[a_query.bar == ReticleArcsAPI::Bar::kLeftSpell ? 0 : 1];
			if (!h.shown || !h.spell || !h.lit) {
				return false;
			}
			a_rgb = Packed(h.color);
			return true;
		}

		struct Def
		{
			const char* id;
			const char* name;
			const char* tooltip;
			const char* placement;
			void (*update)(float, BarState&);
		};

		constexpr Def kDefs[]{
			{ "FadingSpellLightLeft", "Left hand spell light",
				"The light of the spell in your left hand, falling with your magicka and filling as it comes back.", "left", SpellLight<0> },
			{ "FadingSpellLightRight", "Right hand spell light",
				"The light of the spell in your right hand, falling with your magicka and filling as it comes back.", "right", SpellLight<1> },
			{ "FadingBoundTime", "Bound weapon time", "How long your bound weapon has left.", "bottom", BoundTime },
			{ "FadingChargeLeft", "Left weapon charge", "The charge of the enchanted weapon in your left hand.", "left", Charge<0> },
			{ "FadingChargeRight", "Right weapon charge", "The charge of the enchanted weapon in your right hand.", "right", Charge<1> },
		};
		constexpr std::size_t kArcs = std::size(kDefs);
	}

	void RegisterArcBars()
	{
		if (!OtherReticleLoaded()) {
			return;  // no ReticleArcs.dll: no reticle at all (the module draws none of its own)
		}
		// the bars need interface 1 only: asked for that, they keep working with Reticle Arcs 1.8 (asking for the header's
		// newest, 2, would give nothing there)
		auto* arcs = ReticleArcsAPI::Request(1);
		if (!arcs) {
			SKSE::log::info("Reticle Arcs is too old for add-on bars; {}'s bars are not offered to it", Mod::kName);
			return;
		}
		int taken = 0;
		for (const auto& d : kDefs) {
			ReticleArcsAPI::BarDefinition bar;
			bar.id = d.id;
			bar.name = d.name;
			bar.tooltip = d.tooltip;
			bar.placement = d.placement;
			bar.update = d.update;
			bar.reset = ArcReset;
			bar.preview = ArcPreview;
			taken += arcs->RegisterBar(bar) ? 1 : 0;
		}
		SKSE::log::info("Reticle Arcs (interface {}) took {} of {} bars{}", arcs->GetVersion(), taken, kArcs,
			taken < static_cast<int>(kArcs) ? " - the rest are refused (ReticleArcs.log says why; the other mod with this module may hold them)" : "");
		if (auto* colors = ReticleArcsAPI::Request(2)) {
			colors->SetColorProvider(SpellBarColor);  // one mod at a time: the last to set it wins (ReticleArcs.log names it)
			SKSE::log::info("Reticle Arcs' own spell bars take the hand light's color when the player picks an Add-on color for them");
		} else {
			SKSE::log::info("Reticle Arcs is older than 1.9.0: its own spell bars keep their colors (the add-on bars still wear the hand's)");
		}
	}
}
