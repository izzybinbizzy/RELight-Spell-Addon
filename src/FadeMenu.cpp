// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Two pages in SKSE Menu Framework's Mod Control Panel, under the Spell Addon's section. Settings: every setting of the
// module (his rule: settings live in the SKSE menu only), saved at once and live on the next frame; the rule files pick
// weapons one by one. Debug: the debug log,
// each tracked hand's weapon, charge, the numbers on its lights and how many lights were found, plus the rule files and
// their problems - what a bug report needs.
// The look is the shared MenuStyle.h in the Spell Addon's gold, with its glowing headings; an optional HUD element
// shows one charge gem per hand of the player (HudGems).

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Fade.h"

#include "SKSEMenuFramework.h"
#include "Translation.h"
#include "MenuStyle.h"

namespace Fade
{
	namespace
	{
		constexpr ImGuiMCP::ImVec4 kGold{ 1.0f, 0.86f, 0.55f, 1.0f };
		constexpr ImGuiMCP::ImVec4 kEmber{ 0.93f, 0.72f, 0.45f, 0.85f };
		constexpr ImGuiMCP::ImVec4 kDim{ 0.62f, 0.58f, 0.52f, 1.0f };
		using Translation::T;
#define TR_MARK(x) x  // a line Translation.json carries, though it is not written inside T( ) here

		class GlowStyle
		{
		public:
			GlowStyle()
			{
				using namespace ImGuiMCP;
				PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.12f, 0.10f, 0.08f, 0.75f });
				PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{ 0.30f, 0.21f, 0.10f, 0.75f });
				PushStyleColor(ImGuiCol_Separator, ImVec4{ 1.0f, 0.78f, 0.45f, 0.22f });
			}
			~GlowStyle() { ImGuiMCP::PopStyleColor(3); }
			GlowStyle(const GlowStyle&) = delete;
			GlowStyle& operator=(const GlowStyle&) = delete;

		private:
			MenuStyle::Page page;  // the shared accent, rounding and hovers
		};
		namespace Icon = MenuStyle::Icon;

		void GlowHeading(unsigned a_icon, const char* a_text)
		{
			using namespace ImGuiMCP;
			Spacing();
			auto*        dl = GetWindowDrawList();
			const ImVec2 at = GetCursorScreenPos();
			const float  w = GetContentRegionAvail().x;
			const float  h = GetTextLineHeight() + 6.0f;
			ImDrawListManager::AddRectFilledMultiColor(dl, at, ImVec2{ at.x + w, at.y + h }, IM_COL32(255, 186, 90, 46),
				IM_COL32(255, 186, 90, 0), IM_COL32(255, 186, 90, 0), IM_COL32(255, 186, 90, 46));
			ImDrawListManager::AddLine(dl, ImVec2{ at.x, at.y + h }, ImVec2{ at.x + w * 0.55f, at.y + h }, IM_COL32(255, 205, 120, 110), 1.0f);
			Dummy(ImVec2{ 0.0f, 3.0f });
			Text(" ");
			SameLine(0.0f, 0.0f);
			FontAwesome::PushSolid();
			TextColored(kGold, "%s", FontAwesome::UnicodeToUtf8(a_icon).c_str());
			FontAwesome::Pop();
			SameLine();
			TextColored(kGold, "%s", T(a_text));
			Dummy(ImVec2{ 0.0f, 4.0f });
		}

		void Tip(const char* a_text) { ImGuiMCP::SetItemTooltip("%s", T(a_text)); }

		// The settings page edits a copy of the settings (Config), which goes back whole when anything changed (SetConfig):
		// the main thread reads them every frame, so the shared copy is never written half-way. A switch or a choice saves
		// the file at once; a slider when it is let go.
		void Toggle(const char* a_label, bool& a_value, const char* a_tip, bool& a_save)
		{
			a_save |= ImGuiMCP::Checkbox(T(a_label), &a_value);
			Tip(a_tip);
		}

		// a percent slider over a 0..1 (or more) float
		void Percent(const char* a_label, float& a_value, int a_lo, int a_hi, const char* a_tip, bool& a_save)
		{
			int v = static_cast<int>(std::lround(a_value * 100.0f));
			if (ImGuiMCP::SliderInt(T(a_label), &v, a_lo, a_hi, "%d%%")) {
				a_value = static_cast<float>(std::clamp(v, a_lo, a_hi)) / 100.0f;
			}
			a_save |= ImGuiMCP::IsItemDeactivatedAfterEdit();
			Tip(a_tip);
		}

		template <class E>
		void Choice(const char* a_label, E& a_value, const char* const* a_items, int a_count, const char* a_tip, bool& a_save)
		{
			int                      v = static_cast<int>(a_value);
			std::vector<const char*> items;
			for (int i = 0; i < a_count; ++i) {
				items.push_back(T(a_items[i]));
			}
			if (ImGuiMCP::Combo(T(a_label), &v, items.data(), a_count)) {
				a_value = static_cast<E>(std::clamp(v, 0, a_count - 1));
				a_save = true;
			}
			Tip(a_tip);
		}

		void __stdcall RenderSettings()
		{
			const GlowStyle style;
			const Settings  before = Config();
			Settings        s = before;
			auto&           t = s.tuning;
			bool            save = false;
			const float     column = ImGuiMCP::GetContentRegionAvail().x * 0.5f;  // where a paired switch starts
			ImGuiMCP::PushItemWidth(column * 0.9f);

			Toggle("Enabled", s.enabled, "Off: every weapon light and glow is put back exactly as the other mods set it.", save);
			ImGuiMCP::BeginDisabled(!s.enabled);

			GlowHeading(Icon::kSun, "Fading");
			Percent("Brightness when empty", t.floor, 0, 50,
				"What is left of the light at 0% charge. 0% puts it out; 10% (the default) keeps a faint glow so you can "
				"tell the weapon is enchanted but spent.",
				save);
			{
				static const char* const kCurves[] = { TR_MARK("Linear"), TR_MARK("Gentle - stays bright longer") };
				Choice("Curve", t.curve, kCurves, 2, "How the light falls as the charge falls. Gentle holds most of its brightness until the charge is low.",
					save);
			}
			Percent("Reach follows", t.reachFollows, 0, 100,
				"How much of the fade the light's reach follows too. 0%: only the brightness fades; 100%: the reach shrinks just as much.",
				save);
			Toggle("Dim the enchantment glow too", s.dimShader,
				"The glow on the blade (the enchantment's shader and its art, VAER's swirls among them) fades with the charge, "
				"like the lights.",
				save);

			GlowHeading(Icon::kMoon, "Nearly empty");
			Toggle("Sputter", t.sputter, "Below the level set here, the light sputters: short, irregular dips, deeper toward empty.", save);
			ImGuiMCP::BeginDisabled(!t.sputter);
			Percent("Sputter below", t.sputterBelow, 1, 50, "The charge level where the sputter starts.", save);
			Percent("Sputter strength", t.sputterStrength, 0, 100, "How deep the deepest dip goes, at empty.", save);
			ImGuiMCP::EndDisabled();
			Toggle("Steady at empty", t.emptySteady, "At exactly 0% the light stops sputtering and holds still.", save);
			Toggle("Color cooling", t.cool, "Below the sputter level the light's color drains toward grey or a dull ember.", save);
			ImGuiMCP::BeginDisabled(!t.cool);
			{
				static const char* const kTints[] = { TR_MARK("Grey"), TR_MARK("Ember") };
				Choice("Cools toward", t.coolTint, kTints, 2, "Grey: the color drains out. Ember: it turns a dull orange, like a dying fire.", save);
			}
			Percent("Cooling amount", t.coolAmount, 0, 100, "How far the color goes toward grey or ember at empty.", save);
			ImGuiMCP::EndDisabled();

			GlowHeading(Icon::kBolt, "Moments");
			Toggle("Hit pulse", t.pulse, "A quick flash when a hit spends charge.", save);
			ImGuiMCP::SameLine(column);
			Toggle("Recharge flare", t.flare, "When a soul gem refills the weapon, the light swells past full and settles.", save);
			ImGuiMCP::BeginDisabled(!t.pulse);
			Percent("Pulse strength", t.pulseStrength, 0, 200, "How bright the hit pulse flashes.", save);
			ImGuiMCP::EndDisabled();
			ImGuiMCP::BeginDisabled(!t.flare);
			Percent("Flare strength", t.flareStrength, 0, 200, "How far past full the recharge flare swells.", save);
			ImGuiMCP::EndDisabled();

			GlowHeading(Icon::kWand, "What fades");
			Toggle("Weapon enchantments", s.weapons, "An enchanted weapon's lights and glow follow its charge.", save);
			ImGuiMCP::SameLine(column);
			Toggle("Staves", s.staves, "A staff's lights and glow follow its charge.", save);
			Toggle("Spells", s.spells,
				"A spell in hand: the lights and glow on the casting hand follow your magicka, dimming as it runs low and "
				"coming back as it refills.",
				save);
			Toggle("Bound weapons", s.bound,
				"A bound weapon has no charge: its light stays full, then fades over the last seconds of its spell.", save);
			ImGuiMCP::BeginDisabled(!s.bound);
			{
				int secs = static_cast<int>(s.boundFadeSeconds);
				if (ImGuiMCP::SliderInt(T("Bound fade"), &secs, 1, 60, T("last %d s"))) {
					s.boundFadeSeconds = static_cast<float>(std::clamp(secs, 1, 60));
				}
				save |= ImGuiMCP::IsItemDeactivatedAfterEdit();
				Tip("Over how many of the spell's last seconds a bound weapon's light fades.");
			}
			ImGuiMCP::EndDisabled();
			{
				static const char* const kWho[] = { TR_MARK("Player"), TR_MARK("Player and followers") };
				Choice("Whose", s.who, kWho, 2,
					"Followers' weapons usually never lose charge in the base game, so theirs stay full unless another mod "
					"makes them spend it. Their spells follow their magicka.",
					save);
			}

			GlowHeading(Icon::kBook, "By kind of magic");
			ImGuiMCP::TextDisabled("%s", T("By the strongest effect of an enchantment or a spell. Off: that kind keeps its full light."));
			{
				static const char* const kElementLabels[] = { TR_MARK("Fire"), TR_MARK("Frost"), TR_MARK("Shock"), TR_MARK("Absorb"),
					TR_MARK("Soul trap"), TR_MARK("Paralyze"), TR_MARK("Fear, turn undead, banish"), TR_MARK("Other effects") };
				static const char* const kSchoolLabels[] = { TR_MARK("Destruction"), TR_MARK("Restoration"), TR_MARK("Conjuration"),
					TR_MARK("Alteration"), TR_MARK("Illusion") };
				for (std::size_t i = 0; i < s.elements.size(); ++i) {
					if (i % 2) {
						ImGuiMCP::SameLine(column);
					}
					Toggle(kElementLabels[i], s.elements[i], "Effects of this kind fade.", save);
				}
				for (std::size_t i = 0; i < s.schools.size(); ++i) {
					if (i % 2) {
						ImGuiMCP::SameLine(column);
					}
					Toggle(kSchoolLabels[i], s.schools[i], "Magic of this school fades (an effect of no school is never held back here).", save);
				}
			}

			GlowHeading(Icon::kDisplay, "HUD");
			Toggle("Hide the charge bar", s.hideChargeBar,
				"The HUD's enchantment charge bar is hidden: the weapon's light shows the charge instead. Works with the "
				"vanilla HUD, SkyHUD and TrueHUD.",
				save);
			Toggle("Charge gems on the HUD", s.hudGems,
				"A small glowing gem for each hand, bottom right: full while the weapon is charged (or your magicka is full), "
				"dimming and emptying with it. Handy with the charge bar hidden.",
				save);

			ImGuiMCP::EndDisabled();
			ImGuiMCP::PopItemWidth();
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled("%s", T("Weapons can also be picked one by one in the rule files, Data\\SKSE\\Plugins\\RelightSpellAddon\\Fading."));

			// only what changed on this page goes back, key by key: a change DevBench made meanwhile is kept
			for (const auto& k : SettingsText::kKeys) {
				if (k.get(s) != k.get(before)) {
					ApplySetting(k.name, k.get(s));
				}
			}
			if (save) {
				SaveSettings();
			}
		}

		void __stdcall RenderDebug()
		{
			const GlowStyle style;
			const auto      hands = Snapshot();
			{
				Settings s = Config();
				bool     save = false;
				Toggle("Debug log", s.debugLog, "Writes each tracked weapon, rule match, pulse and flare to RelightSpellAddon.log.", save);
				if (save) {
					ApplySetting("DebugLog", s.debugLog ? 1 : 0);
					SaveSettings();
				}
			}

			GlowHeading(Icon::kEye, "Preview");
			auto& preview = PreviewState();
			bool  on = preview.on;
			if (ImGuiMCP::Checkbox(T("Pretend the charge is"), &on)) {
				preview.on = on;
			}
			Tip("Every tracked weapon shows this charge instead of its own, to see the fade, sputter and cooling without "
				"fighting. Not saved: it switches off when the game restarts.");
			ImGuiMCP::SameLine();
			{
				int v = static_cast<int>(std::lround(preview.fraction.load() * 100.0f));
				ImGuiMCP::SetNextItemWidth(180.0f);
				if (ImGuiMCP::SliderInt("##previewCharge", &v, 0, 100, "%d%%")) {
					preview.fraction = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f;
				}
			}
			if (ImGuiMCP::Button(T("Pulse"))) {
				preview.pulse = true;
			}
			Tip("The flash a spending hit makes, now.");
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(T("Flare"))) {
				preview.flare = true;
			}
			Tip("The swell a recharge makes, now.");

			GlowHeading(Icon::kHand, "Hands");
			if (hands.empty()) {
				ImGuiMCP::TextDisabled("%s", T("No enchanted or bound weapon, and no spell, in hand right now."));
			}
			for (std::size_t i = 0; i < hands.size(); ++i) {
				const auto& h = hands[i];
				ImGuiMCP::PushID(static_cast<int>(i));
				ImGuiMCP::TextColored(kEmber, T("%s - %s hand"), h.actor.c_str(), h.left ? T("left") : T("right"));
				ImGuiMCP::Indent();
				ImGuiMCP::Text("%s", h.weapon.c_str());
				ImGuiMCP::TextDisabled("%s", h.enchantment.c_str());
				if (h.exempt) {
					ImGuiMCP::Text(T("left alone - %s"), h.why.c_str());
				} else {
					if (h.spell) {
						ImGuiMCP::Text(T("magicka %.0f / %.0f - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					} else if (h.bound) {
						ImGuiMCP::Text(T("%.1f s of %.0f s left - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					} else {
						ImGuiMCP::Text(T("charge %.0f / %.0f - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					}
					ImGuiMCP::ProgressBar(h.fraction, ImGuiMCP::ImVec2{ -1.0f, 0.0f }, "");
					ImGuiMCP::Text(T("brightness x%.2f   reach x%.2f   cooling %.0f%%"), h.brightness, h.reach, h.cool * 100.0f);
					ImGuiMCP::TextColored(h.lights ? kDim : kGold, T("%zu light(s) under %zu root(s)%s"), h.lights, h.roots,
						h.lights ? "" : T(" - nothing to dim: no lighting mod lights this weapon?"));
					ImGuiMCP::TextDisabled(T("decided by: %s"), h.why.c_str());
				}
				if (h.chargeAV >= 0.0f && !h.bound && !h.spell) {
					ImGuiMCP::TextDisabled(T("the game's own %s actor value: %.1f"), h.left ? "LeftItemCharge" : "RightItemCharge", h.chargeAV);
				}
				ImGuiMCP::Unindent();
				ImGuiMCP::PopID();
			}

			GlowHeading(Icon::kBulb, "Lights");
			ImGuiMCP::Text(T("%zu light(s) being scaled; %zu glow(s) dimmed"), ScaledLightCount(), DimmedGlowCount());
			if (const auto frozen = FrozenLightCount()) {
				ImGuiMCP::TextColored(kGold, T("%zu light(s) held steady: another plugin scales them the same way"), frozen);
				Tip("Two plugins each scaling the other's output would drive the light to black or white. This mod noticed "
					"and holds its base value; the light still fades, but check which other mod touches weapon lights.");
			}

			GlowHeading(Icon::kBook, "Rule files");
			ImGuiMCP::Text(T("%zu rule(s) from %zu file(s) in Data\\SKSE\\Plugins\\RelightSpellAddon\\Fading"), RuleCount(), RuleFileCount());
			if (ImGuiMCP::Button(T("Reload rule files"))) {
				// on the game's main thread, between frames: the per-frame pass reads the rules there
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { LoadRules(); });
				}
			}
			Tip("Reads the .json files again, so a rule can be edited without restarting the game.");
			for (const auto& p : RuleProblems()) {
				ImGuiMCP::TextColored(kGold, "%s", p.c_str());
			}
		}
	}

	namespace
	{
		// the HUD gems: one per tracked hand of the player, bottom right above the vanilla bars; left hand first
		void __stdcall RenderHud()
		{
			using namespace ImGuiMCP;
			const auto s = Config();
			if (!s.enabled || !s.hudGems) {
				return;
			}
			auto hands = Snapshot();
			std::erase_if(hands, [](const HandView& h) { return !h.player || h.exempt; });
			if (hands.empty()) {
				return;
			}
			std::ranges::sort(hands, {}, [](const HandView& h) { return !h.left; });
			auto*       io = GetIO();
			auto*       dl = GetForegroundDrawList();
			const float r = 11.0f, gap = 30.0f;
			const float right = io ? io->DisplaySize.x - 34.0f : 1880.0f, y = io ? io->DisplaySize.y - 200.0f : 880.0f;
			const float x0 = right - gap * static_cast<float>(hands.size() - 1);
			MenuStyle::HudPlate(dl, ImVec2(x0 - r - 8.0f, y - r - 8.0f), ImVec2(right + r + 8.0f, y + r + 8.0f));
			for (std::size_t i = 0; i < hands.size(); ++i) {
				const float  f = std::clamp(hands[i].fraction, 0.0f, 1.0f);
				const ImVec2 c(x0 + gap * static_cast<float>(i), y);
				// a dull ember when empty, warm gold when full; a soft halo grows with the charge
				const int rr = 255, gg = static_cast<int>(110 + 110 * f), bb = static_cast<int>(40 + 80 * f);
				ImDrawListManager::AddCircleFilled(dl, c, r + 6.0f * f, IM_COL32(rr, gg, bb, static_cast<int>(50 * f)), 24);
				ImDrawListManager::AddCircleFilled(dl, c, r, IM_COL32(28, 20, 12, 220), 24);
				ImDrawListManager::AddCircleFilled(dl, c, r * (0.25f + 0.75f * f), IM_COL32(rr, gg, bb, static_cast<int>(120 + 135 * f)), 24);
				ImDrawListManager::AddCircle(dl, c, r, IM_COL32(255, 205, 120, 150), 24, 1.0f);
			}
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		// the colour scheme is the Spell Addon's own (Menu.cpp sets it first): one DLL, one shared MenuStyle theme
		SKSEMenuFramework::SetSection(T("RELight - Spell Addon"));
		SKSEMenuFramework::AddSectionItem(T("Fading"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Fading - debug"), RenderDebug);
		SKSEMenuFramework::AddHudElement(RenderHud);
		SKSE::log::info("settings pages added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
