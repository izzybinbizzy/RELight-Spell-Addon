// RELight - Spell Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Which lighting the game draws with - Illuminated's Lighting.cpp, the same tests in the same order (2.2, his order "make sure
// relight is structurally up to par with illuminated"). RE::Light stays the main driver: only the lights that cannot work
// without knowing the lighting read it (his 2026-10-10 word) - the hand lights and their ground twins, the held and stream lights.
//
//   Community Shaders   inverse square lighting: a light's reach is worked out from its fade and its cutoff
//   ENB                 } the game's own lighting: the inverse square words are ambient color, a light reaches its radius. The
//   Vanilla             } hand, held and stream lights are drawn plain (LightKit::PlainOf)
//
// 🔁 HIS WORD: "re add the enb and cs and vanilla versions but don't make them manually selectable, just like illuminated it will
// auto detect". The game is looked at once, at data load: Community Shaders (its inverse square shader, Light Limit Fix's
// built-in one, or CommunityShaders.dll loaded) -> Community Shaders; an ENB that is running -> ENB; else Vanilla. ENB's settings
// files alone never make it ENB: an ENB preset left in the game folder with no ENB running (his RELight profile with Open Shaders
// unticked) drew Vanilla lights at ENB's strength, rows ~6x too dim. An ENB under another file name is found by its export.
//
// COMMUNITY SHADERS' TWO WORDS (2.1's user report 2026-10-14, Bottled Shaders + KittyVFX: frost and shock hand lights dark):
// Light Limit Fix reads the first two words of EVERY light's runtime data as its own flags and cutoff
// (LLFCommon::RuntimeLightDataExt - bit 9 Disabled, bit 10 InverseSquare), whatever the shader files say. 2.1 looked only for the
// separate shader file (RE::Light's own test), which Bottled Shaders 1.0.10+ no longer has, drew its hand lights plain, and the
// ambient color written there read as flags (a red of 81-122 sets Disabled). So whenever CommunityShaders.dll is loaded
// (Bottled Shaders keeps the name) the lights are written in Community Shaders' words, never with an ambient color; and leftover
// ENB settings never make it ENB while Community Shaders runs (the same report).

#include "Plugin.h"

#include <Windows.h>
#include <TlHelp32.h>

namespace Plugin
{
	namespace
	{
		constexpr const char* kIslShader = "Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli";
		// a Community Shaders that builds inverse square lights into Light Limit Fix (Bottled Shaders 1.0.10 dropped the
		// separate shader above): its attenuation reads each light's InverseSquare flag
		constexpr const char* kLlfAttenuation = "Data/Shaders/LightLimitFix/Attenuation.hlsli";
		constexpr const char* kEnbFiles[] = { "enbseries.ini", "enblocal.ini" };

		std::atomic<Lighting> gFound{ Lighting::kVanilla };  // until ReadLighting (RELight's default before 2.2; Illuminated's is kShaders)
		std::atomic<bool>     gIslShader{ false };           // the lights are written in Community Shaders' inverse square words
		std::atomic<bool>     gCsLoaded{ false }, gEnbRunning{ false }, gEnbFiles{ false };

		[[nodiscard]] bool EnbFilesLeft()
		{
			std::error_code ec;
			return std::ranges::any_of(kEnbFiles, [&](const char* a_file) { return std::filesystem::exists(std::filesystem::current_path() / a_file, ec); });
		}

		[[nodiscard]] bool IslBuiltIntoLightLimitFix()
		{
			std::ifstream in(std::filesystem::current_path() / kLlfAttenuation, std::ios::binary);
			if (!in) {
				return false;
			}
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			return text.find("InverseSquare") != std::string::npos;
		}

		// Community Shaders, or a fork that keeps its DLL name (Bottled Shaders)
		[[nodiscard]] bool CommunityShadersLoaded() { return REX::W32::GetModuleHandleA("CommunityShaders.dll") != nullptr; }

		// an ENB that is running: a loaded module exports its SDK - d3d11.dll as usual, or any other name an injector gave it
		[[nodiscard]] bool EnbRunning()
		{
			for (const char* dll : { "d3d11.dll", "dxgi.dll", "d3d9.dll" }) {
				if (const auto handle = REX::W32::GetModuleHandleA(dll); handle && REX::W32::GetProcAddress(handle, "ENBGetSDKVersion")) {
					return true;
				}
			}
			const auto snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
			if (snap == INVALID_HANDLE_VALUE) {
				return false;
			}
			bool           found = false;
			MODULEENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			for (bool more = ::Module32FirstW(snap, &entry); more && !found; more = ::Module32NextW(snap, &entry)) {
				found = ::GetProcAddress(entry.hModule, "ENBGetSDKVersion") != nullptr;
			}
			::CloseHandle(snap);
			return found;
		}
	}

	void ReadLighting()
	{
		std::error_code ec;
		const bool      shaderFile = std::filesystem::exists(std::filesystem::current_path() / kIslShader, ec);
		const bool      inLlf = IslBuiltIntoLightLimitFix();
		gCsLoaded = CommunityShadersLoaded();
		gIslShader = shaderFile || inLlf || gCsLoaded;
		gEnbRunning = EnbRunning();
		gEnbFiles = EnbFilesLeft();
		gFound = gIslShader ? Lighting::kShaders : gEnbRunning ? Lighting::kEnb :
		                                                         Lighting::kVanilla;
		SKSE::log::info(
			"lighting: {} (detected - CommunityShaders.dll {}, inverse square shader {}, in Light Limit Fix {}, ENB running {}, ENB "
			"settings in the game folder {}{}); lights drawn {}",
			LightingName(gFound), gCsLoaded ? "loaded" : "not loaded", shaderFile ? "installed" : "not installed", inLlf ? "yes" : "no",
			gEnbRunning ? "yes" : "no", gEnbFiles ? "yes" : "no", gEnbFiles && !gEnbRunning && !gIslShader ? " - left over, not used" : "",
			InverseSquare() ? "inverse square" : "plain");
	}

	Lighting LightingFound()
	{
		return gFound.load();
	}

	const char* LightingName(Lighting a_lighting)
	{
		switch (a_lighting) {
		case Lighting::kEnb:
			return "ENB";
		case Lighting::kVanilla:
			return "Vanilla";
		default:
			return "Community Shaders";
		}
	}

	bool InverseSquare()
	{
		return gFound.load() == Lighting::kShaders && gIslShader.load();
	}

	std::string LightingReport()
	{
		return std::format(
			R"({{"pick":"{}","detected":true,"inverseSquareShader":{},"communityShadersLoaded":{},"enbRunning":{},"enbFiles":{},"inverseSquare":{}}})",
			LightingName(gFound), gIslShader.load(), gCsLoaded.load(), gEnbRunning.load(), gEnbFiles.load(), InverseSquare());
	}
}
