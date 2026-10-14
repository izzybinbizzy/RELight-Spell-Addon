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
  On by default. Spells from mods with no patch get one too, in the color of their own light or their element.
- **Community Shaders, ENB and Vanilla found by themselves** - nothing to pick. Off Community Shaders the hand lights are
  drawn for the game's own lighting and light the ground too.
- **Wards** - one dome per ward, and a ward color (Vanilla blue or White). All of it stands aside when Dynamic Wards is
  installed.
- **FleshFX** - with FleshFX installed, the flesh spells' lights take the color of its skins.
- **Lights off while sneaking** - every spell light, off by default.
- **Fading lights** - weapon, staff and bound weapon lights follow their charge, a spell's hand light your magicka
  (rule files in `SKSE\Plugins\RelightSpellAddon\Fading`).
- **Lights by Item** - search any spell, staff or weapon and switch its light off or give it a color of its own.
- **HUD** - hide the enchantment charge bar, a charge gem per weapon hand, and a spell reticle that drains with your magicka.
- **Presets, dim in daylight, and hand lights for (a budget for big fights)**.

Every change applies at once, even to a spell you are holding. Settings are in SKSE Menu Framework's Mod Control
Panel and are saved to `SKSE\Plugins\RelightSpellAddon.ini`. Values the menu does not show are in
`SKSE\Plugins\RelightSpellAddon\RELight - Spell Addon - Advanced Settings.ini`, written with every value and a note the
first time the game starts.

The held weapon lights are made the way ReLight by Truman makes its lights, with his permission.

Needs SKSE and RE::Light. Without SKSE Menu Framework there is no menu and the settings file still applies.

`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).
