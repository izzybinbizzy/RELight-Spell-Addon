// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// How a rule file names a form, as plain text (no game types, so tests/test_fade.cpp checks it):
//   "Skyrim.esm|0x04E4EE"   plugin, bar, hexadecimal form ID (0x optional) - Light Placer's and Base Object Swapper's way
//   "0x04E4EE~Skyrim.esm"   the same, SPID's and KID's way
//   "DA09Dawnbreaker"       an editor ID

#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>

namespace FormText
{
	struct Spec
	{
		enum class Kind
		{
			kEditorID,
			kPluginAndID,
			kBad  // looked like plugin + ID, but the ID is not hexadecimal or the plugin is empty
		};
		Kind             kind{ Kind::kBad };
		std::string_view plugin;
		std::uint32_t    id{ 0 };
		std::string_view editorID;
	};

	[[nodiscard]] inline std::string_view Trim(std::string_view a_s) noexcept
	{
		while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t')) {
			a_s.remove_prefix(1);
		}
		while (!a_s.empty() && (a_s.back() == ' ' || a_s.back() == '\t')) {
			a_s.remove_suffix(1);
		}
		return a_s;
	}

	[[nodiscard]] inline bool ParseHex(std::string_view a_hex, std::uint32_t& a_out) noexcept
	{
		a_hex = Trim(a_hex);
		if (a_hex.starts_with("0x") || a_hex.starts_with("0X")) {
			a_hex.remove_prefix(2);
		}
		if (a_hex.empty() || a_hex.size() > 8) {
			return false;
		}
		const auto r = std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), a_out, 16);
		return r.ec == std::errc{} && r.ptr == a_hex.data() + a_hex.size();
	}

	[[nodiscard]] inline Spec Parse(std::string_view a_text) noexcept
	{
		Spec s;
		a_text = Trim(a_text);
		std::string_view plugin, hex;
		if (const auto bar = a_text.find('|'); bar != std::string_view::npos) {
			plugin = a_text.substr(0, bar);
			hex = a_text.substr(bar + 1);
		} else if (const auto tilde = a_text.find('~'); tilde != std::string_view::npos) {
			hex = a_text.substr(0, tilde);
			plugin = a_text.substr(tilde + 1);
		} else {
			s.kind = a_text.empty() ? Spec::Kind::kBad : Spec::Kind::kEditorID;
			s.editorID = a_text;
			return s;
		}
		s.plugin = Trim(plugin);
		if (s.plugin.empty() || !ParseHex(hex, s.id)) {
			s.kind = Spec::Kind::kBad;
			return s;
		}
		s.kind = Spec::Kind::kPluginAndID;
		return s;
	}

	// the part of a form ID that is the plugin's own: 12 bits in a light plugin, 24 in a full one. A load-order prefix
	// copied from a record editor ("0x0104E4EE", "0xFE012345") is dropped.
	[[nodiscard]] inline std::uint32_t LocalID(std::uint32_t a_id, bool a_lightPlugin) noexcept
	{
		return a_id & (a_lightPlugin ? 0x00000FFFu : 0x00FFFFFFu);
	}
}
