// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Illuminated's own light on an enchanted weapon no mod lights is NOT in the Spell Addon: that is enchantment coverage,
// and RELight's rule is "ENCHANTMENTS ARE TRUMAN'S ... do not build enchantment coverage on this side" (its CLAUDE.md).
// The fading module asks for one through these functions; here none is ever made, so a weapon RE::Light lights fades
// with its charge and one it does not light stays as it is.

#include "Fade.h"
#include "Plugin.h"

namespace Fade
{
	RE::NiPointLight* KeepOwnLight(std::uint64_t, RE::NiAVObject*, const RE::EnchantmentItem*) { return nullptr; }
	void              TintOwnLight(std::uint64_t, const RE::NiColor&) {}
	void              SweepOwnLights() {}
	void              DropOwnLights() {}
	std::size_t       OwnLightCount() { return 0; }
	const char*       OwnLightLighting() { return Plugin::LightingName(); }
}
