// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Editor IDs, recorded as each form loads, because the game throws most of them away. Rule files may name a weapon,
// an enchantment or a magic effect by editor ID; keywords keep their own, so they need nothing here. The same way
// Illuminated does it the same way: SetFormEditorID (vtable slot 0x33) is hooked on the form types the rules read, at plugin load,
// before the game reads its plugins. Nothing is ever erased; EditorID() hands out a copy.

#include "Fade.h"

namespace Fade
{
	namespace
	{
		std::unordered_map<const RE::TESForm*, std::string> gEditorIDs;
		RE::BSSpinLock                                      gLock;

		void Remember(const RE::TESForm* a_form, const char* a_id)
		{
			if (a_form && a_id && *a_id) {
				RE::BSSpinLockGuard guard(gLock);
				gEditorIDs[a_form] = a_id;
			}
		}

		template <class T>
		struct SetEditorID
		{
			static bool thunk(RE::TESForm* a_this, const char* a_id)
			{
				Remember(a_this, a_id);
				return func(a_this, a_id);
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static void                                    Install()
			{
				REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
				func = vtbl.write_vfunc(0x33, thunk);
			}
		};
	}

	void InstallEditorIDHooks()
	{
		SetEditorID<RE::TESObjectWEAP>::Install();
		SetEditorID<RE::EnchantmentItem>::Install();
		SetEditorID<RE::EffectSetting>::Install();
		SKSE::log::info("editor IDs: weapons, enchantments and magic effects are recorded as they load");
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
