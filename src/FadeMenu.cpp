// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Pages in SKSE Menu Framework's Mod Control Panel, under the mod's own section. HUD (since 2026-10-09, his order): the
// charge bar, the charge gems and the reticle. Settings ("Fading"): every other setting of the
// module (his rule: settings live in the SKSE menu only), saved at once and live on the next frame; the rule files pick
// weapons one by one. Debug (ONLY in the testing DLL of the PDB and Source download - his call 2026-10-09: "remove fading
// debug entirely only keep it with source and pdb as an in game testing dll"; built with FADE_DEBUG_PAGE): the debug log,
// each tracked hand's weapon, charge, the numbers on its lights and how many lights were found, plus the rule files and
// their problems - what a bug report needs.
// The look is the shared MenuStyle.h with these pages' warm glowing headings; two optional HUD elements show one charge
// gem per hand of the player beside the health bar (HudGems) and the reticle beside the crosshair (Reticle): Crossfire's struggle bar stood
// on end, one per hand, draining with its charge or magicka. Both draw from Hud() (FadeLights.cpp), a small copy the
// main thread publishes once a frame, so the render thread never waits on the per-frame pass.

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Fade.h"

// SKSE Menu Framework's own header (theirs, MIT): its warnings are not ours, and ours are errors (xmake.lua)
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
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

		// only what changed on a page goes back, key by key: a change DevBench made meanwhile is kept
		void Commit(const Settings& a_before, const Settings& a_now, bool a_save)
		{
			for (const auto& k : SettingsText::kKeys) {
				if (k.get(a_now) != k.get(a_before)) {
					ApplySetting(k.name, k.get(a_now));
				}
			}
			if (a_save) {
				SaveSettings();
			}
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
			// HIS WORD 2026-10-10: "remove cools toward option in color cooling, the only option will be grey, ember cooling only
			// for fire spell types that are orange" (Glow::TintFor)
			Toggle("Color cooling", t.cool,
				"Below the sputter level the light's color drains toward grey. Fire magic with an orange light turns a dull ember "
				"instead, like a dying fire.",
				save);
			ImGuiMCP::BeginDisabled(!t.cool);
			Percent("Cooling amount", t.coolAmount, 0, 100, "How far the color goes toward grey (or ember) at empty.", save);
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

			// HIS WORD 2026-10-10: "put bound weapons above own light for unlit weapons in the what fades menu. completely remove
			// own light for unlit weapons option" - the switch is gone from the menu (the setting file's OwnLight stays as it was)
			GlowHeading(Icon::kWand, "What fades");
			Toggle("Weapon enchantments", s.weapons, "An enchanted weapon's lights and glow follow its charge.", save);
			ImGuiMCP::SameLine(column);
			Toggle("Staves", s.staves, "A staff's lights and glow follow its charge.", save);
			Toggle("Bound weapons", s.bound,
				"A bound weapon has no charge: its light stays full, then fades over the last seconds of its spell.", save);
			ImGuiMCP::SameLine(column);
			Toggle("Spells", s.spells,
				"A spell in hand: the lights and glow on the casting hand follow your magicka, dimming as it runs low and "
				"coming back as it refills.",
				save);
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
				static const char* const kWho[] = { TR_MARK("Player"), TR_MARK("Player and followers"), TR_MARK("Everyone") };
				Choice("Whose", s.who, kWho, 3,
					"Everyone: every caster near you, enemies too - their spells follow their magicka. Other characters' weapons "
					"usually never lose charge in the base game, so theirs stay full unless another mod makes them spend it.",
					save);
			}

			GlowHeading(Icon::kBook, "By Magicka Type");  // his name for it, 2026-10-10 (was "By kind of magic")
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

			ImGuiMCP::EndDisabled();
			ImGuiMCP::PopItemWidth();
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled(T("Weapons can also be picked one by one in the rule files, %s."), Mod::kRulesDirText);
			Commit(before, s, save);
		}

		// HIS MENU OF 2026-10-09 ~11:05: "make a HUD submenu placed right below configs. add logical and simple options for
		// toggling and messing with our hud stuff" - its own page (registered before Fading, right below the mod's own
		// pages): the charge bar, the charge gems (size, opacity, the percent) and the reticle (size, opacity). The gems
		// and the reticle show what the fading module tracks, so they need Fading's Enabled on.
		void __stdcall RenderHudSettings()
		{
			const GlowStyle style;
			const Settings  before = Config();
			Settings        s = before;
			bool            save = false;
			ImGuiMCP::PushItemWidth(ImGuiMCP::GetContentRegionAvail().x * 0.45f);

			GlowHeading(Icon::kGauge, "Charge bar");
			Toggle("Hide the charge bar", s.hideChargeBar,
				"The HUD's enchantment charge bar is hidden: the weapon's light shows the charge instead. Works with the "
				"vanilla HUD, SkyHUD and TrueHUD.",
				save);

			GlowHeading(Icon::kGem, "Charge gems");
			ImGuiMCP::BeginDisabled(!s.enabled);
			Toggle("Charge gems on the HUD", s.hudGems,
				"A soul gem for each enchanted weapon hand beside the health bar - the left hand's on the left, the right hand's on "
				"the right - as full as that weapon's charge. It shows once the charge starts dropping, while your weapons are out. "
				"Needs Fading on.",
				save);
			ImGuiMCP::BeginDisabled(!s.hudGems);
			Percent("Gem size", s.hudGemSize, 50, 200, "How big the gems are.", save);
			Percent("Gem opacity", s.hudGemOpacity, 10, 100, "How strongly the gems show over the game.", save);
			Toggle("Show the percent", s.hudGemPercent, "The charge in percent under each gem.", save);
			ImGuiMCP::EndDisabled();

			GlowHeading(Icon::kTarget, "Reticle");
			const bool otherReticle = OtherReticleLoaded();  // our reticle stays off beside Reticle Arcs (FadeMain.cpp)
			ImGuiMCP::BeginDisabled(otherReticle);
			Toggle("Reticle", s.reticle,
				otherReticle ? "Reticle Arcs is installed, so this reticle stays off." :
							   "For spells: it goes down as your magicka runs low and fills as it comes back, in the color of the spell in "
							   "your hand. It pulses softly when you are out of magicka and when it is full again, and turns ember near "
							   "empty. Needs Fading on (and Spells under What fades).",
				save);
			ImGuiMCP::BeginDisabled(!s.reticle);
			{
				// his pick 2026-10-10: "a hud option with our crossfire info to make the widget fill as magicka regenerates then
				// does a soft pulse when full" - a style of the reticle, not another widget
				static const char* const kStyles[] = { TR_MARK("Bars beside the crosshair"), TR_MARK("Crossfire bar") };
				Choice("Reticle style", s.reticleStyle, kStyles, 2,
					"Bars: one bar each side of the crosshair, one per hand. Crossfire bar: Crossfire's bar under the crosshair, filling "
					"as your magicka comes back, with each hand's spell sigil and its school at the ends.",
					save);
			}
			Percent("Reticle size", s.reticleSize, 50, 200, "How big the reticle's bars are.", save);
			Percent("Reticle opacity", s.reticleOpacity, 10, 100, "How strongly the reticle shows over the game.", save);
			ImGuiMCP::EndDisabled();
			ImGuiMCP::EndDisabled();
			ImGuiMCP::EndDisabled();
			ImGuiMCP::PopItemWidth();
			Commit(before, s, save);
		}

#ifdef FADE_DEBUG_PAGE
		void __stdcall RenderDebug()
		{
			const GlowStyle style;
			const auto      hands = Snapshot();
			{
				Settings s = Config();
				bool     save = false;
				Toggle("Debug log", s.debugLog, "Writes each tracked weapon, rule match, pulse and flare to the mod's log file.", save);
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
			if constexpr (Mod::kOwnLight) {
				ImGuiMCP::Text(T("%zu light(s) being scaled, %zu of them our own; %zu glow(s) dimmed"), ScaledLightCount(), OwnLightCount(),
					DimmedGlowCount());
				ImGuiMCP::Text(T("Own light drawn for: %s"), T(OwnLightLighting()));
			} else {
				ImGuiMCP::Text(T("%zu light(s) being scaled; %zu glow(s) dimmed"), ScaledLightCount(), DimmedGlowCount());
			}
			if (const auto frozen = FrozenLightCount()) {
				ImGuiMCP::TextColored(kGold, T("%zu light(s) held steady: another plugin scales them the same way"), frozen);
				Tip("Two plugins each scaling the other's output would drive the light to black or white. This mod noticed "
					"and holds its base value; the light still fades, but check which other mod touches weapon lights.");
			}

			GlowHeading(Icon::kBook, "Rule files");
			ImGuiMCP::Text(T("%zu rule(s) from %zu file(s) in %s"), RuleCount(), RuleFileCount(), Mod::kRulesDirText);
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
#endif
	}

	namespace
	{
		// a gem's outline (RenderHud, below)
		using GemPoints = std::array<ImGuiMCP::ImVec2, 6>;

		// the part of a convex gem outline below the height `a_y` (the filled part), as a convex polygon
		std::vector<ImGuiMCP::ImVec2> Below(const GemPoints& a_gem, float a_y)
		{
			std::vector<ImGuiMCP::ImVec2> out;
			for (std::size_t i = 0; i < a_gem.size(); ++i) {
				const auto& p = a_gem[i];
				const auto& q = a_gem[(i + 1) % a_gem.size()];
				const bool  pIn = p.y >= a_y, qIn = q.y >= a_y;
				if (pIn) {
					out.push_back(p);
				}
				if (pIn != qIn) {
					const float t = (a_y - p.y) / (q.y - p.y);
					out.push_back(ImGuiMCP::ImVec2{ p.x + (q.x - p.x) * t, a_y });
				}
			}
			return out;
		}

		// THE MENUS (his report 2026-10-10: "the widgets also do not turn off when opening the menus like all other widgets
		// do"). The HUD state is published from the player's update, which stops while a menu pauses the game - so the last
		// frame's state stood on screen over the inventory. A menu open / close watcher keeps the set of open game menus
		// that hide the HUD; the render thread reads one flag.
		class MenuWatch final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuWatch& Get()
			{
				static MenuWatch watch;
				return watch;
			}
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view name = a_event->menuName.c_str();
				if (std::ranges::find(kHiding, name) != std::end(kHiding)) {
					std::lock_guard lock(gate);
					if (a_event->opening) {
						open.insert(std::string(name));
					} else {
						open.erase(std::string(name));
					}
					hide.store(!open.empty(), std::memory_order_relaxed);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
			std::atomic<bool> hide{ false };

		private:
			// the game's own menus over which its HUD steps aside (a mod's always-open menu - TrueHUD's - is not one of them)
			static constexpr std::string_view kHiding[]{ "BarterMenu", "Book Menu", "Console", "Console Native UI Menu", "ContainerMenu",
				"Crafting Menu", "Creation Club Menu", "Creations Menu", "Dialogue Menu", "FavoritesMenu", "GiftMenu", "InventoryMenu",
				"Journal Menu", "Kinect Menu", "LevelUp Menu", "Loading Menu", "Lockpicking Menu", "MagicMenu", "Main Menu", "MapMenu",
				"MessageBoxMenu", "Mist Menu", "Mod Manager Menu", "Quantity Menu", "RaceSex Menu", "Sleep/Wait Menu", "StatsMenu",
				"Training Menu", "Tutorial Menu", "TweenMenu" };
			std::mutex                        gate;
			std::set<std::string>             open;
		};

		// the HUD steps aside: a hiding menu is open, or the game is paused (SKSE Menu Framework's own window pauses it)
		bool HideHud()
		{
			if (MenusHideHud()) {
				return true;
			}
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->GameIsPaused();
		}

		// THE HUD GEMS (his word 2026-10-09: each hand has its own cut soul gem on its own side of the health bar). 🔁 HIS WORD
		// 2026-10-10: "the enchantment gems should not work for spells and only show when the weapon enchantment is dropping"
		// and "remove the circle surrounding the soul gem widgets" - a gem is a WEAPON hand's (soul-gem violet for an
		// enchantment, pale blue for a bound weapon, ember under the sputter point), shown once its charge is below full, with
		// no halo. Render thread: it reads only Hud().
		void __stdcall RenderHud()
		{
			using namespace ImGuiMCP;
			static std::array<float, 2>  shown{ 0.0f, 0.0f };  // each side's fade, 0..1
			static std::array<float, 2>  fill{ 1.0f, 1.0f };
			static std::array<ImVec4, 2> tint{ ImVec4{}, ImVec4{} };
			const auto                   hud = Hud();
			auto*                        io = GetIO();
			if (!io) {
				return;
			}
			std::array<bool, 2> here{ false, false };
			if (hud.gems && hud.drawn && !HideHud()) {
				for (std::size_t i = 0; i < 2; ++i) {
					const auto& h = hud.hands[i];
					if (!h.shown || h.spell || h.fraction >= 0.995f) {
						continue;  // a spell's magicka is the reticle's; a full weapon shows nothing
					}
					here[i] = true;
					fill[i] = std::clamp(h.fraction, 0.0f, 1.0f);
					tint[i] = h.bound ? ImVec4{ 0.62f, 0.84f, 1.0f, 1.0f } : ImVec4{ 0.72f, 0.46f, 1.0f, 1.0f };
				}
			}
			const float dt = std::isfinite(io->DeltaTime) ? std::clamp(io->DeltaTime, 0.0f, 0.25f) : 0.0f;
			for (std::size_t i = 0; i < 2; ++i) {
				shown[i] = std::clamp(shown[i] + (here[i] ? dt : -dt) * 4.0f, 0.0f, 1.0f);
			}
			if (shown[0] <= 0.0f && shown[1] <= 0.0f) {
				return;
			}
			auto* dl = GetForegroundDrawList();
			if (!dl) {
				return;
			}
			// sized to the screen (1080 lines = 1), each gem beside one end of the vanilla health bar
			const float screen = std::clamp(io->DisplaySize.y / 1080.0f, 0.6f, 2.5f);
			const float k = screen * std::clamp(hud.gemSize, 0.5f, 2.0f);  // the HUD page's Gem size
			const float cx = io->DisplaySize.x * 0.5f, y = io->DisplaySize.y - Tuning::Get(Tuning::gGemHeight) * screen;
			const float hw = 11.0f * k, hh = 19.0f * k, sh = 7.0f * k;  // half width, half height, shoulder height
			for (std::size_t i = 0; i < 2; ++i) {
				if (shown[i] <= 0.0f) {
					continue;
				}
				const float     a = shown[i] * std::clamp(hud.gemOpacity, 0.1f, 1.0f);
				const auto      col = [a](float r, float g, float b, float o) { return ColorConvertFloat4ToU32(ImVec4{ r, g, b, o * a }); };
				const float     f = fill[i];
				const auto      hue = f < hud.low ? ImVec4{ 1.0f, 0.42f, 0.18f, 1.0f } : tint[i];
				const ImVec2    c{ cx + (i == 0 ? -1.0f : 1.0f) * Tuning::Get(Tuning::gGemDistance) * screen, y };
				const GemPoints gem{ ImVec2{ c.x, c.y - hh }, ImVec2{ c.x + hw, c.y - sh }, ImVec2{ c.x + hw, c.y + sh },
					ImVec2{ c.x, c.y + hh }, ImVec2{ c.x - hw, c.y + sh }, ImVec2{ c.x - hw, c.y - sh } };
				// the dark empty gem, then what is left of the charge, filled up from the bottom
				ImDrawListManager::AddConvexPolyFilled(dl, gem.data(), static_cast<int>(gem.size()), col(0.05f, 0.04f, 0.06f, 0.85f));
				const float level = c.y + hh - 2.0f * hh * f;
				if (f > 0.0f) {
					const auto part = Below(gem, level);
					if (part.size() >= 3) {
						ImDrawListManager::AddConvexPolyFilled(dl, part.data(), static_cast<int>(part.size()), col(hue.x, hue.y, hue.z, 0.85f));
					}
				}
				// the facets: the girdle and the lines from the tips, faint; the outline in bone
				const ImU32 facet = col(1.0f, 1.0f, 1.0f, 0.22f);
				ImDrawListManager::AddLine(dl, gem[5], gem[1], facet, 1.0f);
				ImDrawListManager::AddLine(dl, gem[4], gem[2], facet, 1.0f);
				ImDrawListManager::AddLine(dl, gem[0], ImVec2{ c.x, c.y - sh }, facet, 1.0f);
				ImDrawListManager::AddLine(dl, gem[3], ImVec2{ c.x, c.y + sh }, facet, 1.0f);
				ImDrawListManager::AddPolyline(dl, gem.data(), static_cast<int>(gem.size()), col(0.90f, 0.85f, 0.74f, 0.95f), ImDrawFlags_Closed,
					std::max(1.0f, 1.5f * k));
				if (!hud.gemPercent) {
					continue;
				}
				// the percent under the gem, centred
				char text[8];
				std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::lround(f * 100.0f)));
				const float tw = 7.0f * static_cast<float>(std::strlen(text));
				ImDrawListManager::AddText(dl, ImVec2{ c.x - tw * 0.5f, c.y + hh + 3.0f * k }, col(0.93f, 0.90f, 0.82f, 0.95f), text);
			}
		}

		// The reticle's two soft pulses (his word 2026-10-10: "make the reticles softly pulse when out of magicka and also when
		// fully regenerated"): out of magicka, a slow breath as long as it lasts; full again, one swell that settles.
		struct Pulse
		{
			double fullAt{ -10.0 };  // when the fill last reached full
			bool   wasFull{ true };

			// 0..1, how strongly the reticle glows this frame
			float Glow(float a_fill, double a_now)
			{
				const bool full = a_fill >= 0.995f;
				if (full && !wasFull) {
					fullAt = a_now;
				}
				wasFull = full;
				if (a_fill <= 0.02f) {
					return 0.5f + 0.5f * static_cast<float>(std::sin(a_now * Tuning::Get(Tuning::gEmptyPulseSpeed)));  // empty: breathing
				}
				const double since = a_now - fullAt;
				const double swell = Tuning::Get(Tuning::gFullPulseSeconds);
				if (since >= 0.0 && since < swell) {
					return static_cast<float>(std::sin(3.14159265 * since / swell));  // refilled: one swell
				}
				return 0.0f;
			}
		};

		// a spell's sigil for the Crossfire bar (Crossfire's DrawSigil): a ring in the spell's color with its element's mark
		void DrawSigil(ImGuiMCP::ImDrawList* a_dl, ImGuiMCP::ImVec2 a_c, float a_r, int a_element, ImGuiMCP::ImU32 a_color, float a_alpha)
		{
			using namespace ImGuiMCP;
			const ImU32 back = ColorConvertFloat4ToU32(ImVec4{ 0.0f, 0.0f, 0.0f, 0.55f * a_alpha });
			const float t = std::max(1.0f, a_r * 0.16f);
			const auto  at = [&](float x, float y) { return ImVec2{ a_c.x + x * a_r, a_c.y + y * a_r }; };
			ImDrawListManager::AddCircleFilled(a_dl, a_c, a_r, back, 32);
			ImDrawListManager::AddCircle(a_dl, a_c, a_r, a_color, 32, t);
			switch (static_cast<Element>(a_element)) {
			case Element::kFire:  // a flame: a tongue rising off a round base
				ImDrawListManager::AddCircleFilled(a_dl, at(0.0f, 0.22f), a_r * 0.3f, a_color, 16);
				ImDrawListManager::AddTriangleFilled(a_dl, at(-0.3f, 0.18f), at(0.08f, -0.62f), at(0.3f, 0.18f), a_color);
				break;
			case Element::kFrost:  // a snowflake: three crossed strokes
				for (int i = 0; i < 3; ++i) {
					const float ang = 1.5708f + static_cast<float>(i) * 1.0472f;
					ImDrawListManager::AddLine(a_dl, at(std::cos(ang) * 0.6f, -std::sin(ang) * 0.6f), at(-std::cos(ang) * 0.6f, std::sin(ang) * 0.6f), a_color, t);
				}
				break;
			case Element::kShock:
				{  // a bolt
					const ImVec2 bolt[]{ at(0.15f, -0.62f), at(-0.25f, 0.05f), at(0.08f, 0.05f), at(-0.15f, 0.62f) };
					ImDrawListManager::AddPolyline(a_dl, bolt, 4, a_color, 0, t * 1.2f);
					break;
				}
			case Element::kAbsorb:  // a ring inside the ring
				ImDrawListManager::AddCircle(a_dl, a_c, a_r * 0.45f, a_color, 24, t);
				ImDrawListManager::AddCircleFilled(a_dl, a_c, a_r * 0.15f, a_color, 12);
				break;
			default:  // anything else: a four-point star
				ImDrawListManager::AddQuadFilled(a_dl, at(0.0f, -0.6f), at(0.14f, 0.0f), at(0.0f, 0.6f), at(-0.14f, 0.0f), a_color);
				ImDrawListManager::AddQuadFilled(a_dl, at(-0.6f, 0.0f), at(0.0f, 0.14f), at(0.6f, 0.0f), at(0.0f, -0.14f), a_color);
				break;
			}
		}

		// THE RETICLE (his order 2026-10-08: "a reticule toggle ... build something from our crossfire bar"). Our own Crossfire
		// struggle bar (Illuminated Salvage\Crossfire\src\Menu.cpp, DrawStruggleBar). 🔁 HIS WORDS 2026-10-10: "vice versa for
		// reticles, they are only meant for spells", "i want the reticles to change color with spells", the two soft pulses,
		// and a Crossfire style (his pick: a style of the reticle). So it shows a SPELL hand's magicka only, in the color of
		// that spell's own light (ember under the sputter point), and
		//   Bars (style 0): the bar stood on end beside the crosshair, one per hand - the left hand left, the right right - its
		//     dark slot, the fill faint at the bottom and brightest at its top, a tick and chevron at both ends, the diamond
		//     riding the top of the fill with its breathing glow;
		//   Crossfire bar (style 1): the struggle bar itself under the crosshair, filling from the left as magicka comes back -
		//     the left hand's spell color at the left end, the right hand's at the right - each hand's sigil beyond its end and
		//     its school and skill under it, the diamond at the edge of the fill.
		// Each fades in and out, and only while her spells are drawn and no menu is open. Render thread: it reads only Hud().
		void __stdcall RenderReticle()
		{
			using namespace ImGuiMCP;
			static std::array<float, 2>  shown{ 0.0f, 0.0f };  // each side's fade, 0..1
			static std::array<float, 2>  fill{ 1.0f, 1.0f };   // each side's last fraction (kept while it fades out)
			static std::array<ImVec4, 2> tint{ ImVec4{ 0.32f, 0.58f, 1.0f, 1.0f }, ImVec4{ 0.32f, 0.58f, 1.0f, 1.0f } };
			static std::array<int, 2>    element{ -1, -1 }, school{ -1, -1 }, skill{ 0, 0 };
			static std::array<Pulse, 2>  pulse{};
			const auto                   hud = Hud();
			auto*                        io = GetIO();
			if (!io) {
				return;
			}
			std::array<bool, 2> here{ false, false };
			if (hud.reticle && hud.drawn && !HideHud()) {
				for (std::size_t i = 0; i < 2; ++i) {
					const auto& h = hud.hands[i];
					if (!h.shown || !h.spell) {
						continue;  // a weapon's charge is the gems'
					}
					here[i] = true;
					fill[i] = std::clamp(h.fraction, 0.0f, 1.0f);
					tint[i] = ImVec4{ h.color.r, h.color.g, h.color.b, 1.0f };
					element[i] = h.element;
					school[i] = h.school;
					skill[i] = h.skill;
				}
			}
			const float dt = std::isfinite(io->DeltaTime) ? std::clamp(io->DeltaTime, 0.0f, 0.25f) : 0.0f;
			for (std::size_t i = 0; i < 2; ++i) {
				shown[i] = std::clamp(shown[i] + (here[i] ? dt : -dt) * 4.0f, 0.0f, 1.0f);
			}
			if (shown[0] <= 0.0f && shown[1] <= 0.0f) {
				return;
			}
			auto* dl = GetForegroundDrawList();
			if (!dl) {
				return;
			}
			const double now = GetTime();
			const float  k = std::clamp(hud.reticleSize, 0.5f, 2.0f);
			const float  cx = io->DisplaySize.x * 0.5f, cy = io->DisplaySize.y * 0.5f;
			const float  low = hud.low;
			const auto   colOf = [](float a) {
				return [a](float r, float g, float b, float o) { return ColorConvertFloat4ToU32(ImVec4{ r, g, b, o * a }); };
			};
			const auto hueOf = [&](std::size_t i) { return fill[i] < low ? ImVec4{ 1.0f, 0.42f, 0.18f, 1.0f } : tint[i]; };

			if (hud.reticleStyle == 1) {
				// THE CROSSFIRE BAR: one bar, magicka is one pool - the fuller side's fill (both hands read the same magicka)
				const float a = std::clamp(hud.reticleOpacity, 0.1f, 1.0f) * (std::max)(shown[0], shown[1]);
				const auto  col = colOf(a);
				const float f = (std::max)(shown[0] > 0.0f ? fill[0] : 0.0f, shown[1] > 0.0f ? fill[1] : 0.0f);
				const float glow = pulse[0].Glow(f, now);
				const auto  left = shown[0] > 0.0f ? hueOf(0) : hueOf(1);
				const auto  right = shown[1] > 0.0f ? hueOf(1) : hueOf(0);
				const float w = Tuning::Get(Tuning::gBarWidth) * k, h = 6.0f * k;
				const float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, yc = cy + Tuning::Get(Tuning::gBarDrop) * k;
				const float edge = x0 + w * f;
				const ImU32 bone = col(0.86f, 0.82f, 0.73f, 0.9f);
				ImDrawListManager::AddRectFilled(dl, ImVec2{ x0, yc - h * 0.5f - 1.0f }, ImVec2{ x1, yc + h * 0.5f + 1.0f }, col(0.0f, 0.0f, 0.0f, 0.6f), 0.0f, 0);
				ImDrawListManager::AddRect(dl, ImVec2{ x0, yc - h * 0.5f - 1.0f }, ImVec2{ x1, yc + h * 0.5f + 1.0f }, col(0.78f, 0.75f, 0.65f, 0.45f), 0.0f, 0, 1.0f);
				if (f > 0.0f) {
					// the left hand's color at the left end, the right hand's toward the fill's edge, faint to bright
					ImDrawListManager::AddRectFilledMultiColor(dl, ImVec2{ x0, yc - h * 0.5f }, ImVec2{ edge, yc + h * 0.5f },
						col(left.x, left.y, left.z, 0.45f), col(right.x, right.y, right.z, 1.0f), col(right.x, right.y, right.z, 1.0f),
						col(left.x, left.y, left.z, 0.45f));
				}
				for (const auto [x, s] : { std::pair{ x0, 1.0f }, std::pair{ x1, -1.0f } }) {
					ImDrawListManager::AddLine(dl, ImVec2{ x, yc - h * 1.6f }, ImVec2{ x, yc + h * 1.6f }, bone, std::max(1.0f, k));
					ImDrawListManager::AddTriangleFilled(dl, ImVec2{ x - s * 10.0f * k, yc }, ImVec2{ x - s * 3.0f * k, yc - h * 0.9f },
						ImVec2{ x - s * 3.0f * k, yc + h * 0.9f }, bone);
				}
				// the glow at the fill's edge, breathing; and the pulse over the whole bar
				const float breath = 0.85f + 0.15f * static_cast<float>(std::sin(now * (f < low ? 9.0 : 4.0)));
				for (int g = 0; g < 7; ++g) {
					const float r = (22.0f - static_cast<float>(g) * 2.6f) * k * breath;
					ImDrawListManager::AddCircleFilled(dl, ImVec2{ edge, yc }, r, col(right.x, right.y, right.z, 0.04f + static_cast<float>(g) * 0.012f), 24);
				}
				if (glow > 0.0f) {
					ImDrawListManager::AddRectFilled(dl, ImVec2{ x0 - 4.0f * k, yc - h * 1.4f }, ImVec2{ x1 + 4.0f * k, yc + h * 1.4f },
						col(right.x, right.y, right.z, 0.22f * glow), 3.0f * k, 0);
				}
				const float r = 6.0f * k;
				ImDrawListManager::AddQuadFilled(dl, ImVec2{ edge, yc - r }, ImVec2{ edge + r, yc }, ImVec2{ edge, yc + r }, ImVec2{ edge - r, yc },
					col(0.96f, 0.93f, 0.82f, 1.0f));
				ImDrawListManager::AddQuad(dl, ImVec2{ edge, yc - r }, ImVec2{ edge + r, yc }, ImVec2{ edge, yc + r }, ImVec2{ edge - r, yc },
					col(0.16f, 0.13f, 0.10f, 1.0f), 1.0f);
				// each hand's sigil beyond its end, its school and skill under it (Crossfire's info)
				static constexpr const char* kSchools[]{ "Destruction", "Restoration", "Conjuration", "Alteration", "Illusion" };
				auto*                        font = GetFont();
				const float                  fs = GetFontSize() * 0.8f * k;
				for (std::size_t i = 0; i < 2; ++i) {
					if (shown[i] <= 0.0f) {
						continue;
					}
					const auto  hue = hueOf(i);
					const float sx = i == 0 ? x0 - 30.0f * k : x1 + 30.0f * k;
					DrawSigil(dl, ImVec2{ sx, yc }, 10.0f * k, element[i], col(hue.x, hue.y, hue.z, 1.0f), a * shown[i]);
					if (school[i] >= 0 && school[i] < 5) {
						char info[48];
						std::snprintf(info, sizeof(info), "%s %d", Translation::T(kSchools[school[i]]), skill[i]);
						for (char* c = info; *c; ++c) {
							*c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
						}
						const float tw = CalcTextSize(info).x * fs / GetFontSize();
						const float tx = i == 0 ? x0 : x1 - tw;
						ImDrawListManager::AddText(dl, font, fs, ImVec2{ tx + 1.0f, yc + h * 1.6f + 3.0f * k + 1.0f }, col(0.0f, 0.0f, 0.0f, 0.8f), info);
						ImDrawListManager::AddText(dl, font, fs, ImVec2{ tx, yc + h * 1.6f + 3.0f * k }, col(0.75f, 0.71f, 0.63f, 1.0f), info);
					}
				}
				return;
			}

			// THE BARS beside the crosshair, one per hand
			const float gap = Tuning::Get(Tuning::gReticleGap) * k, h = Tuning::Get(Tuning::gReticleLength) * k, w = 5.0f * k;
			const float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
			for (std::size_t i = 0; i < 2; ++i) {
				if (shown[i] <= 0.0f) {
					continue;
				}
				const float a = std::clamp(hud.reticleOpacity, 0.1f, 1.0f) * shown[i];
				const auto  col = colOf(a);
				const float f = fill[i];
				const bool  ember = f < low;
				const auto  hue = hueOf(i);
				const auto  shade = [&](float o) { return col(hue.x, hue.y, hue.z, o); };
				const ImU32 bone = col(0.86f, 0.82f, 0.73f, 0.9f);
				const float x = cx + (i == 0 ? -gap : gap);
				const float yf = y1 - h * f;
				const float glow = pulse[i].Glow(f, now);
				// the track: a dark slot with a faint bone edge
				ImDrawListManager::AddRectFilled(dl, ImVec2{ x - w * 0.5f - 1.0f, y0 }, ImVec2{ x + w * 0.5f + 1.0f, y1 }, col(0.0f, 0.0f, 0.0f, 0.6f), 0.0f, 0);
				ImDrawListManager::AddRect(dl, ImVec2{ x - w * 0.5f - 1.0f, y0 }, ImVec2{ x + w * 0.5f + 1.0f, y1 }, col(0.78f, 0.75f, 0.65f, 0.45f), 0.0f, 0, 1.0f);
				// the fill, faint at the bottom and brightest at its top
				if (f > 0.0f) {
					ImDrawListManager::AddRectFilledMultiColor(dl, ImVec2{ x - w * 0.5f, yf }, ImVec2{ x + w * 0.5f, y1 }, shade(1.0f), shade(1.0f),
						shade(0.4f), shade(0.4f));
				}
				// the soft pulse: the whole slot glows (out of magicka, breathing; refilled, one swell)
				if (glow > 0.0f) {
					ImDrawListManager::AddRectFilled(dl, ImVec2{ x - w * 1.5f, y0 - 3.0f * k }, ImVec2{ x + w * 1.5f, y1 + 3.0f * k }, shade(0.22f * glow),
						3.0f * k, 0);
				}
				// the bracket ends: a tick and a small outward chevron, as on the bar
				for (const auto [y, d] : { std::pair{ y0, -1.0f }, std::pair{ y1, 1.0f } }) {
					ImDrawListManager::AddLine(dl, ImVec2{ x - w * 1.6f, y }, ImVec2{ x + w * 1.6f, y }, bone, std::max(1.0f, k));
					ImDrawListManager::AddTriangleFilled(dl, ImVec2{ x, y + d * 8.0f * k }, ImVec2{ x - w * 0.9f, y + d * 2.5f * k },
						ImVec2{ x + w * 0.9f, y + d * 2.5f * k }, bone);
				}
				// the glow round the top of the fill, breathing (faster when nearly empty)
				const float breath = 0.85f + 0.15f * static_cast<float>(std::sin(now * (ember ? 9.0 : 4.0)));
				for (int g = 0; g < 6; ++g) {
					const float r = (14.0f - static_cast<float>(g) * 1.8f) * k * breath;
					ImDrawListManager::AddCircleFilled(dl, ImVec2{ x, yf }, r, shade(0.04f + static_cast<float>(g) * 0.012f), 24);
				}
				// the diamond on the top of the fill
				const float r = 4.5f * k;
				ImDrawListManager::AddQuadFilled(dl, ImVec2{ x, yf - r }, ImVec2{ x + r, yf }, ImVec2{ x, yf + r }, ImVec2{ x - r, yf },
					col(0.96f, 0.93f, 0.82f, 1.0f));
				ImDrawListManager::AddQuad(dl, ImVec2{ x, yf - r }, ImVec2{ x + r, yf }, ImVec2{ x, yf + r }, ImVec2{ x - r, yf }, col(0.16f, 0.13f, 0.10f, 1.0f),
					1.0f);
			}
		}
	}

	bool MenusHideHud() noexcept { return MenuWatch::Get().hide.load(std::memory_order_relaxed); }

	void RegisterMenu()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&MenuWatch::Get());  // the HUD elements hide over the game's menus
		}
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		// under the mod's own section; the colour scheme is the plugin's (its Menu.cpp sets it first): one DLL, one theme
		SKSEMenuFramework::SetSection(T(Mod::kName));
		// his order 2026-10-10: "an entirely new main menu ... search for the weapon/spell and change it's lights" (FadeItems.cpp)
		SKSEMenuFramework::AddSectionItem(T("Lights by Item"), Items::Render);
		SKSEMenuFramework::AddSectionItem(T("HUD"), RenderHudSettings);  // right below the mod's own pages (his order 2026-10-09)
		SKSEMenuFramework::AddSectionItem(T("Fading"), RenderSettings);
#ifdef FADE_DEBUG_PAGE
		SKSEMenuFramework::AddSectionItem(T("Fading - debug"), RenderDebug);
#endif
		SKSEMenuFramework::AddHudElement(RenderHud);
		SKSEMenuFramework::AddHudElement(RenderReticle);
		SKSE::log::info("settings pages added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
