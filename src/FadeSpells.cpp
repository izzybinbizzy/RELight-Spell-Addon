// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Spells in hand. A readied spell's light and glow follow the caster's magicka, the way a weapon's follow its charge.
// Like the weapons, nothing is made: whatever hangs at the casting hand is scaled - the game's own casting light (the
// effect's hand light), and every light and glowing mesh under the hand's magic node, where the casting art is attached
// and where Light Placer, Illuminated and spell art mods hang theirs. So it works with all of them, and with
// none.
//
// Also here: the kind of an enchantment's or a spell's strongest effect (element and school), which the category
// switches in the settings read, for weapons and spells alike.

#include "Fade.h"

namespace Fade
{
	// 2026-10-03, found testing every enchantment kind in game: Skyrim's own soul trap effects (SoulTrapFFActor,
	// EnchSoulTrapFFContact) are SCRIPT effects, not the Soul Trap archetype, so the archetype alone missed every vanilla one
	bool IsSoulTrap(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return false;
		}
		if (a_effect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kSoulTrap) {
			return true;
		}
		auto id = EditorID(a_effect);
		std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return id.find("soultrap") != std::string::npos;
	}

	Kind KindOf(const RE::MagicItem* a_item)
	{
		Kind        k;
		const auto* top = a_item ? a_item->GetCostliestEffectItem() : nullptr;
		const auto* base = top ? top->baseEffect : nullptr;
		if (!base) {
			return k;
		}
		using A = RE::EffectArchetypes::ArchetypeID;
		switch (base->data.resistVariable) {
		case RE::ActorValue::kResistFire:
			k.element = Element::kFire;
			break;
		case RE::ActorValue::kResistFrost:
			k.element = Element::kFrost;
			break;
		case RE::ActorValue::kResistShock:
			k.element = Element::kShock;
			break;
		default:
			if (IsSoulTrap(base)) {
				k.element = Element::kSoulTrap;
				break;
			}
			switch (base->GetArchetype()) {
			case A::kAbsorb:
				k.element = Element::kAbsorb;
				break;
			case A::kSoulTrap:
				k.element = Element::kSoulTrap;
				break;
			case A::kParalysis:
				k.element = Element::kParalyze;
				break;
			case A::kDemoralize:
			case A::kTurnUndead:
			case A::kBanish:
				k.element = Element::kFearTurnBanish;
				break;
			default:
				break;
			}
			break;
		}
		switch (base->data.associatedSkill) {
		case RE::ActorValue::kDestruction:
			k.school = 0;
			break;
		case RE::ActorValue::kRestoration:
			k.school = 1;
			break;
		case RE::ActorValue::kConjuration:
			k.school = 2;
			break;
		case RE::ActorValue::kAlteration:
			k.school = 3;
			break;
		case RE::ActorValue::kIllusion:
			k.school = 4;
			break;
		default:
			break;
		}
		return k;
	}

	bool ReadSpellHand(RE::Actor* a_actor, bool a_left, SpellHand& a_out)
	{
		a_out = {};
		auto* form = a_actor ? a_actor->GetEquippedObject(a_left) : nullptr;
		auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
		if (!spell || spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
			return false;
		}
		a_out.spell = spell;
		auto* av = a_actor->AsActorValueOwner();
		if (av) {
			a_out.current = (std::max)(av->GetActorValue(RE::ActorValue::kMagicka), 0.0f);
			a_out.max = av->GetPermanentActorValue(RE::ActorValue::kMagicka) +
			            a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kMagicka);
		}
		a_out.fraction = a_out.max > 0.0f ? Glow::Clamp01(a_out.current / a_out.max) : 1.0f;
		const auto& name = a_left ? RE::FixedStrings::GetSingleton()->npcLMagicNode : RE::FixedStrings::GetSingleton()->npcRMagicNode;
		for (const bool first : { false, true }) {
			if (first && !a_actor->IsPlayerRef()) {
				break;
			}
			if (auto* root = a_actor->Get3D(first)) {
				a_out.nodes[first ? 1 : 0] = root->GetObjectByName(name);
			}
		}
		const auto source = a_left ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand;
		if (auto* caster = skyrim_cast<RE::ActorMagicCaster*>(a_actor->GetMagicCaster(source)); caster && caster->light) {
			a_out.casterLight = netimmerse_cast<RE::NiPointLight*>(caster->light->light.get());
		}
		return true;
	}
}
