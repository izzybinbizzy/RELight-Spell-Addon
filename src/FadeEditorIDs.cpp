// The fading module (Illuminated and RELight - Spell Lighting NG carry identical copies; FadeConfig.h and FadeDevBench.cpp are each mod's own)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Editor IDs, recorded as each form loads, because the game throws most of them away. The ONE recorder in the plugin:
// SetFormEditorID (vtable slot 0x33) is hooked per form type at SKSE's post-load, before the game reads its plugins.
// The fading module needs weapons and enchantments (rule files may name them by editor ID; keywords keep their own) and
// magic effects (the soul trap test, FadeSpells.cpp, and the plugins' own readers look at them while the game runs); a
// plugin that needs more types asks for them with RecordEditorIDs<T>() (Fade.h). After the data has loaded,
// ForgetPassEditorIDs() lets go of every other editor ID - the passes that needed them have run - so the table holds
// only those three types. EditorID() hands out a copy.

#include "Fade.h"

namespace Fade
{
	namespace
	{
		std::unordered_map<const RE::TESForm*, std::string> gEditorIDs;
		RE::BSSpinLock                                      gLock;

		// the form types kept for the whole session: weapons and enchantments (rule files name them), magic effects (read
		// while the game runs: the soul trap test and the plugins' own readers)
		[[nodiscard]] bool RuleType(const RE::TESForm* a_form) noexcept
		{
			return a_form->Is(RE::FormType::Weapon, RE::FormType::Enchantment, RE::FormType::MagicEffect);
		}
	}

	void RememberEditorID(const RE::TESForm* a_form, const char* a_id)
	{
		if (a_form && a_id && *a_id) {
			RE::BSSpinLockGuard guard(gLock);
			gEditorIDs[a_form] = a_id;
		}
	}

	void InstallEditorIDHooks()
	{
		RecordEditorIDs<RE::TESObjectWEAP>();
		RecordEditorIDs<RE::EnchantmentItem>();
		RecordEditorIDs<RE::EffectSetting>();
		SKSE::log::info("editor IDs: weapons, enchantments and magic effects are recorded as they load");
	}

	void ForgetPassEditorIDs()
	{
		RE::BSSpinLockGuard guard(gLock);
		std::erase_if(gEditorIDs, [](const auto& a_kv) { return !RuleType(a_kv.first); });
		gEditorIDs.rehash(0);
	}

	std::string EditorID(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		{
			RE::BSSpinLockGuard guard(gLock);
			if (const auto it = gEditorIDs.find(a_form); it != gEditorIDs.end()) {
				return it->second;
			}
		}
		const char* own = a_form->GetFormEditorID();
		return own ? std::string(own) : std::string();
	}

	std::string Label(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "(none)";
		}
		const auto  id = EditorID(a_form);
		const auto* file = a_form->GetFile(0);
		const char* name = a_form->GetName();
		return std::format("{} [{}] {}|{:08X}", name && *name ? name : "(no name)", id.empty() ? "no editor ID" : id,
			file ? file->GetFilename() : "(made in game)", a_form->GetFormID());
	}
}
