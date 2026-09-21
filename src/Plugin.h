// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What the files share. Three jobs live in this plugin:
//   main.cpp     the hooks, and the lights going out while you sneak
//   Options.cpp  THE PROBE: one installer option switched on and off while the game runs
//   Menu.cpp     the one tick box that switches it, in SKSE Menu Framework's Mod Control Panel

#pragma once

namespace Plugin
{
	// ---------------------------------------------------------------- the probe
	//
	// WHAT IT IS ASKING. Every option this mod installs is a set of mesh names, and the light RE::Light
	// makes for one of those meshes hangs off that object's own 3D. RE::Light has no conditions, so an
	// option cannot be switched off in a config the way the Light Placer build does it - the only route
	// is this plugin finding those lights at run time and putting them out, which is the same thing the
	// sneaking feature already does, pointed at an option instead of at crouching.
	//
	// The question only the game can answer is whether that LOOKS right: whether a light that is already
	// lit goes quietly when the box is unticked, and whether one that should stay off ever shows for a
	// frame when its object loads. One option is enough to answer both.
	//
	// ONE option, hardcoded, and it is Runes - twelve mesh names, read off the built archive. A rune sits
	// on the ground and stays lit, so it can be watched while the box is ticked and unticked; the fireball
	// explosion flash is in the same option, so a Fireball answers the loading half.

	// 🌙 LIGHTS OFF WHILE SNEAKING - the mod's one shipped feature, a SETTING now rather than an
	// installer option. HIS CALL, 2026-09-21: the plugin always installs, so the choice moved in here.
	// ⛛ Default OFF, which is what the installer's unticked option meant, so nobody's game changes.
	bool SneakOn();
	void SetSneakOn(bool a_on);

	bool RunesOn();
	void SetRunesOn(bool a_on);

	// how many of this option's lights are lit right now, and how many the probe is holding out, so the
	// menu can say what it is doing instead of leaving it to be guessed
	std::size_t RunesLit();
	std::size_t RunesHeldOut();

	// Called from the player update, every frame, after the sneaking pass.
	void UpdateOptionLights();

	// Called from the Load3D hooks, on the 3D that has just loaded, before it can be drawn. This is the
	// half that decides whether a switched-off light ever shows for a frame.
	void CullOptionLightsUnder(RE::NiAVObject* a_root);

	// true when this plugin put that light out for an option, so the sneaking pass does not turn it back on
	bool HeldOutForOption(RE::NiLight* a_light);

	void RegisterMenu();
}
