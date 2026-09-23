// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version. See LICENSE.txt.
//
// THE FILES, AND WHAT EACH ONE IS FOR
//   main.cpp        this file - the hooks, and spell lights going out while you sneak
//   Data.cpp        the data files the installer put down: what this mod lights, its switches, its streams
//   Settings.cpp    the settings file
//   Options.cpp     the switches - an option's lights put out while the game runs
//   Brightness.cpp  our own brightness slider, which scales this mod's lights and nothing else
//   Streams.cpp     lights that travel with sprays, breath shouts and beams
//   HandLights.cpp  the light on the caster's hands, made in memory - no plugin, no script
//   Menu.cpp        the settings page, in SKSE Menu Framework's Mod Control Panel
//   Plugin.h        what they share      PCH.h  what they all include

#include "Plugin.h"

namespace
{
	RE::BSSpinLock                          gLock;
	std::vector<RE::NiPointer<RE::NiLight>> gMagicLights;
	std::vector<RE::NiPointer<RE::NiLight>> gCulled;
	bool                                    gWasSneaking = false;

	bool PlayerSneaking()
	{
		// 🌙 the setting is what decides now, not the installer: with it off this whole feature is
		// inert, which is exactly what an unticked option used to mean
		if (!Plugin::SneakOn()) {
			return false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player && player->IsSneaking();
	}

	RE::TESObjectREFR* OwnerOf(RE::NiAVObject* a_obj)
	{
		for (auto* o = a_obj; o; o = o->parent) {
			if (auto* ref = o->GetUserData()) {
				return ref;
			}
		}
		return nullptr;
	}

	bool IsSpellObject(RE::TESObjectREFR* a_ref)
	{
		using FT = RE::FormType;
		return a_ref && a_ref->Is(FT::ProjectileMissile, FT::ProjectileArrow, FT::ProjectileGrenade, FT::ProjectileBeam,
							FT::ProjectileFlame, FT::ProjectileCone, FT::ProjectileBarrier, FT::Explosion, FT::PlacedHazard);
	}

	bool IsMagicLight(RE::NiLight* a_light)
	{
		for (auto& l : gMagicLights) {
			if (l.get() == a_light) {
				return true;
			}
		}
		return false;
	}

	void Prune(std::vector<RE::NiPointer<RE::NiLight>>& a_list)
	{
		std::erase_if(a_list, [](const RE::NiPointer<RE::NiLight>& l) { return !l || l->GetRefCount() <= 1; });
	}

	void CullSpellLights()
	{
		auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!ssn) {
			return;
		}
		for (auto& bsLight : ssn->GetRuntimeData().activeLights) {
			if (!bsLight || !bsLight->light) {
				continue;
			}
			auto* niLight = bsLight->light.get();
			if (niLight->GetAppCulled()) {
				continue;
			}
			if (IsMagicLight(niLight) || IsSpellObject(OwnerOf(niLight))) {
				niLight->SetAppCulled(true);
				gCulled.emplace_back(niLight);
			}
		}
	}

	void UncullAll()
	{
		for (auto& l : gCulled) {
			// a light the probe is holding out for a switched-off option stays out: standing up is not a
			// reason to give it back, and the two passes must not be able to fight over the same light
			if (l && !Plugin::HeldOutForOption(l.get())) {
				l->SetAppCulled(false);
			}
		}
		gCulled.clear();
	}

	struct MagicLight
	{
		static RE::NiPointLight* thunk(RE::TESObjectLIGH* a_light, RE::TESObjectREFR* a_ref, RE::NiNode* a_node,
			bool a_forceDynamic, bool a_useLightRadius, bool a_affectRequesterOnly)
		{
			if (PlayerSneaking()) {
				return nullptr;
			}
			// 🖐 one of our hand lights (HandLights.cpp): made with the game's own function, NOT passed on to
			// RE::Light, which switches off a casting light it has no config for - and it can have none for a
			// light that lives in memory. Then dressed the way RE::Light dressed the plugin's records.
			const Plugin::Hand* hand = Plugin::HandOfLight(a_light);
			RE::NiPointLight*   made = nullptr;
			if (hand) {
				made = netimmerse_cast<RE::NiPointLight*>(
					a_light->GenDynamic(a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly));
				Plugin::DressHandLight(made, *hand);
			} else {
				made = func(a_light, a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly);
			}
			if (made) {
				RE::BSSpinLockGuard lock(gLock);
				Prune(gMagicLights);
				gMagicLights.emplace_back(made);
				// a hand light of ours, and a spray light RE::Light made from our config, are ours for the brightness slider
				if (hand || Plugin::IsSprayLight(a_light)) {
					Plugin::RememberHandLight(made);
				}
			}
			return made;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void CullTree(RE::NiAVObject* a_root)
	{
		if (!a_root) {
			return;
		}
		RE::BSVisit::TraverseScenegraphLights(a_root, [](RE::NiPointLight* a_light) {
			if (a_light && !a_light->GetAppCulled()) {
				a_light->SetAppCulled(true);
				gCulled.emplace_back(a_light);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	template <class T>
	struct Load3D
	{
		static RE::NiAVObject* thunk(T* a_this, bool a_backgroundLoading)
		{
			auto* root = func(a_this, a_backgroundLoading);
			if (root) {
				// the travelling lights go on first, so that sneaking below puts them out with everything else
				Plugin::HangStreamLights(a_this, root);
				if constexpr (std::is_same_v<T, RE::Explosion>) {
					Plugin::NoteExplosion(a_this, root);
				}
				RE::BSSpinLockGuard lock(gLock);
				if (PlayerSneaking()) {
					CullTree(root);
				} else {
					// RE::Light hangs its light on this 3D inside its own Load3D hook, so a light for a
					// switched-off option exists the moment this returns. Putting it out here rather than
					// on the next player update is what decides whether it ever shows for a frame.
					Plugin::CullOptionLightsUnder(root);
				}
			}
			return root;
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			func = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6A, thunk);
		}
	};

	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			bool sneaking = Plugin::SneakOn() && a_this && a_this->IsSneaking();
			RE::BSSpinLockGuard lock(gLock);
			if (sneaking) {
				CullSpellLights();
			} else if (gWasSneaking) {
				UncullAll();
			}
			Prune(gCulled);
			gWasSneaking = sneaking;
			// after the sneaking pass, in the same frame, so a light given back above and then held out
			// here never reaches the screen in between
			Plugin::UpdateOptionLights();
			Plugin::UpdateStreamLights();
			// last: RE::Light has already written this frame's fades (its update runs inside `func` above)
			Plugin::UpdateBrightness();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void Install()
	{
		auto& trampoline = SKSE::GetTrampoline();
		std::vector<std::pair<REL::RelocationID, REL::VariantOffset>> sites{
			{ RELOCATION_ID(33603, 34381), REL::VariantOffset(0xAC, 0xE2, 0xE2) },
			{ RELOCATION_ID(33391, 34151), REL::VariantOffset(0x86, 0xCD, 0x86) },
			{ RELOCATION_ID(42965, 44222), REL::VariantOffset(0x58, 0x36D, 0x58) }
		};
		if (REL::Module::IsAE()) {
			sites.push_back({ RELOCATION_ID(33403, 34185), REL::VariantOffset(0x407, 0x407, 0x407) });
		}
		std::uintptr_t first = 0;
		for (auto& [id, off] : sites) {
			REL::Relocation<std::uintptr_t> target{ id, off };
			auto old = trampoline.write_call<5>(target.address(), MagicLight::thunk);
			if (!first) {
				first = old;
				MagicLight::func = old;
			} else if (old != first) {
				SKSE::log::warn("spell light call sites lead to different functions; using the first");
			}
		}
		// ⚫ whether RE::Light's hook is the one we now call: if the call sites still lead straight to the game's own
		// function, RE::Light hooked nothing here (it is not installed, or it loaded after this - which kPostLoad rules out)
		const auto game = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(17208, 17610) }.address();
		SKSE::log::info("spell lights: {} call sites hooked, after every plugin loaded; they led to {}", sites.size(),
			first == game ? "the game's own function (RE::Light has no hook there)" : "another plugin's hook (RE::Light's)");
	}

	void InstallLate()
	{
		Load3D<RE::MissileProjectile>::Install();
		Load3D<RE::ArrowProjectile>::Install();
		Load3D<RE::GrenadeProjectile>::Install();
		Load3D<RE::BeamProjectile>::Install();
		Load3D<RE::FlameProjectile>::Install();
		Load3D<RE::ConeProjectile>::Install();
		Load3D<RE::BarrierProjectile>::Install();
		Load3D<RE::Explosion>::Install();
		Load3D<RE::Hazard>::Install();
		REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		SKSE::log::info("projectile, explosion and hazard loads and the player update hooked after every plugin loaded");
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		if (!a_msg) {
			return;
		}
		if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
			// ⛔ AFTER EVERY PLUGIN HAS LOADED, and that is the point: RE::Light hooks these same call sites when IT
			// loads, and the hook written last runs first. Ours has to run first, or RE::Light switches our hand
			// lights off before we see them (HandLights.cpp).
			Install();
		} else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Plugin::LoadData();
			Plugin::LoadSettings();
			Plugin::ClaimSprayLights();  // before the hand lights remember each effect's own light
			Plugin::MakeHandLights();
			Plugin::TakeStreamProjectileLights();
			InstallLate();
			Plugin::RegisterMenu();
		}
	});
	return true;
}
