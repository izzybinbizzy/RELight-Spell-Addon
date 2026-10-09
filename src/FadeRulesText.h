// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Rule files as text, with nothing of the game in it (tests/test_fade.cpp reads them off the game): a file's rules,
// every problem in it, and how a matching rule's settings lay over the ones before it. Rules.cpp finds the files,
// resolves "Plugin.esp|0x123" to a form in this load order (through the Resolve it hands in), and matches weapons.
// The format is in docs/RULES.md.
//
//   { "rules": [ { "name": "Dawnbreaker always glows", "weapons": ["Skyrim.esm|0x04E4EE"], "mode": "exempt" } ] }

#pragma once

#include "FadeFormText.h"
#include "FadeGlow.h"
#include "FadeSettingsText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fade::RulesText
{
	// a form named in a rule: resolved to a form ID at load, or kept as a lower-case editor ID
	struct FormRef
	{
		std::uint32_t id{ 0 };
		std::string   editorID;
	};

	struct Rule
	{
		std::string              name;  // "file.json #3: name"
		std::vector<FormRef>     weapons, enchantments;
		std::vector<std::string> weaponKeywords, effectKeywords;  // lower case
		std::optional<bool>      bound, staff;

		std::optional<Mode>  mode;
		std::optional<float> floor, reachFollows, sputterBelow, sputterStrength, pulseStrength, flareStrength, coolAmount,
			boundFadeSeconds;
		std::optional<Glow::Curve>    curve;
		std::optional<Glow::CoolTint> coolTint;
		std::optional<bool>           sputter, emptySteady, pulse, flare, cool;
	};

	// the form a plugin-and-ID names in this load order, or nothing when that plugin or record is not installed
	using Resolve = std::function<std::optional<std::uint32_t>(std::string_view a_plugin, std::uint32_t a_id)>;

	struct Loaded
	{
		std::vector<Rule>        rules;
		std::vector<std::string> problems;  // what is wrong in a file: shown on the menu's Debug page and logged as warnings
		std::vector<std::string> notes;     // what is fine but worth a log line (a form from a plugin not installed)
	};

	[[nodiscard]] inline std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	namespace detail
	{
		using json = nlohmann::json;

		struct Reader
		{
			Loaded&          out;
			const Resolve&   resolve;
			std::string_view where;

			void Problem(std::string a_text) { out.problems.push_back(std::string(where) + ": " + std::move(a_text)); }

			// a list of strings ("weapons": ["A", "B"]), or one string on its own; anything else is a problem
			template <class F>
			bool Strings(const json& a_rule, const char* a_key, F&& a_each)
			{
				if (!a_rule.contains(a_key)) {
					return false;
				}
				const auto& v = a_rule[a_key];
				if (v.is_string()) {
					a_each(v.get<std::string>());
				} else if (v.is_array()) {
					for (const auto& e : v) {
						if (e.is_string()) {
							a_each(e.get<std::string>());
						} else {
							Problem("every entry of \"" + std::string(a_key) + "\" must be a string");
						}
					}
				} else {
					Problem("\"" + std::string(a_key) + "\" must be a list of strings");
				}
				return true;
			}

			std::vector<FormRef> Forms(const json& a_rule, const char* a_key, bool& a_named)
			{
				std::vector<FormRef> refs;
				a_named = Strings(a_rule, a_key, [&](const std::string& a_text) {
					const auto spec = FormText::Parse(a_text);
					switch (spec.kind) {
					case FormText::Spec::Kind::kEditorID:
						refs.push_back({ 0, Lower(spec.editorID) });
						break;
					case FormText::Spec::Kind::kBad:
						Problem("\"" + a_text + "\" - not \"Plugin.esp|0x123\", \"0x123~Plugin.esp\" or an editor ID");
						break;
					default:
						if (const auto id = resolve ? resolve(spec.plugin, spec.id) : std::nullopt) {
							refs.push_back({ *id, {} });
						} else {
							// not an error: the rule's plugin may simply not be installed
							out.notes.push_back(std::string(where) + ": \"" + a_text + "\" is not in this load order; that entry is skipped");
						}
					}
				});
				return refs;
			}

			std::vector<std::string> Keywords(const json& a_rule, const char* a_key)
			{
				std::vector<std::string> words;
				Strings(a_rule, a_key, [&](const std::string& a_text) { words.push_back(Lower(a_text)); });
				return words;
			}

			std::optional<bool> Bool(const json& a_rule, const char* a_key)
			{
				if (!a_rule.contains(a_key)) {
					return std::nullopt;
				}
				if (!a_rule[a_key].is_boolean()) {
					Problem("\"" + std::string(a_key) + "\" must be true or false");
					return std::nullopt;
				}
				return a_rule[a_key].get<bool>();
			}

			std::optional<std::string> Text(const json& a_rule, const char* a_key)
			{
				if (!a_rule.contains(a_key)) {
					return std::nullopt;
				}
				if (!a_rule[a_key].is_string()) {
					Problem("\"" + std::string(a_key) + "\" must be text");
					return std::nullopt;
				}
				return a_rule[a_key].get<std::string>();
			}

			// a number in [a_lo, a_hi], clamped; true/false or text is a problem, not a number
			std::optional<float> Number(const json& a_rule, const char* a_key, float a_lo, float a_hi)
			{
				if (!a_rule.contains(a_key)) {
					return std::nullopt;
				}
				if (!a_rule[a_key].is_number()) {
					Problem("\"" + std::string(a_key) + "\" must be a number");
					return std::nullopt;
				}
				return static_cast<float>(std::clamp(a_rule[a_key].get<double>(), static_cast<double>(a_lo), static_cast<double>(a_hi)));
			}

			// a percent field, 0..a_hi, as a fraction
			std::optional<float> Percent(const json& a_rule, const char* a_key, float a_hi = 100.0f)
			{
				const auto v = Number(a_rule, a_key, 0.0f, a_hi);
				return v ? std::optional<float>(*v / 100.0f) : std::nullopt;
			}

			template <class E>
			std::optional<E> Choice(const json& a_rule, const char* a_key, std::initializer_list<std::pair<std::string_view, E>> a_names,
				std::string_view a_allowed)
			{
				const auto text = Text(a_rule, a_key);
				if (!text) {
					return std::nullopt;
				}
				const auto low = Lower(*text);
				for (const auto& [name, value] : a_names) {
					if (low == name) {
						return value;
					}
				}
				Problem(std::string(a_key) + " \"" + *text + "\" is not " + std::string(a_allowed));
				return std::nullopt;
			}
		};

		inline void ReadRule(const json& a_rule, std::string_view a_file, std::size_t a_index, const Resolve& a_resolve, Loaded& a_out)
		{
			Rule r;
			r.name = std::string(a_file) + " #" + std::to_string(a_index + 1);
			if (a_rule.contains("name") && a_rule["name"].is_string()) {
				r.name += ": " + a_rule["name"].get<std::string>();
			}
			Reader rd{ a_out, a_resolve, r.name };
			if (a_rule.contains("name") && !a_rule["name"].is_string()) {
				rd.Problem("\"name\" must be text");
			}

			// a misspelt key would otherwise do nothing, silently
			static constexpr std::string_view kKnown[] = { "name", "weapons", "enchantments", "weaponKeywords", "effectKeywords", "bound",
				"staff", "mode", "emptyBrightness", "curve", "reachFollows", "sputter", "sputterBelow", "sputterStrength", "emptySteady",
				"hitPulse", "hitPulseStrength", "rechargeFlare", "rechargeFlareStrength", "colorCooling", "colorCoolingAmount",
				"colorCoolingTint", "boundFadeSeconds", "comment" };
			for (const auto& item : a_rule.items()) {
				if (std::ranges::find(kKnown, std::string_view(item.key())) == std::end(kKnown)) {
					rd.Problem("unknown key \"" + item.key() + "\" (ignored)");
				}
			}

			bool namedWeapons = false, namedEnchantments = false;
			r.weapons = rd.Forms(a_rule, "weapons", namedWeapons);
			r.enchantments = rd.Forms(a_rule, "enchantments", namedEnchantments);
			r.weaponKeywords = rd.Keywords(a_rule, "weaponKeywords");
			r.effectKeywords = rd.Keywords(a_rule, "effectKeywords");
			r.bound = rd.Bool(a_rule, "bound");
			r.staff = rd.Bool(a_rule, "staff");

			r.mode = rd.Choice<Mode>(a_rule, "mode", { { "charge", Mode::kCharge }, { "exempt", Mode::kExempt }, { "bound", Mode::kBound } },
				"charge, exempt or bound");
			r.curve = rd.Choice<Glow::Curve>(a_rule, "curve",
				// "steep" was a third curve in early builds: an old rule file reads it as gentle
				{ { "linear", Glow::Curve::kLinear }, { "gentle", Glow::Curve::kGentle }, { "steep", Glow::Curve::kGentle } },
				"linear or gentle");
			r.coolTint = rd.Choice<Glow::CoolTint>(a_rule, "colorCoolingTint",
				{ { "grey", Glow::CoolTint::kGrey }, { "gray", Glow::CoolTint::kGrey }, { "ember", Glow::CoolTint::kEmber } }, "grey or ember");
			// the same ranges as the settings file and the menu (SettingsText.h)
			r.floor = rd.Percent(a_rule, "emptyBrightness", 50.0f);
			r.reachFollows = rd.Percent(a_rule, "reachFollows");
			if (const auto below = rd.Number(a_rule, "sputterBelow", 1.0f, 50.0f)) {
				r.sputterBelow = *below / 100.0f;
			}
			r.sputterStrength = rd.Percent(a_rule, "sputterStrength");
			r.pulseStrength = rd.Percent(a_rule, "hitPulseStrength", 200.0f);
			r.flareStrength = rd.Percent(a_rule, "rechargeFlareStrength", 200.0f);
			r.coolAmount = rd.Percent(a_rule, "colorCoolingAmount");
			r.boundFadeSeconds = rd.Number(a_rule, "boundFadeSeconds", 1.0f, 60.0f);
			r.sputter = rd.Bool(a_rule, "sputter");
			r.emptySteady = rd.Bool(a_rule, "emptySteady");
			r.pulse = rd.Bool(a_rule, "hitPulse");
			r.flare = rd.Bool(a_rule, "rechargeFlare");
			r.cool = rd.Bool(a_rule, "colorCooling");
			// read to the end first, so every problem in it is reported, then: a rule that names forms none of which are
			// installed must match nothing, not everything
			if ((namedWeapons && r.weapons.empty()) || (namedEnchantments && r.enchantments.empty())) {
				a_out.notes.push_back(r.name + " names no installed form; skipped");
				return;
			}
			a_out.rules.push_back(std::move(r));
		}
	}

	// One file's text: its rules appended to a_out, in the order written. False when the file is not a rule file at all
	// (not JSON, or no "rules" list); the reason is then one of a_out's problems. Comments are allowed.
	inline bool ReadFile(std::string_view a_text, std::string_view a_file, const Resolve& a_resolve, Loaded& a_out)
	{
		detail::json doc;
		try {
			doc = detail::json::parse(a_text.begin(), a_text.end(), nullptr, true, true);
		} catch (const detail::json::exception& e) {  // parse_error, and out_of_range for a number past a double (1e400)
			a_out.problems.push_back(std::string(a_file) + ": not valid JSON (" + e.what() + ")");
			return false;
		}
		if (!doc.is_object() || !doc.contains("rules") || !doc["rules"].is_array()) {
			a_out.problems.push_back(std::string(a_file) + ": no \"rules\" list");
			return false;
		}
		std::size_t i = 0;
		for (const auto& rule : doc["rules"]) {
			if (rule.is_object()) {
				detail::ReadRule(rule, a_file, i, a_resolve, a_out);
			} else {
				a_out.problems.push_back(std::string(a_file) + " #" + std::to_string(i + 1) + ": a rule must be an object");
			}
			++i;
		}
		return true;
	}

	// a matching rule's settings, laid over what the settings and earlier rules gave: whatever it sets replaces them
	inline void Overlay(const Rule& a_r, Verdict& a_v)
	{
		const auto take = [](auto& a_to, const auto& a_from) {
			if (a_from) {
				a_to = *a_from;
			}
		};
		auto& t = a_v.tuning;
		take(a_v.mode, a_r.mode);
		take(t.floor, a_r.floor);
		take(t.curve, a_r.curve);
		take(t.reachFollows, a_r.reachFollows);
		take(t.sputter, a_r.sputter);
		take(t.sputterBelow, a_r.sputterBelow);
		take(t.sputterStrength, a_r.sputterStrength);
		take(t.emptySteady, a_r.emptySteady);
		take(t.pulse, a_r.pulse);
		take(t.pulseStrength, a_r.pulseStrength);
		take(t.flare, a_r.flare);
		take(t.flareStrength, a_r.flareStrength);
		take(t.cool, a_r.cool);
		take(t.coolAmount, a_r.coolAmount);
		take(t.coolTint, a_r.coolTint);
		take(a_v.boundFadeSeconds, a_r.boundFadeSeconds);
		a_v.why = a_r.name;
	}
}
