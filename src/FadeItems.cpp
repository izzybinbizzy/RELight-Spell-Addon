// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// LIGHTS BY ITEM - HIS ORDER 2026-10-10: "completely remove own light for unlit weapons option. put that as an entirely new main
// menu and make it work for spells and weapons. have it be to where the user can search for the weapon/spell and change it's
// lights. so basically dynamic wards feature." His picks the same morning: a search over every spell, staff and weapon, an on /
// off switch per item, a color per item (Auto = the light's own, or one the player picks), "in my hands now", reset one / reset
// all - and "no slider per item, they all work with the main sliders".
//
// An item's choice is applied where its light is: once a frame on the main thread, at the end of the fading module's pass (so
// after the sliders and the fading, before a ground twin copies the hand light), on every light at the hand that holds it -
// a spell's: under the hand's magic nodes and the game's casting light; a weapon's (staves and bound weapons too): under its
// 3D in the hand. Off puts the light out (fade 0), a color recolors it; the fade and color it had are kept, and given back the
// moment the item is set to Auto again (unless something else wrote the light since). Every lighting mod's light at that hand
// is reached - Light Placer's, RE::Light's, the game's own, ours - since the choice is about the item, not the light's maker.
// The choices live in Mod::kItemsPath, one line per item that is not Auto, keyed "Plugin.esp|0x00ABCD" (the item's own file
// and local id, so they survive a load order change).

#include "Fade.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
// SKSE Menu Framework's own header (theirs, MIT): its warnings are not ours, and ours are errors (xmake.lua)
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#include "Translation.h"
#include "MenuStyle.h"

#include <sstream>

namespace Fade::Items
{
	namespace
	{
		using Translation::T;

		struct Choice
		{
			bool        off{ false };
			bool        colored{ false };
			RE::NiColor color{ 1.0f, 1.0f, 1.0f };

			[[nodiscard]] bool Auto() const noexcept { return !off && !colored; }
		};

		// one item in the menu's list (built once at data load, main thread; read-only after)
		struct Item
		{
			RE::FormID  id{ 0 };
			std::string key;    // "Plugin.esp|0x00ABCD"
			std::string name;   // as the game shows it
			std::string lower;  // for the search
			std::string group;  // the school, or the weapon's kind
			bool        spell{ false };
		};

		std::vector<Item>                      gCatalog;
		std::mutex                             gLock;  // the choices: the menu (render thread) changes them, the main thread applies
		std::unordered_map<RE::FormID, Choice> gChoices;
		std::atomic<std::uint32_t>             gGeneration{ 0 };  // bumped on every change; the main thread copies on a change
		std::array<std::atomic<RE::FormID>, 2> gInHands{};        // the player's left / right item now, for "in my hands now"

		// main thread only: this frame's copy of the choices, and every light a choice is on now
		std::unordered_map<RE::FormID, Choice> gApplied;
		std::uint32_t                          gAppliedGen = static_cast<std::uint32_t>(-1);
		struct Held
		{
			RE::NiPointer<RE::NiPointLight> light;
			float                           ownFade{ 0.0f }, wroteFade{ -1.0f };
			RE::NiColor                     ownColor{}, wroteColor{ -1.0f, -1.0f, -1.0f };
			bool                            faded{ false }, colored{ false };
			std::uint32_t                   frame{ 0 };
		};
		std::unordered_map<RE::NiPointLight*, Held> gHeld;
		std::uint32_t                               gFrame = 0;

		[[nodiscard]] std::string KeyOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return {};
			}
			const auto local = a_form->GetLocalFormID();
			return std::format("{}|0x{:06X}", file->GetFilename(), local);
		}

		[[nodiscard]] RE::FormID FormOfKey(std::string_view a_key)
		{
			const auto bar = a_key.find('|');
			if (bar == std::string_view::npos) {
				return 0;
			}
			const auto    file = std::string(a_key.substr(0, bar));
			const auto    hex = a_key.substr(bar + 1);
			std::uint32_t local = 0;
			const auto    digits = hex.starts_with("0x") || hex.starts_with("0X") ? hex.substr(2) : hex;
			if (std::from_chars(digits.data(), digits.data() + digits.size(), local, 16).ec != std::errc{}) {
				return 0;
			}
			auto*       dh = RE::TESDataHandler::GetSingleton();
			const auto* form = dh ? dh->LookupForm(local, file) : nullptr;
			return form ? form->GetFormID() : 0;
		}

		[[nodiscard]] std::string LowerText(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		void SaveLocked()
		{
			std::ostringstream text;
			text << "; " << Mod::kName << " - Lights by Item, written by its menu. One line per item that is not Auto:\n"
				 << ";   Plugin.esp|0x00ABCD=off           its light is off\n"
				 << ";   Plugin.esp|0x00ABCD=255,128,0     its light in this color\n[Items]\n";
			for (const auto& item : gCatalog) {
				const auto it = gChoices.find(item.id);
				if (it == gChoices.end() || it->second.Auto()) {
					continue;
				}
				const auto& c = it->second;
				if (c.off) {
					text << item.key << "=off\n";
				} else {
					text << std::format("{}={},{},{}\n", item.key, std::lround(c.color.red * 255.0f), std::lround(c.color.green * 255.0f),
						std::lround(c.color.blue * 255.0f));
				}
			}
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(Mod::kItemsPath).parent_path(), ec);
			const std::string tmp = std::string(Mod::kItemsPath) + ".tmp";
			{
				std::ofstream out(tmp, std::ios::trunc);
				out << text.str();
				if (!out.flush()) {
					SKSE::log::warn("lights by item: {} could not be written", Mod::kItemsPath);
					return;
				}
			}
			if (std::filesystem::rename(tmp, Mod::kItemsPath, ec); ec) {
				SKSE::log::warn("lights by item: {} could not be replaced ({})", Mod::kItemsPath, ec.message());
				std::filesystem::remove(tmp, ec);
			}
		}

		void Set(RE::FormID a_id, const Choice& a_choice)
		{
			std::lock_guard lock(gLock);
			if (a_choice.Auto()) {
				gChoices.erase(a_id);
			} else {
				gChoices[a_id] = a_choice;
			}
			gGeneration.fetch_add(1, std::memory_order_relaxed);
			SaveLocked();
		}

		// ------------------------------------------------------------------ applying, main thread
		RE::NiAVObject* WeaponPart(RE::Actor* a_actor, bool a_firstPerson, bool a_left, const RE::TESForm* a_weapon)
		{
			const auto& biped = a_actor->GetBiped(a_firstPerson);
			if (!biped || !a_weapon) {
				return nullptr;
			}
			using B = RE::BIPED_OBJECT;
			if (a_left) {
				const auto& shield = biped->objects[B::kShield];
				return shield.item == a_weapon && shield.partClone ? shield.partClone.get() : nullptr;
			}
			for (auto slot = static_cast<std::uint32_t>(B::kHandToHandMelee); slot <= static_cast<std::uint32_t>(B::kCrossbow); ++slot) {
				const auto& obj = biped->objects[slot];
				if (obj.item == a_weapon && obj.partClone) {
					return obj.partClone.get();
				}
			}
			return nullptr;
		}

		void Touch(RE::NiPointLight* a_light, const Choice& a_choice)
		{
			if (!a_light) {
				return;
			}
			auto [it, added] = gHeld.try_emplace(a_light);
			auto& h = it->second;
			if (added) {
				h.light.reset(a_light);
			}
			h.frame = gFrame;
			auto& d = a_light->GetLightRuntimeData();
			if (a_choice.off) {
				if (!h.faded || d.fade != h.wroteFade) {
					h.ownFade = d.fade;  // what its owner gave it: given back when the item is Auto again
				}
				d.fade = 0.0f;
				h.wroteFade = 0.0f;
				h.faded = true;
			} else if (h.faded) {
				if (d.fade == h.wroteFade) {
					d.fade = h.ownFade;
				}
				h.faded = false;
			}
			if (a_choice.colored) {
				const bool same = d.diffuse.red == h.wroteColor.red && d.diffuse.green == h.wroteColor.green && d.diffuse.blue == h.wroteColor.blue;
				if (!h.colored || !same) {
					h.ownColor = d.diffuse;
				}
				d.diffuse = a_choice.color;
				h.wroteColor = a_choice.color;
				h.colored = true;
			} else if (h.colored) {
				const bool same = d.diffuse.red == h.wroteColor.red && d.diffuse.green == h.wroteColor.green && d.diffuse.blue == h.wroteColor.blue;
				if (same) {
					d.diffuse = h.ownColor;
				}
				h.colored = false;
			}
		}

		void TouchUnder(RE::NiAVObject* a_root, const Choice& a_choice)
		{
			if (!a_root) {
				return;
			}
			RE::BSVisit::TraverseScenegraphLights(a_root, [&](RE::NiPointLight* a_light) {
				Touch(a_light, a_choice);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		void VisitActor(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->Is3DLoaded()) {
				return;
			}
			const bool player = a_actor->IsPlayerRef();
			for (const bool left : { false, true }) {
				auto* form = a_actor->GetEquippedObject(left);
				if (player) {
					gInHands[left ? 0 : 1].store(form ? form->GetFormID() : 0, std::memory_order_relaxed);
				}
				const auto it = form ? gApplied.find(form->GetFormID()) : gApplied.end();
				if (it == gApplied.end()) {
					continue;
				}
				const auto& choice = it->second;
				if (form->As<RE::SpellItem>()) {
					const auto& name = left ? RE::FixedStrings::GetSingleton()->npcLMagicNode : RE::FixedStrings::GetSingleton()->npcRMagicNode;
					for (const bool first : { false, true }) {
						if (first && !player) {
							break;
						}
						auto* root = a_actor->Get3D(first);
						TouchUnder(root ? root->GetObjectByName(name) : nullptr, choice);
					}
					const auto source = left ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
					if (auto* caster = skyrim_cast<RE::ActorMagicCaster*>(a_actor->GetMagicCaster(source)); caster && caster->light) {
						Touch(netimmerse_cast<RE::NiPointLight*>(caster->light->light.get()), choice);
					}
				} else if (form->As<RE::TESObjectWEAP>()) {
					TouchUnder(WeaponPart(a_actor, false, left, form), choice);
					if (player) {
						TouchUnder(WeaponPart(a_actor, true, left, form), choice);
					}
				}
			}
		}
	}

	void Load()
	{
		std::lock_guard lock(gLock);
		gChoices.clear();
		std::ifstream in(Mod::kItemsPath);
		std::string   line;
		std::size_t   read = 0, missing = 0;
		while (std::getline(in, line)) {
			const auto t = SettingsText::Trim(line);
			if (t.empty() || t.front() == ';' || t.front() == '[') {
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) {
				continue;
			}
			const auto id = FormOfKey(SettingsText::Trim(t.substr(0, eq)));
			const auto val = SettingsText::Trim(t.substr(eq + 1));
			if (!id) {
				++missing;
				continue;
			}
			Choice c;
			if (SettingsText::SameText(val, "off")) {
				c.off = true;
			} else {
				int  rgb[3]{};
				auto rest = val;
				bool ok = true;
				for (int& v : rgb) {
					const auto comma = rest.find(',');
					const auto part = SettingsText::Trim(rest.substr(0, comma));
					ok = ok && SettingsText::ReadInt(part, v);
					rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
				}
				if (!ok) {
					continue;
				}
				c.colored = true;
				c.color = { std::clamp(rgb[0], 0, 255) / 255.0f, std::clamp(rgb[1], 0, 255) / 255.0f, std::clamp(rgb[2], 0, 255) / 255.0f };
			}
			gChoices[id] = c;
			++read;
		}
		gGeneration.fetch_add(1, std::memory_order_relaxed);
		SKSE::log::info("lights by item: {} choice(s) read from {} ({} name an item not in this load order)", read, Mod::kItemsPath, missing);
	}

	void BuildCatalog()
	{
		static constexpr const char* kSchools[]{ "Destruction", "Restoration", "Conjuration", "Alteration", "Illusion" };
		gCatalog.clear();
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
			if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell || spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				continue;
			}
			const char* name = spell->GetFullName();
			if (!name || !*name) {
				continue;
			}
			const auto kind = KindOf(spell);
			const auto key = KeyOf(spell);
			if (key.empty()) {
				continue;
			}
			gCatalog.push_back({ spell->GetFormID(), key, name, LowerText(name), kind.school >= 0 ? kSchools[kind.school] : "Other spells", true });
		}
		for (auto* weapon : dh->GetFormArray<RE::TESObjectWEAP>()) {
			if (!weapon || !weapon->GetPlayable()) {
				continue;
			}
			const char* name = weapon->GetFullName();
			const bool  staff = weapon->IsStaff();
			const bool  bound = weapon->IsBound();
			if (!name || !*name || !(weapon->formEnchanting || staff || bound)) {
				continue;  // a weapon with no magic carries no light of ours to change
			}
			const auto key = KeyOf(weapon);
			if (key.empty()) {
				continue;
			}
			gCatalog.push_back({ weapon->GetFormID(), key, name, LowerText(name), staff ? "Staves" : bound ? "Bound weapons" :
																											 "Enchanted weapons",
				false });
		}
		std::ranges::sort(gCatalog, [](const Item& a, const Item& b) { return std::make_tuple(!a.spell, a.group, a.lower) < std::make_tuple(!b.spell, b.group, b.lower); });
		SKSE::log::info("lights by item: {} spells and weapons in the list", gCatalog.size());
	}

	void Apply()
	{
		++gFrame;
		if (const auto gen = gGeneration.load(std::memory_order_relaxed); gen != gAppliedGen) {
			std::lock_guard lock(gLock);
			gApplied = gChoices;
			gAppliedGen = gen;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!gApplied.empty()) {
			VisitActor(player);
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto& handle : lists->highActorHandles) {
					if (auto actor = handle.get(); actor && actor.get() != player) {
						VisitActor(actor.get());
					}
				}
			}
		} else if (player) {
			for (const bool left : { false, true }) {
				auto* form = player->GetEquippedObject(left);
				gInHands[left ? 0 : 1].store(form ? form->GetFormID() : 0, std::memory_order_relaxed);
			}
		}
		// a light no choice reached this frame goes back as its owner left it (an item set to Auto, put away, a light gone)
		for (auto it = gHeld.begin(); it != gHeld.end();) {
			auto&      h = it->second;
			const bool gone = !h.light || h.light->GetRefCount() <= 1;
			if (gone || h.frame != gFrame) {
				if (!gone) {
					Touch(h.light.get(), Choice{});  // Auto: what we wrote goes back to its own
				}
				it = gHeld.erase(it);
			} else {
				++it;
			}
		}
	}

	void Release()
	{
		for (auto& [light, h] : gHeld) {
			if (h.light && h.light->GetRefCount() > 1) {
				Touch(h.light.get(), Choice{});
			}
		}
		gHeld.clear();
	}

	// ------------------------------------------------------------------ the page
	void __stdcall Render()
	{
		using namespace ImGuiMCP;
		const MenuStyle::Page page;
		static char           search[64]{};
		static bool           handsOnly = false;
		MenuStyle::Header(MenuStyle::Icon::kBulb, T("Lights by Item"));
		TextWrapped("%s", T("Find a spell, staff or weapon and change its light: switch it off, or give it a color of its own. Every "
							"other item keeps its light as it is (Auto). The main Brightness and Reach sliders still reach them all."));
		SetNextItemWidth(260.0f);
		if (InputTextWithHint("##itemSearch", T("Search a spell or weapon"), search, sizeof(search))) {
			handsOnly = false;
		}
		SameLine();
		if (Button(T("In my hands now"))) {
			handsOnly = true;
			search[0] = '\0';
		}
		SetItemTooltip("%s", T("Shows the spells and weapons in your hands right now."));
		SameLine();
		if (Button(T("Reset all"))) {
			std::lock_guard lock(gLock);
			gChoices.clear();
			gGeneration.fetch_add(1, std::memory_order_relaxed);
			SaveLocked();
		}
		SetItemTooltip("%s", T("Every item back to Auto: its light as the mod and your other lighting mods made it."));
		std::unordered_map<RE::FormID, Choice> choices;
		{
			std::lock_guard lock(gLock);
			choices = gChoices;
		}
		const std::string     needle = LowerText(search);
		const RE::FormID      hands[2]{ gInHands[0].load(std::memory_order_relaxed), gInHands[1].load(std::memory_order_relaxed) };
		std::string           group;
		std::size_t           shown = 0;
		constexpr std::size_t kMaxShown = 120;  // a search narrows it; the whole load order is thousands
		for (const auto& item : gCatalog) {
			const bool inHands = item.id && (item.id == hands[0] || item.id == hands[1]);
			const bool changed = choices.contains(item.id);
			if (handsOnly ? !inHands : (needle.empty() ? !changed : item.lower.find(needle) == std::string::npos)) {
				continue;  // with no search, the list shows the items you changed
			}
			if (++shown > kMaxShown) {
				TextDisabled(T("... and more - type more of the name to narrow the list"));
				break;
			}
			if (item.group != group) {
				group = item.group;
				Spacing();
				TextColored(ImVec4{ 1.0f, 0.86f, 0.55f, 1.0f }, "%s", T(group.c_str()));
			}
			PushID(static_cast<int>(item.id));
			auto c = choices.contains(item.id) ? choices[item.id] : Choice{};
			bool on = !c.off;
			if (Checkbox("##on", &on)) {
				c.off = !on;
				Set(item.id, c);
			}
			SetItemTooltip("%s", T("Its light on or off."));
			SameLine();
			bool own = c.colored;
			if (Checkbox("##own", &own)) {
				c.colored = own;
				Set(item.id, c);
			}
			SetItemTooltip("%s", T("Its own color (off: Auto, the light's own color)."));
			SameLine();
			BeginDisabled(!c.colored);
			float rgb[3]{ c.color.red, c.color.green, c.color.blue };
			if (ColorEdit3("##color", rgb, ImGuiColorEditFlags_NoInputs)) {
				c.color = { rgb[0], rgb[1], rgb[2] };
				Set(item.id, c);
			}
			EndDisabled();
			SameLine();
			Text("%s", item.name.c_str());
			if (changed) {
				SameLine();
				if (SmallButton(T("Reset"))) {
					Set(item.id, Choice{});
				}
			}
			PopID();
		}
		if (shown == 0) {
			TextDisabled("%s", handsOnly      ? T("Nothing with a light in your hands right now.") :
							   needle.empty() ? T("No item changed yet - search one above.") :
												T("Nothing by that name."));
		}
	}
}
