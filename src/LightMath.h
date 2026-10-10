// LightMath - the plain-number part of LightKit (Illuminated and RELight - Spell Addon both use it)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Nothing of the game in here, so tests/test_fade.cpp builds and checks it off the game (PC Runner\fade tests.py, against
// both mods' copies). LightKit.h includes it; the game-facing helpers stay there. Kept byte for byte the same in both mods
// by PC Runner\fade copies check.py, as LightKit.h is.
//
//   Lower, SameText    the one lower-casing helper and the one case-blind compare every file uses
//   CutoffFor          the cutoff of an inverse-square light (the build's K, as gen.py and relightgen.py write every config)
//   IslReach, PlainOf  how far such a light reaches, and the same light drawn by the game's own lighting (ENB, Vanilla)
//   NamedColorRgb      the ten named colors of the element color settings (Dynamic Wards' preset hues, the same order)
//   ToScreen           linear light <-> the screen's color (Dynamic Wards' wardgen.srgb)
//   DaylightShare      how bright the day or the room is, for "Dim in daylight"
//   KeyAt              a fade controller's value at a time (Truman's KeyframeSequence maths, RE::Light's controller.h)

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LightKit
{
	// a lower-case copy (ASCII: editor IDs, keys, file names)
	[[nodiscard]] inline std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// the same text whatever its case
	[[nodiscard]] inline bool SameText(std::string_view a, std::string_view b) noexcept
	{
		return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
			return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
		});
	}

	// The build's cutoff constant (0.8 * 69.99², as gen.py and relightgen.py write every config's cutoff) and the size a
	// light made here carries in its radius' z
	inline constexpr float kK = 3918.88f;
	inline constexpr float kLightSize = 1.414f;

	namespace Isl
	{
		inline constexpr std::uint32_t kFlag = 1u << 10;
		inline constexpr float         kLowestCutoff = 0.01f, kHighestCutoff = 0.99f;
	}

	// cutoff = K * fade / (reach² + size²), kept where Community Shaders reads a cutoff
	[[nodiscard]] inline float CutoffFor(float a_fade, float a_reach, float a_size = kLightSize) noexcept
	{
		return std::clamp(kK * a_fade / (a_reach * a_reach + a_size * a_size), Isl::kLowestCutoff, Isl::kHighestCutoff);
	}

	// How far an inverse-square light reaches (reach² = K x fade / cutoff - size²), and the same light drawn by the game's own
	// lighting (ENB, Vanilla): Dynamic Wards' house light - LTBG section 4's reach 133 drawn at radius 178 with fade 1.14
	// (wardgen.plain_light), never shorter than the radius the light already states. Illuminated's Lighting.cpp does the same.
	[[nodiscard]] inline float IslReach(float a_fade, float a_cutoff, float a_size = kLightSize) noexcept
	{
		if (a_cutoff <= 0.0f || a_fade <= 0.0f) {
			return 0.0f;
		}
		return std::sqrt((std::max)(kK * a_fade / a_cutoff - a_size * a_size, 0.0f));
	}
	struct Plain
	{
		float fade{ 0.0f }, radius{ 0.0f };
	};
	[[nodiscard]] inline Plain PlainOf(float a_fade, float a_radius, float a_cutoff, float a_size = kLightSize) noexcept
	{
		constexpr float kPlainReach = 178.0f / 133.0f, kPlainFade = 1.14f;
		return { a_fade * kPlainFade, (std::max)(a_radius, IslReach(a_fade, a_cutoff, a_size) * kPlainReach) };
	}

	// the element color settings' ten colors, 1 to kNamedColorCount (their names are each menu's)
	inline constexpr std::array<std::uint32_t, 10> kNamedColors = { 0xD0102E, 0xFF6A10, 0xFFC420, 0x2ED452, 0x00D2B0, 0x40DCFF,
		0x2468FF, 0x7A3CFF, 0xFF2EC4, 0xFFFFFF };
	inline constexpr int                           kNamedColorCount = static_cast<int>(kNamedColors.size());

	// one named color as 0xRRGGBB; a pick outside 1..kNamedColorCount takes the nearest end
	[[nodiscard]] constexpr std::uint32_t NamedColorRgb(int a_pick) noexcept
	{
		return kNamedColors[static_cast<std::size_t>(std::clamp(a_pick, 1, kNamedColorCount) - 1)];
	}

	// linear light <-> the screen's color, per channel (0..1)
	[[nodiscard]] inline float ToScreen(float a_x) noexcept
	{
		const float x = std::clamp(a_x, 0.0f, 1.0f);
		return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
	}
	[[nodiscard]] inline float ToLinear(float a_x) noexcept
	{
		const float x = std::clamp(a_x, 0.0f, 1.0f);
		return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
	}
	// the same for one 0..255 channel
	[[nodiscard]] inline std::uint8_t ScreenChannel(std::uint8_t a_linear) noexcept
	{
		const float v = ToScreen(static_cast<float>(a_linear) / 255.0f);
		return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
	}

	// "Dim in daylight": how bright it is, 0 (night, a dark room) to 1 (full day, a bright room). Outdoors it follows the
	// hour (full day 8 to 17, a three-hour dawn and dusk); indoors the room's own light, as the luminance (0..1) of its
	// ambient or directional color, whichever is brighter.
	[[nodiscard]] inline float DaylightOfHour(float a_hour) noexcept
	{
		if (a_hour < 5.0f || a_hour >= 20.0f) {
			return 0.0f;
		}
		if (a_hour < 8.0f) {
			return (a_hour - 5.0f) / 3.0f;
		}
		if (a_hour < 17.0f) {
			return 1.0f;
		}
		return (20.0f - a_hour) / 3.0f;
	}
	[[nodiscard]] inline float DaylightOfRoom(float a_luminance) noexcept
	{
		return std::clamp((a_luminance - 0.15f) / 0.3f, 0.0f, 1.0f);
	}
	// the share the lights keep: a_pick 0 off, 1 a little (at most a quarter dimmer), 2 more (at most half)
	[[nodiscard]] inline float DaylightShare(int a_pick, float a_bright) noexcept
	{
		if (a_pick <= 0) {
			return 1.0f;
		}
		const float most = a_pick == 1 ? 0.25f : 0.5f;
		return 1.0f - most * std::clamp(a_bright, 0.0f, 1.0f);
	}

	// A fade controller's value at a_time (inside its loop): Truman's KeyframeSequence::Interpolate (RE::Light,
	// controller.h, GPL-3.0; his permission for RELight's code, credited) - step (0), linear (1) or cubic Hermite (2; the
	// configs give no tangents, so they are 0). a_keys: (time, value), sorted by time; empty gives 0.
	[[nodiscard]] inline float KeyAt(const std::vector<std::pair<float, float>>& a_keys, int a_interpolation, float a_time) noexcept
	{
		if (a_keys.empty()) {
			return 0.0f;
		}
		const auto next = std::ranges::upper_bound(a_keys, a_time, {}, &std::pair<float, float>::first);
		if (next == a_keys.begin()) {
			return a_keys.front().second;
		}
		if (next == a_keys.end()) {
			return a_keys.back().second;
		}
		const auto& [t0, v0] = *std::prev(next);
		const auto& [t1, v1] = *next;
		const float dt = t1 - t0;
		if (dt <= 0.0f || a_interpolation == 0) {
			return v0;
		}
		const float u = (a_time - t0) / dt;
		if (a_interpolation == 1) {
			return (1.0f - u) * v0 + u * v1;
		}
		const float u2 = u * u, u3 = u2 * u;
		return (2.0f * u3 - 3.0f * u2 + 1.0f) * v0 + (-2.0f * u3 + 3.0f * u2) * v1;
	}
}
