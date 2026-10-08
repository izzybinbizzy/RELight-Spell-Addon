// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Three pages in SKSE Menu Framework's Mod Control Panel, under their own section so nothing of RE::Light's own menu is
// touched. Settings: Brightness, Reach, lights off while sneaking, hand lights, weapon lights, and a switch per option the
// installer put down (his call, 2026-09-28 late night: no per-option brightness sliders). Patches: a switch per mod patch. Weapons: a switch per weapon option, laid out as
// the Patches page is. Every change is saved at once (Settings.cpp) and reaches lights already lit. The look is the shared
// MenuStyle.h in warm spell-light gold, with this mod's glowing headings.

#define WIN32_LEAN_AND_MEAN  // NOMINMAX is set for every file in xmake.lua (the fading module needs it too)
#include "Plugin.h"

#include "SKSEMenuFramework.h"
#include "Translation.h"
#include "MenuStyle.h"

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
			return a_option.download == kPatches ? Page::kPatches : a_option.download == kWeapons ? Page::kWeapons :
			                                                                                        Page::kSettings;
		}

		// The look (his ask, 2026-09-23: "a cool ui design ... not too crazy, just a subtle glowy vibe"): warm spell-light
		// amber on the menu's own dark - headings on a soft glow that fades to the right with a thin lit underline, gold
		// check marks and slider grips, frames a touch warmer when hovered. Scoped to our pages, so no other mod's changes.
		constexpr ImGuiMCP::ImVec4 kGold{ 1.0f, 0.86f, 0.55f, 1.0f };
		constexpr ImGuiMCP::ImVec4 kEmber{ 0.93f, 0.72f, 0.45f, 0.85f };
		// every shown line goes through T(): Data\SKSE\Plugins\RelightSpellAddon\Translation.json (relightgen.py writes the English one)
		using Translation::T;

		// his call, 2026-09-27: as Illuminated does - when CS Light is loaded, say which of its options light the same
		// things a second time (the option names are CS Light's own installer's, spelling included)
		// a printf line with three counts, for a widget that takes plain text
		[[nodiscard]] std::string Fill3(const char* a_format, std::size_t a_a, std::size_t a_b, std::size_t a_c)
		{
			char buf[256];
			std::snprintf(buf, sizeof(buf), a_format, a_a, a_b, a_c);
			return buf;
		}

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

		// a heading on a soft glow; a_icon 0 for none (the Patches and Weapons pages' category headings)
		void GlowHeading(const char* a_text, unsigned a_icon = 0)
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
			if (a_icon) {
				FontAwesome::PushSolid();
				TextColored(kGold, "%s", FontAwesome::UnicodeToUtf8(a_icon).c_str());
				FontAwesome::Pop();
				SameLine();
			}
			TextColored(kGold, "%s", T(a_text));
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
				// the ids with a Weapons half, gathered once per draw (the same fix as the counts below - CodeRabbit's CWE-407
				// read of line 211, 2026-10-06: a scan of every option for every row made the draw quadratic)
				std::unordered_set<std::string_view> weaponIds;
				for (const auto& m : opts) {
					if (m.weapons) {
						weaponIds.insert(m.id);
					}
				}
				std::vector<std::size_t> packs;
				for (const auto i : OptionsInMenuOrder()) {
					const auto& o = opts[i];
					if (!o.switchable || PageOf(o) != Page::kPatches || drawn.contains(o.id)) {
						continue;
					}
					const bool weaponHalf = weaponIds.contains(o.id);
					if (weaponHalf && drawn.insert(o.id).second) {
						packs.push_back(i);
					}
				}
				std::ranges::stable_sort(packs, {}, [&opts](std::size_t a_i) { return std::make_tuple(opts[a_i].author, opts[a_i].name); });
				// his call, 2026-09-27: "Creation Club always stays with vanilla stuff" - its switch sits in the page's first
				// category (Artifacts, Bound Weapons), not with the mod patches at the bottom
				const auto  rest = std::ranges::stable_partition(packs, [&opts](std::size_t a_i) { return opts[a_i].name == "Creation Club"; });
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
			// every file of a pack, counted once per draw (CodeRabbit deep scan 2026-10-06, CWE-407: a scan of every option
			// for every row was quadratic in the number of installed data files)
			std::unordered_map<std::string_view, std::pair<std::size_t, std::size_t>> counts;
			for (const auto& m : opts) {
				if (m.switchable) {
					auto& c = counts[m.id];
					c.first += m.lit;
					c.second += m.heldOut;
				}
			}
			std::string shown, author;
			for (std::size_t n = 0; n < order.size(); ++n) {
				const auto               i = order[n];
				auto&                    o = opts[i];
				static const std::string kPackHeading = T("Mod Patches (also on the Patches page)");
				const auto&              heading = n >= packsFrom          ? kPackHeading :
				                                   withVanilla.contains(i) ? shown :
				                                   grouped                 ? (o.category.empty() ? o.download : o.category) :
				                                                             o.download;
				if (heading != shown) {
					shown = heading;
					author.clear();
					GlowHeading(shown.c_str());  // GlowHeading translates
				}
				if (grouped && o.author != author) {
					author = o.author;
					if (!author.empty()) {
						ImGuiMCP::Spacing();
						ImGuiMCP::TextColored(kEmber, "%s", T(author.c_str()));
					}
				}
				const bool indented = grouped && !o.author.empty();  // an author's mods sit under the author's name
				if (indented) {
					ImGuiMCP::Indent();
				}
				ImGuiMCP::PushID(static_cast<int>(i));
				bool on = o.on;
				if (ImGuiMCP::Checkbox(T(o.name.c_str()), &on)) {
					SetOptionOn(i, on);
					SaveSettings();
					RehandSoon();
				}
				if (!o.desc.empty()) {  // his call, 2026-09-26: a broad word on what each switch lights
					ImGuiMCP::SetItemTooltip("%s", T(o.desc.c_str()));
				}
				const auto [lit, heldOut] = counts[o.id];  // every file of a pack
				ImGuiMCP::SameLine();
				if (!o.on) {
					ImGuiMCP::TextDisabled(T("off - %zu held out"), heldOut);
				} else if (o.weapons && !WeaponLightsOn()) {
					ImGuiMCP::TextDisabled(T("Weapon lights off - %zu held out"), heldOut);
				} else {
					ImGuiMCP::TextDisabled(T("%zu lit"), lit);
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
			MenuStyle::Status(true, Fill3(T("%zu data file(s), %zu held weapon light(s) right now, %zu spell(s) with a hand light"), DataFiles(),
										LiveHeldLights(), HandEffects())
										.c_str());
			if (const auto* cs = CSLightLoaded()) {
				ImGuiMCP::TextColored(kGold, T("%s is loaded."), cs);
				ImGuiMCP::TextWrapped("%s", T("RELight - Spell Addon does not need CS Light. If you keep CS Light for its world lights, untick its Magic FX, Mysticsm, Bound Weapons, Praedy Staves, Regular soulgems, Spiders, Misc Effects and Dwarven Spiders options in its own installer, or those lights glow twice."));
				ImGuiMCP::Separator();
			}
			GlowHeading("Lights", Icon::kBulb);
			int b = BrightnessPercent();
			if (ImGuiMCP::SliderInt(T("Brightness"), &b, 10, 200, "%d%%")) {
				SetBrightnessPercent(b);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("How bright this mod's lights are, and only this mod's. 100% is the measured brightness; about 30% "
				  "is what the old Reduced download was. How far they carry does not change - that is the slider below."));

			int r = ReachPercent();
			if (ImGuiMCP::SliderInt(T("Reach"), &r, 50, 150, "%d%%")) {
				SetReachPercent(r);
			}
			if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("How far this mod's lights carry. 100% is the measured reach. How bright they are does not change - "
				  "that is the slider above."));

			// ---- Illuminated's light settings, ported 2026-10-08 (his "add everything from illuminated into relight ... sister
			// mods"); RE::Light lights the objects, these are what it has no setting for
			// presets: one click sets the two sliders and the light budget (-1 leaves the budget as it is)
			struct Preset
			{
				const char* name;
				const char* tip;
				int         brightness, reach, hands;
			};
			const Preset presets[] = {
				{ T("Subtle"), T("Softer lights that stay close to the spell."), 75, 80, -1 },
				{ T("Default"), T("The lights as the mod was made."), 100, 100, 0 },
				{ T("Dramatic"), T("Brighter lights that reach further."), 150, 120, -1 },
				{ T("Performance"), T("For big fights: hand lights for you and your followers only, and a shorter reach."), 100, 80, 2 },
			};
			ImGuiMCP::TextDisabled("%s", T("Presets:"));
			for (const auto& p : presets) {
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button(p.name)) {
					SetBrightnessPercent(p.brightness);
					SetReachPercent(p.reach);
					if (p.hands >= 0) {
						SetHandLightsFor(p.hands);
					}
					SaveSettings();
				}
				ImGuiMCP::SetItemTooltip("%s", p.tip);
			}

			const char* const kDaylight[] = { T("Off"), T("A little"), T("More") };
			int               day = DimInDaylight();
			if (ImGuiMCP::Combo(T("Dim in daylight"), &day, kDaylight, 3)) {
				SetDimInDaylight(day);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("This mod's lights are dimmer outdoors by day and in brightly lit rooms, where a bright light looks out of place, and at "
				  "full strength at night and in the dark. Follows the time of day as it passes."));

			const char* const kColorLooks[] = { T("Automatic"), T("Paler"), T("Deeper") };
			int               look = LightColors();
			if (ImGuiMCP::Combo(T("Light colors"), &look, kColorLooks, 3)) {
				SetLightColors(look);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("Automatic draws each color as RE::Light gives it. Paler lifts every color toward white, Deeper takes it richer."));

			bool sneak = SneakOn();
			if (ImGuiMCP::Checkbox(T("Lights off while sneaking"), &sneak)) {
				SetSneakOn(sneak);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("While you sneak, no spell light turns on - hand lights, projectiles, runes, explosions and "
				  "hazards - and the ones already lit go out. They come back when you stand up."));

			bool hands = HandLightsOn();
			if (ImGuiMCP::Checkbox(T("Hand lights"), &hands)) {
				SetHandLightsOn(hands);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("A light on your hands while you cast, in the color of the spell. Changes reach a spell you are already holding."));

			// his call, 2026-09-26: "a toggle for weapon lights just like hand lights and right under it"
			bool weapons = WeaponLightsOn();
			if (ImGuiMCP::Checkbox(T("Weapon lights"), &weapons)) {
				SetWeaponLightsOn(weapons);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("Every weapon light: enchanted weapons, bound weapons, artifacts and staves. Off puts them all out at "
				  "once; the switches on the Weapons page choose among them."));

			GlowHeading("Spell Colors", Icon::kBulb);
			const char* const kElementLabels[] = { "", T("Fire color"), T("Frost color"), T("Shock color") };  // literals, for Translation.json
			for (int e = 1; e <= 3; ++e) {
				// Brightness.cpp NamedColor's order (Dynamic Wards' preset hues)
				const char* const items[] = { T("Auto (the spell's own)"), T("Crimson"), T("Ember"), T("Gold"), T("Green"), T("Teal"),
					T("Frost"), T("Blue"), T("Violet"), T("Magenta"), T("White") };
				static_assert(std::size(items) == kNamedColorCount + 1);
				int c = ElementColor(e);
				ImGuiMCP::PushID(e);
				if (ImGuiMCP::Combo(kElementLabels[e], &c, items, static_cast<int>(std::size(items)))) {
					SetElementColor(e, c);
					SaveSettings();
				}
				ImGuiMCP::PopID();
				ImGuiMCP::SetItemTooltip("%s", T("The color of every spell of that element - its hand, bolt and explosion. Auto keeps each spell's own color."));
			}

			GlowHeading("Big Fights", Icon::kBulb);
			const char* const kWho[] = { T("Everyone"), T("Everyone nearby"), T("Player and followers"), T("Player only") };
			int               who = HandLightsFor();
			if (ImGuiMCP::Combo(T("Hand lights for"), &who, kWho, 4)) {
				SetHandLightsFor(who);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("Which casters' hands carry a spell light. In a big fight fewer lights keep the game smooth. Everyone nearby leaves "
				  "out casters farther than about forty paces from you. Reaches the next spell they ready."));

			GlowHeading("Spells Without a Patch", Icon::kBulb);
			bool autoOn = AutoLightsOn();
			if (ImGuiMCP::Checkbox(T("Light spells this mod has no patch for"), &autoOn)) {
				SetAutoLightsOn(autoOn);
				SaveSettings();
				RehandSoon();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("RE::Light switches off a casting light it has no config for, so another mod's spell would be dark in the hand. On: it "
				  "gets a hand light in the color of its own light, or of its element, at the strength of this mod's hand lights."));
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled(T("%zu lit"), AutoHandEffects());

			GlowHeading("Wards", Icon::kShield);
			if (WardsSteppedDown()) {
				ImGuiMCP::TextDisabled("%s", T("Dynamic Wards is installed - it colors the wards, so this setting stands aside."));
			} else {
				const char* const kColours[] = { T("Vanilla blue"), T("White") };
				int               c = WardColour();
				if (ImGuiMCP::Combo(T("Ward color"), &c, kColours, 2)) {
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
					T("The ward's dome, the 360 Ward sphere and its flash, the art on your hand and the hand light, in one colour. "
					  "Vanilla blue keeps the vanilla dome and gives 360 Ward's sphere the vanilla blue. Shows on the next cast."));
			}

			DrawSwitches(Page::kSettings);
		}

		// his ask, 2026-09-23: the mod patches on a page of their own
		void __stdcall RenderPatches()
		{
			const GlowStyle style;
			MenuStyle::Note(T("Lights for other mods' spells and weapons. Each switch only matters if you have that mod."));
			DrawSwitches(Page::kPatches);
		}

		// his ask, 2026-09-26: the weapon options on a page of their own, laid out as the Patches page
		void __stdcall RenderWeapons()
		{
			const GlowStyle style;
			MenuStyle::Status(WeaponLightsOn(), WeaponLightsOn() ? T("Lights for weapons: enchantments, bound weapons and artifacts.") : T("Weapon lights is off on the Settings page, so every light here is out."));
			DrawSwitches(Page::kWeapons);
		}
	}

	void RegisterMenu()
	{
		SKSE::log::info("{}", Translation::Load("Data/SKSE/Plugins/RelightSpellAddon/Translation.json"));
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		MenuStyle::gTheme = MenuStyle::MakeTheme(0xFFD58A);  // warm spell-light gold
		SKSEMenuFramework::SetSection(T("RELight - Spell Addon"));
		SKSEMenuFramework::AddSectionItem(T("Settings"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Patches"), RenderPatches);
		SKSEMenuFramework::AddSectionItem(T("Weapons"), RenderWeapons);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
