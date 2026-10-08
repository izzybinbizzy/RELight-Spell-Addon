# RELight - Spell Addon - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `LICENSE`.

Ships with RELight - Spell Addon and is required by it.

- **Brightness** and **Reach** - sliders for this mod's lights only.
- **A switch for each option you installed** - untick one and its lights go out; mod patches have
  their own page, by category and author.
- **Sprays, breath shouts and beams** - RE::Light lights them; the sliders and switches reach their lights too.
- **Held weapon lights** - a staff or weapon this mod lights on the ground carries its light in your hand while it is drawn,
  which RE::Light only does for an enchantment.
- **VAER Reborn's swirls kept** - with VAER Reborn and its option installed, every effect VAER dresses gets VAER's swirl
  back when a later plugin (Thaumaturgy, Artificer) replaced it, and Thaumaturgy's own Fear, Paralyze, Turn Undead,
  Banish, Silent Moons and second Absorb effects wear VAER's swirl too.
- **Hand lights** - a light on your hands while you cast, made in memory, with Dynamic Lighting. No plugin, no script.
  On by default.
- **Lights off while sneaking**, off by default.
- **Fading lights** - weapon, staff and bound weapon lights follow their charge, a spell's hand light your magicka
  (rule files in `SKSE\Plugins\RelightSpellAddon\Fading`).
- **Presets, dim in daylight, light colors, Fire / Frost / Shock colors, hand lights for (a budget for big fights)** and a
  hand light for every spell no patch covers.
- **Community Shaders, ENB or Vanilla** - found by itself or picked in the menu; our lights are drawn for it.

Every change applies at once, even to a spell you are holding. Settings are in SKSE Menu Framework's Mod Control
Panel and are saved to `SKSE\Plugins\RelightSpellAddon.ini`.

The held weapon lights are made the way ReLight by Truman makes its lights, with his permission.

Needs SKSE and RE::Light. Without SKSE Menu Framework there is no menu and the settings file still applies.

`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).
