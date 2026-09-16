# Aetherial Radiance Sneak - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `../LICENSE`.

Turns spell lights off while the player sneaks and back on when they stand up: lights the game makes
for magic effects are not created while sneaking, lights on loaded projectiles, explosions and hazards
are culled, and everything culled is restored when sneaking ends.

## Credits

The four call sites where the game creates a magic light (relocation IDs and offsets in `Install()`)
were taken from RE::Light by Truman. Thank you.
