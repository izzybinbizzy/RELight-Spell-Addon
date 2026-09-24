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
	RE::BSSpinLock gLock;
	// every spell light the game made through the hooked call sites, held so sneaking can find it again
	std::unordered_map<RE::NiLight*, RE::NiPointer<RE::NiLight>> gMagicLights;
	std::vector<RE::NiPointer<RE::NiLight>>                      gCulled;
	bool                                                         gWasSneaking = false;
	std::uint32_t                                                gMade = 0;

	bool PlayerSneaking()
	{
		if (!Plugin::SneakOn()) {
			return false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player && player->IsSneaking();
	}

	[[nodiscard]] bool IsSpellObject(const RE::TESObjectREFR* a_ref)
	{
		using FT = RE::FormType;
		return a_ref && a_ref->Is(FT::ProjectileMissile, FT::ProjectileArrow, FT::ProjectileGrenade, FT::ProjectileBeam,
							FT::ProjectileFlame, FT::ProjectileCone, FT::ProjectileBarrier, FT::Explosion, FT::PlacedHazard);
	}

	// a light only these lists still hold has left the game
	[[nodiscard]] bool Gone(const RE::NiPointer<RE::NiLight>& a_light) { return !a_light || a_light->GetRefCount() <= 1; }

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
			if (gMagicLights.contains(niLight) || IsSpellObject(Plugin::ReferenceOf(niLight))) {
				niLight->SetAppCulled(true);
				gCulled.emplace_back(niLight);
			}
		}
	}

	void UncullAll()
	{
		for (auto& l : gCulled) {
			// a light held out for a switched-off option (or a hand light a switch put out) stays out when the player stands up
			if (l && !Plugin::HeldOutForOption(l.get()) && !Plugin::HandLightHeldOut(l.get())) {
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
			// our hand lights are made with the game's own function and never reach RE::Light, which would switch off
			// a casting light it has no config for (an in-memory record can have none)
			const Plugin::Hand* hand = Plugin::HandOfLight(a_light);
			RE::NiPointLight*   made = nullptr;
			if (hand) {
				made = netimmerse_cast<RE::NiPointLight*>(
					a_light->GenDynamic(a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly));
				Plugin::DressHandLight(made, *hand);
				Plugin::NoteHandLight(made, *hand);
			} else {
				made = func(a_light, a_ref, a_node, a_forceDynamic, a_useLightRadius, a_affectRequesterOnly);
			}
			if (made) {
				RE::BSSpinLockGuard lock(gLock);
				if ((++gMade & 63) == 0) {
					std::erase_if(gMagicLights, [](const auto& a_kv) { return Gone(a_kv.second); });
				}
				gMagicLights.try_emplace(made, made);
				// a hand light of ours, and a spray light RE::Light made from our config, are ours for the sliders
				if (hand || Plugin::IsSprayLight(a_light)) {
					Plugin::RememberLight(made, hand ? Plugin::HandFxOf(hand->key) : nullptr);
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
				RE::BSSpinLockGuard lock(gLock);
				if (PlayerSneaking()) {
					CullTree(root);
				} else {
					// RE::Light has already hung its light here; put a switched-off option's out before its first frame
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

	// a projectile's 3D is taken apart: its travelling lights leave with it (Illuminated's route; the per-frame check in
	// Streams.cpp alone missed a finished Thunderbolt, whose light stayed lit - measured 2026-09-24)
	template <class T>
	struct Release3D
	{
		static void thunk(T* a_this)
		{
			Plugin::DropStreamLights(a_this);
			func(a_this);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			func = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6B, thunk);
		}
	};

	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			const bool sneaking = Plugin::SneakOn() && a_this && a_this->IsSneaking();
			RE::BSSpinLockGuard lock(gLock);
			if (sneaking) {
				CullSpellLights();
			} else if (gWasSneaking) {
				UncullAll();
			}
			std::erase_if(gCulled, Gone);
			gWasSneaking = sneaking;
			// after the sneaking pass, in the same frame, so a light given back above and then held out
			// here never reaches the screen in between
			Plugin::UpdateOptionLights();
			Plugin::UpdateStreamLights();
			// last: RE::Light has already written this frame's fades (its update runs inside `func` above)
			Plugin::UpdateBrightness(a_delta);
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
		// if the call sites still lead to the game's own function, RE::Light hooked nothing here
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
		// only the classes a stream can hang on
		Release3D<RE::MissileProjectile>::Install();
		Release3D<RE::ArrowProjectile>::Install();
		Release3D<RE::GrenadeProjectile>::Install();
		Release3D<RE::BeamProjectile>::Install();
		Release3D<RE::FlameProjectile>::Install();
		Release3D<RE::ConeProjectile>::Install();
		Release3D<RE::BarrierProjectile>::Install();
		REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		SKSE::log::info("projectile, explosion and hazard loads, projectile unloads and the player update hooked after every plugin loaded");
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
			// after every plugin has loaded: the hook written last runs first, and ours must run before RE::Light's
			Install();
			// Dynamic Wards 2.0 changes a ward's casting art from its menu; its hand light follows
			SKSE::GetMessagingInterface()->RegisterListener("DynamicWards", [](SKSE::MessagingInterface::Message* a_m) {
				if (a_m && a_m->type == 'DWAC') {
					Plugin::RefindHandLights();
				}
			});
		} else if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Plugin::LoadData();
			Plugin::LoadSettings();
			Plugin::ClaimSprayLights();  // before the hand lights remember each effect's own light
			Plugin::MakeHandLights();
			Plugin::TakeStreamProjectileLights();
			InstallLate();
			Plugin::RegisterMenu();
		} else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame || a_msg->type == SKSE::MessagingInterface::kNewGame) {
			Plugin::RefindHandLights();  // Dynamic Wards 2.0's ranked hand art, set at data load in whichever order
		}
	});
	return true;
}
