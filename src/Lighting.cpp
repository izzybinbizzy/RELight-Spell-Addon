// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Which lighting the game draws with (the same three picks as Dynamic Wards and Illuminated), and how a light made for
// Community Shaders' inverse square lighting is drawn without it.
//
//   Community Shaders   inverse square: a light's reach comes from its fade and cutoff (CutoffFor)
//   ENB, Vanilla        the game's own lighting: the inverse square words are ambient colour there, and a light reaches its
//                       radius. Our lights are drawn plain - the reach they have under Community Shaders at Dynamic Wards'
//                       house light (reach 133 drawn at radius 178, fade x 1.14), ambient a tenth of the colour.
//
// The pick is one word in `SKSE\Plugins\RelightSpellAddon\Lighting.txt` (cs, enb, vanilla), which the installer's option
// installs. With no file the game is looked at: Community Shaders' inverse square shader, else an ENB's settings in the
// game folder, else Vanilla. Inverse square only on the Community Shaders pick with its shader there.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kIslShader = "Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli";
		constexpr const char* kPickPath = "Data/SKSE/Plugins/RelightSpellAddon/Lighting.txt";
		constexpr float       kPlainReach = 178.0f / 133.0f;
		constexpr float       kPlainFade = 1.14f;
		constexpr float       kAmbient = 0.1f;
		constexpr const char* kNames[] = { "Community Shaders", "ENB", "Vanilla" };

		std::atomic<int>  gPick{ 0 };
		std::atomic<bool> gFromFile{ false };
		std::atomic<bool> gIslShader{ false };

		// the first word of the first line that has one, lower case; "" for none
		std::string FirstWord()
		{
			std::ifstream in(kPickPath);
			std::string   line;
			while (in && std::getline(in, line)) {
				if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
					line.erase(0, 3);  // a UTF-8 BOM some editors write
				}
				std::string word;
				for (const char c : line) {
					if (c == '#') {
						break;
					}
					if (!std::isspace(static_cast<unsigned char>(c))) {
						word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					} else if (!word.empty()) {
						break;
					}
				}
				if (!word.empty()) {
					return word;
				}
			}
			return {};
		}
	}

	void ReadLighting()
	{
		std::error_code ec;
		gIslShader = std::filesystem::exists(kIslShader, ec);
		const auto word = FirstWord();
		int        pick = -1;
		if (word == "cs" || word == "communityshaders" || word == "shaders") {
			pick = 0;
		} else if (word == "enb") {
			pick = 1;
		} else if (word == "vanilla") {
			pick = 2;
		} else if (!word.empty()) {
			SKSE::log::warn("lighting: Lighting.txt says '{}', which is not cs, enb or vanilla; the game is looked at instead", word);
		}
		gFromFile = pick >= 0;
		if (pick < 0) {
			const bool enb = std::filesystem::exists("enbseries.ini", ec) || std::filesystem::exists("enblocal.ini", ec);
			pick = gIslShader ? 0 : enb ? 1 : 2;
		}
		gPick = pick;
		SKSE::log::info("lighting: {} ({}); inverse square shader {}; our lights drawn {}", kNames[pick],
			gFromFile ? "the installer's pick" : "no Lighting.txt, looked at the game", gIslShader ? "installed" : "not installed",
			IslLighting() ? "inverse square" : "plain");
	}

	bool IslLighting() { return gPick.load() == 0 && gIslShader.load(); }

	const char* LightingName() { return kNames[gPick.load()]; }

	float PlainRadius(float a_reach) { return a_reach * kPlainReach; }

	float PlainFade(float a_fade) { return a_fade * kPlainFade; }

	RE::NiColor PlainAmbient(const RE::NiColor& a_colour)
	{
		return { a_colour.red * kAmbient, a_colour.green * kAmbient, a_colour.blue * kAmbient };
	}
}
