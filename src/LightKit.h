// LightKit - small helpers Illuminated and RELight - Spell Addon both use
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// The two mods never run together, so each carries its own copy of this file - kept byte for byte the same as the other
// by PC Runner\fade copies check.py (in preflight), as the fading module is. A change is made in one copy and copied to the
// other in the same pass. Nothing here knows which mod it is in.
//
//   Relaxed      a value one thread writes and another reads, with nothing else depending on it
//   NewForm      a form made in memory by the game's own factory; IsCallAt, a call site's check before it is hooked
//   Isl          the two words Community Shaders' inverse-square lighting reads off a light
//   CloneLight   a new light for the scene, and AddToScene to register it (ReLight's method, Truman's permission)
//   NamedColor   one of the ten named colors as the game's color
//   Daylight     how much dimmer the lights are by day and in bright rooms: the "Dim in daylight" setting
// The plain-number helpers (Lower, CutoffFor, the named colors, ToScreen, KeyAt ...) are in LightMath.h, tested off the game.

#pragma once

#include "PCH.h"

#include "LightMath.h"

namespace LightKit
{
	// A value read and written on different threads (the menu's render thread, the main thread, the game's loader threads)
	// with no other value depending on it: relaxed atomic loads and stores. Copyable, so it can live in a vector that is
	// built once at load.
	template <class T>
	class Relaxed
	{
	public:
		Relaxed(T a_value = T{}) noexcept : value(a_value) {}  // NOLINT(google-explicit-constructor): a drop-in for a plain T
		Relaxed(const Relaxed& a_other) noexcept : value(a_other.load()) {}
		Relaxed& operator=(const Relaxed& a_other) noexcept
		{
			store(a_other.load());
			return *this;
		}
		Relaxed& operator=(T a_value) noexcept
		{
			store(a_value);
			return *this;
		}
		Relaxed& operator++() noexcept
		{
			value.fetch_add(1, std::memory_order_relaxed);
			return *this;
		}
		// (no move of its own: a move copies, which is what a vector built at load needs)

						operator T() const noexcept { return load(); }  // NOLINT(google-explicit-constructor)
		[[nodiscard]] T load() const noexcept { return value.load(std::memory_order_relaxed); }
		void            store(T a_value) noexcept { value.store(a_value, std::memory_order_relaxed); }

	private:
		std::atomic<T> value;
	};

	// A new form of type T, made in memory by the game's own factory: it never reaches a save, and the game owns it for the
	// session (nothing frees it). nullptr when the game could not make one. Create() is not const, so neither is the factory.
	template <class T>
	[[nodiscard]] T* NewForm()
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<T>();
		return factory ? factory->Create() : nullptr;
	}

	// Every form of type T the game loaded, or an empty list when the data handler is not there (never a null read)
	template <class T>
	[[nodiscard]] RE::BSTArray<T*>& FormsOf()
	{
		static RE::BSTArray<T*> none;
		auto*                   dh = RE::TESDataHandler::GetSingleton();
		return dh ? dh->GetFormArray<T>() : none;
	}

	// A 5-byte relative call (opcode E8) is at a_address - checked before a call site inside a game function is wrapped;
	// when another plugin has rewritten the site, it is left alone and the caller says so in the log.
	[[nodiscard]] inline bool IsCallAt(std::uintptr_t a_address) noexcept
	{
		std::uint8_t opcode = 0;
		std::memcpy(&opcode, reinterpret_cast<const void*>(a_address), sizeof(opcode));
		return opcode == 0xE8;
	}

	// Community Shaders' inverse-square flag on a light RECORD (TESObjectLIGH): its falloff is then the cutoff
	inline constexpr std::uint32_t kRecordInverseSquare = 1u << 14;

	// Community Shaders' inverse-square lighting reads a flag and the cutoff from the first eight bytes of a light's runtime
	// data (its ambient color's red and green, as the game lays them out); without Community Shaders those words are plain
	// ambient color. They are read and written as bytes (memcpy), never through a pointer of another type, and always AFTER
	// SetLightAttenuation, which writes the same two words.
	namespace Isl
	{
		[[nodiscard]] inline std::array<std::uint32_t, 2> Read(RE::NiLight* a_light) noexcept
		{
			std::array<std::uint32_t, 2> w{};
			std::memcpy(w.data(), &a_light->GetLightRuntimeData(), sizeof(w));
			return w;
		}
		inline void Write(RE::NiLight* a_light, const std::array<std::uint32_t, 2>& a_words) noexcept
		{
			std::memcpy(&a_light->GetLightRuntimeData(), a_words.data(), sizeof(a_words));
		}
		[[nodiscard]] inline bool On(RE::NiLight* a_light) noexcept { return (Read(a_light)[0] & kFlag) != 0; }
		inline void               SetOn(RE::NiLight* a_light) noexcept
		{
			auto w = Read(a_light);
			w[0] |= kFlag;
			Write(a_light, w);
		}
		[[nodiscard]] inline float Cutoff(RE::NiLight* a_light) noexcept { return std::bit_cast<float>(Read(a_light)[1]); }
		inline void                SetCutoff(RE::NiLight* a_light, float a_cutoff, float a_lowest = kLowestCutoff,
			float a_highest = kHighestCutoff) noexcept
		{
			auto w = Read(a_light);
			w[1] = std::bit_cast<std::uint32_t>(std::clamp(a_cutoff, a_lowest, a_highest));
			Write(a_light, w);
		}
	}

	// A new light for the scene, cloned from one master light made once: ReLight found that a freshly made light, attached
	// straight away, crashes. nullptr when the game could not make one. Main thread.
	[[nodiscard]] inline RE::NiPointLight* CloneLight()
	{
		static RE::NiPointer<RE::NiPointLight> master;
		if (!master) {
			const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());  // let go once cloned
			auto*                                 clone = fresh ? netimmerse_cast<RE::NiPointLight*>(fresh->Clone()) : nullptr;
			if (!clone) {
				return nullptr;
			}
			master.reset(clone);
		}
		return netimmerse_cast<RE::NiPointLight*>(master->Clone());
	}

	// Registers a light already attached under a node and updated once: a dynamic light that casts no shadow, as ReLight
	// registers its own (field of view 90, the one a shadowless light is given). nullptr when the scene refused it - the
	// caller then detaches it. Main thread.
	[[nodiscard]] inline RE::BSLight* AddToScene(RE::ShadowSceneNode* a_scene, RE::NiPointLight* a_light, float a_falloff = 1.0f)
	{
		RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
		params.dynamic = true;
		params.shadowLight = false;
		params.portalStrict = true;
		params.affectLand = true;
		params.affectWater = true;
		params.neverFades = true;
		params.fov = 90.0f;
		params.falloff = a_falloff;
		params.nearDistance = 5.0f;
		params.depthBias = 1.0f;
		params.sceneGraphIndex = 0;
		params.restrictedNode = nullptr;
		params.lensFlareData = nullptr;
		return a_scene && a_light ? a_scene->AddLight(a_light, params) : nullptr;
	}

	[[nodiscard]] inline RE::NiColor NamedColor(int a_pick) noexcept
	{
		const auto c = NamedColorRgb(a_pick);
		return { static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
			static_cast<float>(c & 0xFF) / 255.0f };
	}

	// "Dim in daylight" (LightMath.h's DaylightShare): 1 at night and in the dark; a_pick 0 off, 1 a little, 2 more. Main thread:
	// it reads the player's cell (an interior by its own light, outdoors by the hour).
	[[nodiscard]] inline float Daylight(int a_pick)
	{
		auto* player = a_pick > 0 ? RE::PlayerCharacter::GetSingleton() : nullptr;
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return 1.0f;
		}
		float bright = 0.0f;
		if (cell->IsInteriorCell()) {
			if (const auto* l = cell->GetLighting()) {
				const auto lum = [](const RE::Color& a_c) {
					return (0.2126f * a_c.red + 0.7152f * a_c.green + 0.0722f * a_c.blue) / 255.0f;
				};
				bright = DaylightOfRoom((std::max)(lum(l->ambient), lum(l->directional)));
			}
		} else if (const auto* calendar = RE::Calendar::GetSingleton()) {
			bright = DaylightOfHour(calendar->GetHour());
		}
		return DaylightShare(a_pick, bright);
	}
}
