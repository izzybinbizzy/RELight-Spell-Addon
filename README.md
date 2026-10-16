# RELight - Spell Addon - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `LICENSE`.

The plugin of RELight - Spell Addon; it ships with the mod and is required by it. RE::Light does the lighting; this
plugin adds what RE::Light cannot do from a config.

## What it does
- **Hand lights** - a light on your hands while you cast, made in memory, with Dynamic Lighting. Spells from mods with no
  patch get one too, in the color of their own light or their element.
- **Held weapon lights** - a staff or weapon this mod lights on the ground carries its light in your hand while drawn
  (made the way ReLight by Truman makes its lights, with his permission).
- **Sprays and bolts lit along their length** - each wears a raised copy of its own light record.
- **Community Shaders, ENB and Vanilla found by themselves** - off Community Shaders the hand lights are drawn for the
  game's own lighting and light the ground too.
- A switch for every installed option and patch, Brightness and Reach, presets, Lights by Item, Fading, the HUD (reticle
  and charge gems), lights off while sneaking, dim in daylight, a budget for big fights, wards (stepping aside for
  Dynamic Wards), FleshFX colors and VAER Reborn's swirls kept.

## Settings
SKSE Menu Framework > RELight - Spell Addon, saved to `SKSE\Plugins\RelightSpellAddon.ini`. Values the menu does not
show are in `RELight - Spell Addon - Advanced Settings.ini`. Every change applies at once.

## Requires
SKSE, Address Library for SKSE Plugins, RE::Light and SKSE Menu Framework (the settings page and the HUD).

## Third-party files
`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).
