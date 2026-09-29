// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Three pages in SKSE Menu Framework's Mod Control Panel, under their own section so nothing of RE::Light's own menu is
// touched. Settings: Brightness, Reach, lights off while sneaking, hand lights, weapon lights, and a switch per option the
// installer put down (his call, 2026-09-28 late night: no per-option brightness sliders). Patches: a switch per mod patch. Weapons: a switch per weapon option, laid out as
// the Patches page is. Every change is saved at once (Settings.cpp) and reaches lights already lit.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"

namespace Plugin
{
	namespace
	{

		// the menu draws off the game's main thread; which light a magic effect wears is changed on it
		void RehandSoon()
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([]() {
					ApplyHandLights(true);
					ApplyStreamProjectileLights(true);
				});
			}
		}

		constexpr std::string_view kPatches = "Patch Collection";
		constexpr std::string_view kWeapons = "Weapons";

		enum class Page
		{
			kSettings,
			kPatches,
			kWeapons
		};

		[[nodiscard]] Page PageOf(const Option& a_option)
		{
			return a_option.download == kPatches ? Page::kPatches : a_option.download == kWeapons ? Page::kWeapons : Page::kSettings;
		}

		// The look (his ask, 2026-09-23: "a cool ui design ... not too crazy, just a subtle glowy vibe"): warm spell-light
		// amber on the menu's own dark - headings on a soft glow that fades to the right with a thin lit underline, gold
		// check marks and slider grips, frames a touch warmer when hovered. Scoped to our pages, so no other mod's changes.
		constexpr ImGuiMCP::ImVec4 kGold{ 1.0f, 0.86f, 0.55f, 1.0f };
		constexpr ImGuiMCP::ImVec4 kEmber{ 0.93f, 0.72f, 0.45f, 0.85f };

		// his call, 2026-09-27: as Illuminated does - when CS Light is loaded, say which of its options light the same
		// things a second time (the option names are CS Light's own installer's, spelling included)
		[[nodiscard]] const char* CSLightLoaded()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			for (const auto* name : { "CS Light.esp", "CS Light.esl" }) {
				if (dh && (dh->LookupLoadedModByName(name) || dh->LookupLoadedLightModByName(name))) {
					return name;
				}
			}
			return nullptr;
		}

		class GlowStyle
		{
		public:
			GlowStyle()
			{
				using namespace ImGuiMCP;
				PushStyleColor(ImGuiCol_CheckMark, ImVec4{ 1.0f, 0.80f, 0.42f, 1.0f });
				PushStyleColor(ImGuiCol_SliderGrab, ImVec4{ 1.0f, 0.74f, 0.38f, 0.90f });
				PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4{ 1.0f, 0.86f, 0.55f, 1.0f });
				PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.12f, 0.10f, 0.08f, 0.75f });
				PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{ 0.30f, 0.21f, 0.10f, 0.75f });
				PushStyleColor(ImGuiCol_Separator, ImVec4{ 1.0f, 0.78f, 0.45f, 0.22f });
				PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
			}
			~GlowStyle()
			{
				ImGuiMCP::PopStyleVar(1);
				ImGuiMCP::PopStyleColor(6);
			}
			GlowStyle(const GlowStyle&) = delete;
			GlowStyle& operator=(const GlowStyle&) = delete;
		};

		void GlowHeading(const char* a_text)
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
			TextColored(kGold, "  %s", a_text);
			Dummy(ImVec2{ 0.0f, 4.0f });
		}

		// the switches. Settings: the Spells download's own options, under its heading, in the build's menu order.
		// Patches (his call, 2026-09-23): a heading per category in the build's order, and under it each author's mods
		// the mods with no author group first, then each author's mods together under the author's name - by name within each.
		// Weapons (his call, 2026-09-26: "handled just like the spells mod menu"): the same layout as Patches.
		// A pack split across the downloads (his call, 2026-09-26) is several options with one id: one row, one switch.
		void DrawSwitches(Page a_page)
		{
			auto&                           opts = Options();
			const bool                      grouped = a_page != Page::kSettings;
			std::vector<std::size_t>        order;
			std::unordered_set<std::string> drawn;
			for (const auto i : OptionsInMenuOrder()) {
				if (opts[i].switchable && PageOf(opts[i]) == a_page && drawn.insert(opts[i].id).second) {
					order.push_back(i);
				}
			}
			if (grouped) {
				// his call: a category's own mods first, the authors' groups at the bottom
				std::ranges::stable_sort(order, {}, [&opts](std::size_t a_i) {
					const auto& o = opts[a_i];
					return std::make_tuple(o.menu, !o.author.empty(), o.author, o.name);
				});
			}
			// his call, 2026-09-26 (night-run answers): a pack's ONE switch shows on the Weapons page as well as Patches - the
			// same option, so flipping either flips both. At the bottom of the Weapons page, under its own heading.
			std::size_t                     packsFrom = order.size();
			std::unordered_set<std::size_t> withVanilla;  // packs shown under the first category's heading
			if (a_page == Page::kWeapons) {
				std::vector<std::size_t> packs;
				for (const auto i : OptionsInMenuOrder()) {
					const auto& o = opts[i];
					if (!o.switchable || PageOf(o) != Page::kPatches || drawn.contains(o.id)) {
						continue;
					}
					const bool weaponHalf = std::ranges::any_of(opts, [&o](const Option& m) { return m.id == o.id && m.weapons; });
					if (weaponHalf && drawn.insert(o.id).second) {
						packs.push_back(i);
					}
				}
				std::ranges::stable_sort(packs, {}, [&opts](std::size_t a_i) { return std::make_tuple(opts[a_i].author, opts[a_i].name); });
				// his call, 2026-09-27: "Creation Club always stays with vanilla stuff" - its switch sits in the page's first
				// category (Artifacts, Bound Weapons), not with the mod patches at the bottom
				const auto rest = std::ranges::stable_partition(packs, [&opts](std::size_t a_i) { return opts[a_i].name == "Creation Club"; });
				std::size_t at = 0;
				while (at < order.size() && opts[order[at]].category == opts[order.front()].category) {
					++at;
				}
				const auto ccCount = static_cast<std::size_t>(std::ranges::distance(packs.begin(), rest.begin()));
				order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), packs.begin(), rest.begin());
				withVanilla.insert(packs.begin(), rest.begin());
				order.insert(order.end(), rest.begin(), rest.end());
				packsFrom = order.size() - (packs.size() - ccCount);
			}
			std::string shown, author;
			for (std::size_t n = 0; n < order.size(); ++n) {
				const auto i = order[n];
				auto& o = opts[i];
				static const std::string kPackHeading = "Mod Patches (also on the Patches page)";
				const auto& heading = n >= packsFrom             ? kPackHeading :
				                      withVanilla.contains(i)    ? shown :
				                      grouped                    ? (o.category.empty() ? o.download : o.category) :
				                                                   o.download;
				if (heading != shown) {
					shown = heading;
					author.clear();
					GlowHeading(shown.c_str());
				}
				if (grouped && o.author != author) {
					author = o.author;
					if (!author.empty()) {
						ImGuiMCP::Spacing();
						ImGuiMCP::TextColored(kEmber, "%s", author.c_str());
					}
				}
				const bool indented = grouped && !o.author.empty();  // an author's mods sit under the author's name
				if (indented) {
					ImGuiMCP::Indent();
				}
				ImGuiMCP::PushID(static_cast<int>(i));
				bool on = o.on;
				if (ImGuiMCP::Checkbox(o.name.c_str(), &on)) {
					SetOptionOn(i, on);
					SaveSettings();
					RehandSoon();
				}
				if (!o.desc.empty()) {  // his call, 2026-09-26: a broad word on what each switch lights
					ImGuiMCP::SetItemTooltip("%s", o.desc.c_str());
				}
				std::size_t lit = 0, heldOut = 0;  // every file of a pack
				for (const auto& m : opts) {
					if (m.switchable && m.id == o.id) {
						lit += m.lit;
						heldOut += m.heldOut;
					}
				}
				ImGuiMCP::SameLine();
				if (!o.on) {
					ImGuiMCP::TextDisabled("off - %zu held out", heldOut);
				} else if (o.weapons && !WeaponLightsOn()) {
					ImGuiMCP::TextDisabled("Weapon lights off - %zu held out", heldOut);
				} else {
					ImGuiMCP::TextDisabled("%zu lit", lit);
				}
				ImGuiMCP::PopID();
				if (indented) {
					ImGuiMCP::Unindent();
				}
			}
		}

		void __stdcall RenderSettings()
		{
			const GlowStyle style;
			if (const auto* cs = CSLightLoaded()) {
				ImGuiMCP::TextColored(kGold, "%s is loaded.", cs);
				ImGuiMCP::TextWrapped("%s", "RELight - Spell Addon does not need CS Light. If you keep CS Light for its world lights, untick its Magic FX, Mysticsm, Bound Weapons, Praedy Staves, Regular soulgems, Spiders, Misc Effects and Dwarven Spiders options in its own installer, or those lights glow twice.");
				ImGuiMCP::Separator();
			}
			GlowHeading("Lights");
			int b = BrightnessPercent();
			if (ImGuiMCP::SliderInt("Brightness", &b, 10, 200, "%d%%")) {
				SetBrightnessPercent(b);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"How bright this mod's lights are, and only this mod's. 100% is the measured brightness; about 30% "
				"is what the old Reduced download was. How far they carry does not change - that is the slider below.");

			int r = ReachPercent();
			if (ImGuiMCP::SliderInt("Reach", &r, 50, 150, "%d%%")) {
				SetReachPercent(r);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"How far this mod's lights carry. 100% is the measured reach. How bright they are does not change - "
				"that is the slider above.");

			bool sneak = SneakOn();
			if (ImGuiMCP::Checkbox("Lights off while sneaking", &sneak)) {
				SetSneakOn(sneak);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"While you sneak, no spell light turns on - hand lights, projectiles, runes, explosions and "
				"hazards - and the ones already lit go out. They come back when you stand up.");

			bool hands = HandLightsOn();
			if (ImGuiMCP::Checkbox("Hand lights", &hands)) {
				SetHandLightsOn(hands);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"A light on your hands while you cast, in the color of the spell. Changes reach a spell you are already holding.");

			// his call, 2026-09-26: "a toggle for weapon lights just like hand lights and right under it"
			bool weapons = WeaponLightsOn();
			if (ImGuiMCP::Checkbox("Weapon lights", &weapons)) {
				SetWeaponLightsOn(weapons);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"Every weapon light: enchanted weapons, bound weapons, artifacts and staves. Off puts them all out at "
				"once; the switches on the Weapons page choose among them.");

			GlowHeading("Wards");
			if (WardsSteppedDown()) {
				ImGuiMCP::TextDisabled("%s", "Dynamic Wards is installed - it colors the wards, so this setting stands aside.");
			} else {
				static const char* const kColours[] = { "Vanilla blue", "White" };
				int                      c = WardColour();
				if (ImGuiMCP::Combo("Ward color", &c, kColours, 2)) {
					SetWardColour(c);
					SaveSettings();
					// the art forms and lights are changed on the game's main thread; the next cast shows it
					if (auto* tasks = SKSE::GetTaskInterface()) {
						tasks->AddTask([]() {
							ApplyWards("the menu");
							RefindHandLights();
						});
					}
				}
				ImGuiMCP::SetItemTooltip("%s",
					"The ward's dome, the 360 Ward sphere and its flash, the art on your hand and the hand light, in one colour. "
					"Vanilla blue keeps the vanilla dome and gives 360 Ward's sphere the vanilla blue. Shows on the next cast.");
			}

			DrawSwitches(Page::kSettings);
			ImGuiMCP::Separator();
			ImGuiMCP::TextDisabled("%zu data file(s), %zu travelling light(s) and %zu held weapon light(s) right now, %zu spell(s) with a hand light",
				DataFiles(), LiveStreamLights(), LiveHeldLights(), HandEffects());
		}

		// his ask, 2026-09-23: the mod patches on a page of their own
		void __stdcall RenderPatches()
		{
			const GlowStyle style;
			ImGuiMCP::TextDisabled("%s", "Lights for other mods' spells and weapons. Each switch only matters if you have that mod.");
			DrawSwitches(Page::kPatches);
		}

		// his ask, 2026-09-26: the weapon options on a page of their own, laid out as the Patches page
		void __stdcall RenderWeapons()
		{
			const GlowStyle style;
			ImGuiMCP::TextDisabled("%s", WeaponLightsOn() ? "Lights for weapons: enchantments, bound weapons and artifacts."
			                                              : "Weapon lights is off on the Settings page, so every light here is out.");
			DrawSwitches(Page::kWeapons);
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection("RELight - Spell Addon");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		SKSEMenuFramework::AddSectionItem("Patches", RenderPatches);
		SKSEMenuFramework::AddSectionItem("Weapons", RenderWeapons);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
