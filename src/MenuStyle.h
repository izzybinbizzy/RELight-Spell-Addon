// MenuStyle.h - the shared look of izzydoingit's SKSE Menu Framework pages (one identical copy per plugin, like
// Translation.h). Each mod passes its own accent; the layout, the icon headers, the notes, the colour picker with its
// spectrum bar and the HUD panel are the same everywhere.
// Copyright (C) 2026 izzydoingit. GPL-3.0-or-later.
//
// Include after SKSEMenuFramework.h. Everything here is drawn on the menu's own thread.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>

namespace MenuStyle
{
	using namespace ImGuiMCP;

	// Font Awesome 6 (solid) icons the pages use - each one seen drawn in SKSE Menu Framework 3.8 (its font has no sword)
	namespace Icon
	{
		inline constexpr unsigned kPalette = 0xF53F, kShield = 0xF3ED, kBulb = 0xF0EB, kPuzzle = 0xF12E, kEye = 0xF06E,
								  kGear = 0xF013, kBolt = 0xF0E7, kFire = 0xF06D, kSnow = 0xF2DC, kWand = 0xF0D0, kSun = 0xF185,
								  kMoon = 0xF186, kStar = 0xF005, kBug = 0xF188, kGauge = 0xF625, kHand = 0xF256, kTarget = 0xF05B,
								  kGhost = 0xF6E2, kFlask = 0xF0C3, kList = 0xF03A, kInfo = 0xF05A, kSliders = 0xF1DE, kDisplay = 0xF108,
								  kBook = 0xF02D, kSkull = 0xF54C, kTemp = 0xF2C9, kWater = 0xF773, kGem = 0xF3A5;
	}

	struct Theme
	{
		ImVec4 accent;
		ImVec4 accentDim;
	};

	inline const ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };
	inline const ImVec4 kMuted{ 0.62f, 0.62f, 0.68f, 1.0f };
	inline const ImVec4 kGood{ 0.45f, 0.85f, 0.5f, 1.0f };
	inline const ImVec4 kBad{ 1.0f, 0.45f, 0.4f, 1.0f };

	inline Theme gTheme{ { 0.62f, 0.45f, 1.0f, 1.0f }, { 0.36f, 0.24f, 0.62f, 1.0f } };

	// a theme from one 0xRRGGBB accent: the dim shade is the same hue at 58% brightness
	inline Theme MakeTheme(std::uint32_t a_rgb)
	{
		const ImVec4 a{ ((a_rgb >> 16) & 0xFF) / 255.0f, ((a_rgb >> 8) & 0xFF) / 255.0f, (a_rgb & 0xFF) / 255.0f, 1.0f };
		return { a, { a.x * 0.58f, a.y * 0.58f, a.z * 0.58f, 1.0f } };
	}

	inline ImU32 U32(const ImVec4& a_c, float a_alpha = 1.0f)
	{
		auto ch = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return IM_COL32(ch(a_c.x), ch(a_c.y), ch(a_c.z), ch(a_c.w * a_alpha));
	}

	// the page's colours and rounding, popped when it goes out of scope; open one at the top of every page
	class Page
	{
	public:
		Page()
		{
			const auto& t = gTheme;
			const ImVec4 hover{ t.accentDim.x * 1.25f, t.accentDim.y * 1.25f, t.accentDim.z * 1.25f, 1.0f };
			PushStyleColor(ImGuiCol_CheckMark, t.accent);
			PushStyleColor(ImGuiCol_SliderGrab, t.accent);
			PushStyleColor(ImGuiCol_SliderGrabActive, t.accentDim);
			PushStyleColor(ImGuiCol_Header, t.accentDim);
			PushStyleColor(ImGuiCol_HeaderHovered, hover);
			PushStyleColor(ImGuiCol_HeaderActive, t.accent);
			PushStyleColor(ImGuiCol_ButtonHovered, hover);
			PushStyleColor(ImGuiCol_ButtonActive, t.accentDim);
			PushStyleColor(ImGuiCol_TabHovered, hover);
			PushStyleColor(ImGuiCol_PlotHistogram, t.accent);
			PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
			PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
		}
		~Page()
		{
			PopStyleVar(2);
			PopStyleColor(10);
		}
		Page(const Page&) = delete;
		Page& operator=(const Page&) = delete;
	};

	// an icon and a title in the accent, then a rule
	inline void Header(unsigned a_icon, const char* a_text)
	{
		Spacing();
		FontAwesome::PushSolid();
		TextColored(gTheme.accent, "%s", FontAwesome::UnicodeToUtf8(a_icon).c_str());
		FontAwesome::Pop();
		SameLine();
		TextColored(gTheme.accent, "%s", a_text);
		Separator();
	}

	// a quiet line of explanation under a header
	inline void Note(const char* a_text)
	{
		PushTextWrapPos(0.0f);
		TextColored(kMuted, "%s", a_text);
		PopTextWrapPos();
	}

	// a hover tooltip on the item just drawn
	inline void Tip(const char* a_text)
	{
		SetItemTooltip("%s", a_text);
	}

	// a small round dot and a label: on (accent) or off (muted)
	inline void Status(bool a_on, const char* a_text)
	{
		const auto pos = GetCursorScreenPos();
		const float h = GetTextLineHeight();
		ImDrawListManager::AddCircleFilled(GetWindowDrawList(), ImVec2(pos.x + h * 0.5f, pos.y + h * 0.5f), h * 0.3f,
			a_on ? U32(kGood) : U32(kMuted, 0.6f), 12);
		Dummy(ImVec2(h, h));
		SameLine();
		TextColored(a_on ? ImVec4(0.9f, 0.9f, 0.92f, 1.0f) : kMuted, "%s", a_text);
	}

	// the full spectrum as a bar: click or drag picks the hue at full colour; true when the mouse is let go
	inline bool SpectrumBar(const char* a_id, float a_rgb[3])
	{
		const auto  pos = GetCursorScreenPos();
		const float w = (std::max)(120.0f, GetContentRegionAvail().x), h = 16.0f;
		auto*       dl = GetWindowDrawList();
		constexpr ImU32 kStops[] = { IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255), IM_COL32(0, 255, 0, 255),
			IM_COL32(0, 255, 255, 255), IM_COL32(0, 0, 255, 255), IM_COL32(255, 0, 255, 255), IM_COL32(255, 0, 0, 255) };
		for (int i = 0; i < 6; ++i) {
			const float x0 = pos.x + w * i / 6.0f, x1 = pos.x + w * (i + 1) / 6.0f;
			ImDrawListManager::AddRectFilledMultiColor(dl, ImVec2(x0, pos.y), ImVec2(x1, pos.y + h), kStops[i], kStops[i + 1], kStops[i + 1], kStops[i]);
		}
		ImDrawListManager::AddRect(dl, pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(0, 0, 0, 160), 3.0f, 0, 1.0f);
		InvisibleButton(a_id, ImVec2(w, h));
		if (IsItemActive()) {
			const float t = std::clamp((GetMousePos().x - pos.x) / w, 0.0f, 0.9999f);
			ColorConvertHSVtoRGB(t, 1.0f, 1.0f, &a_rgb[0], &a_rgb[1], &a_rgb[2]);
		}
		return IsItemDeactivated();
	}

	struct Swatch
	{
		const char* name;
		float       rgb[3];
	};

	// a colour row: the picker (hex, hue bar), optional quick picks, the spectrum bar. a_rgb changes while dragging;
	// true once an edit is finished (picker let go, a quick pick, the bar let go) - save and apply then.
	inline bool ColorRow(const char* a_id, float a_rgb[3], std::span<const Swatch> a_quick = {})
	{
		PushID(a_id);
		bool done = false;
		SetNextItemWidth(220.0f);
		ColorEdit3("##pick", a_rgb, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoAlpha);
		done |= IsItemDeactivatedAfterEdit();
		for (const auto& q : a_quick) {
			SameLine();
			if (ColorButton(q.name, ImVec4(q.rgb[0], q.rgb[1], q.rgb[2], 1.0f), ImGuiColorEditFlags_NoAlpha, ImVec2(20.0f, 20.0f))) {
				std::copy_n(q.rgb, 3, a_rgb);
				done = true;
			}
			SetItemTooltip("%s", q.name);
		}
		done |= SpectrumBar("##spectrum", a_rgb);
		PopID();
		return done;
	}

	// a slider that reports true once let go (the drag shows live through a_value; save and apply on true)
	inline bool SliderDone() { return IsItemDeactivatedAfterEdit(); }

	// the HUD panel every mod's HUD element draws in: a dark rounded plate with an accent edge
	inline void HudPlate(ImDrawList* a_dl, ImVec2 a_min, ImVec2 a_max)
	{
		ImDrawListManager::AddRectFilled(a_dl, a_min, a_max, IM_COL32(10, 9, 16, 150), 6.0f, 0);
		ImDrawListManager::AddRect(a_dl, a_min, a_max, U32(gTheme.accent, 0.45f), 6.0f, 0, 1.0f);
	}
}
