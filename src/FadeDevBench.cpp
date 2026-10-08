// RELight - Spell Addon - the fading module (Illuminated's, ported 2026-10-08)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The Spell Addon has no DevBench glue (Illuminated's FadeDevBench.cpp offers `inspect kind=illuminatedfade`); these
// are what the fading module calls, and do nothing here.

#include "Fade.h"

namespace Fade
{
	void OfferToDevBench() {}
	void RulesLoaded() {}
}
