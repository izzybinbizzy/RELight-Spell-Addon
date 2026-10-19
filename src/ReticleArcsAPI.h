#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chiktabba
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <cstdint>

// Reticle Arcs add-on interface. UNSUPPORTED FOR NOW: it may change without notice while it settles.
//
// an add-on is an SKSE plugin showing its own bar beside Reticle Arcs' own. Reticle Arcs draws it in the player's look and
// asks the add-on for its state every frame; the add-on only reads the game. no CommonLib types cross it, so add-ons don't
// need the same CommonLib build. a crash inside an add-on's callback stops that add-on, not the game
namespace ReticleArcsAPI {
    inline constexpr std::uint32_t kVersion = 1;

    // with drain, what moves along the fill
    enum class Motion : std::uint8_t {
        kNone,
        // a soft spot running along it
        kFlow,
        // the fill breathing brighter
        kPulse,
        // motes drifting off its end
        kMotes,
        // whatever the player picked for concentration spells
        kPlayers,
    };

    // what the bar shows this frame. Reticle Arcs hands it over zeroed; later versions only add fields at the end
    struct BarState {
        // 0 to 1
        float percent = 0.0f;
        // shown. going from shown to not plays the bar's release effect
        bool active = false;
        // empties towards its start and follows the value without easing, like a concentration spell. rises still ease,
        // and reaching full that way plays the bar's charged effect
        bool drain = false;
        Motion motion = Motion::kNone;
        // split into this many pieces, like a shout's words. 0 or 1 for one piece, 12 at most
        std::uint32_t segments = 0;
        // in the warning colour
        bool low = false;
        // the track pulses in the warning colour
        bool alarm = false;
        // a rim in the warning colour, like the bow's for its last arrow
        bool rim = false;
        // this colour, 0xRRGGBB, instead of the player's for this bar
        bool useColor = false;
        std::uint32_t color = 0;
        // for one frame while not shown: the warning flash, as for a spell that can't be afforded
        bool refused = false;
        // for the frame it stops showing: the player's break effect instead of the release, as for a lost charge
        bool broken = false;
    };

    struct BarDefinition {
        // leave as is: tells Reticle Arcs which version of this struct it was given
        std::uint32_t size = sizeof(BarDefinition);
        // unique, letters, digits, _ and - only, 48 at most. the ini section is [AddOn.<id>]. start it with your mod's name
        const char* id = nullptr;
        // the menu's label and tooltip, the tooltip may be nullptr. Translation.ini's [AddOns] <id> and <id>Tip replace them
        const char* name = nullptr;
        const char* tooltip = nullptr;
        // defaults until the player changes them: fill as 0xRRGGBB and 0-255 opacity, a side
        std::uint32_t color = 0xFFFFFF;
        std::uint8_t alpha = 200;
        // top, bottom, left or right
        const char* placement = "bottom";
        bool enabled = true;
        // main thread, every frame the game runs and no kill move or killcam is playing. seconds since the last call.
        // keep it cheap, the frame waits for it
        void (*update)(float a_delta, BarState& a_state) = nullptr;
        // after a save loads, may be nullptr
        void (*reset)() = nullptr;
        // the settings page's preview, seconds since it opened. nullptr shows a plain sweep
        void (*preview)(float a_time, BarState& a_state) = nullptr;
    };

    class IReticleArcs {
    public:
        // the newest version this Reticle Arcs offers
        virtual std::uint32_t GetVersion() const = 0;
        // during SKSE's kPostLoad message. false when refused, the reason goes to ReticleArcs.log
        virtual bool RegisterBar(const BarDefinition& a_bar) = 0;

    protected:
        ~IReticleArcs() = default;
    };

    // exported by ReticleArcs.dll
    using RequestFunction = IReticleArcs* (*)(std::uint32_t a_version);

    // nullptr when Reticle Arcs isn't installed or is older than this header
    inline IReticleArcs* Request() {
        auto* module = ::GetModuleHandleW(L"ReticleArcs.dll");
        auto* request = module ? reinterpret_cast<RequestFunction>(::GetProcAddress(module, "RequestReticleArcsAPI")) : nullptr;
        return request ? request(kVersion) : nullptr;
    }
}
