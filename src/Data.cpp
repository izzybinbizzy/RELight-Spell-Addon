// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Reads the data files the installer put down, once, when the game has loaded its data.
//
// A line is TAB-separated (the build writes them; `relightgen.py read_data_text` is the same reader in Python and the
// build refuses a file it cannot read):
//   version 1 | file <download> <option> | order <n> | menu <n> | switch <0|1> | mesh <key> | base <0xID~Plugin>
//   stream <key> <node> <r> <g> <b> <fade> <radius> <size> <cutoff> <x> <y> <z>
//   hand <key> <r> <g> <b> <fade> <radius> <size> <cutoff> <inverse square 0|1> <portal strict 0|1>
//   spraylight <0xID~Plugin>   a light record a spray makes; RE::Light lights it through our config
//   handfx <key> <pulse|flicker> <per second> <intensity>   a hand light's Dynamic Lighting (installed with it)
//   menugroup <category> <author or ->   where a patch sits on the Patches page
//   tint <key or 0xID~Plugin> <r> <g> <b>   an art pick's colour for the lights on an object (highest order wins)
//
// Objects are matched as RE::Light matches them: a form first, then the bare mesh key ("Magic\RuneFire01.nif" is
// "runefire01"), exactly, then the longest of our keys the object's key contains.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		namespace fs = std::filesystem;
		constexpr std::string_view kFolder = "Data/SKSE/Plugins/RelightSpellAddon";

		struct Tint
		{
			RE::NiColor color;
			int         order{ 0 };
		};

		std::vector<Option>                         gOptions;
		StringMap<std::size_t>                      gMeshOwner;  // key -> option (highest order wins)
		std::unordered_map<RE::FormID, std::size_t> gBaseOwner;  // form -> option
		StringMap<Stream>                           gStreams;    // key -> recipe (highest order wins)
		// key -> EVERY layer's hand light, highest order first: a switched-off option hands the hand to the next layer
		StringMap<std::vector<Hand>>                gHands;
		std::unordered_set<RE::FormID>              gSprayLights;
		StringMap<HandFx>                           gHandFx;
		StringMap<Tint>                             gMeshTints;
		std::unordered_map<RE::FormID, Tint>        gBaseTints;
		std::vector<std::string>                    gKeysLongestFirst, gStreamKeysLongestFirst, gTintKeysLongestFirst;
		std::size_t                                 gFiles = 0;

		// what a base form resolves to never changes after the data has loaded; Load3D asks from loader threads too
		std::mutex                                            gCacheLock;
		std::unordered_map<RE::FormID, std::size_t>           gOwnerCache;
		std::unordered_map<RE::FormID, const Stream*>         gStreamCache;
		std::unordered_map<RE::FormID, const RE::NiColor*>    gTintCache;

		[[nodiscard]] std::vector<std::string_view> Split(std::string_view a_line)
		{
			std::vector<std::string_view> out;
			for (std::size_t start = 0;;) {
				const auto tab = a_line.find('\t', start);
				out.push_back(a_line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
				if (tab == std::string_view::npos) {
					return out;
				}
				start = tab + 1;
			}
		}

		template <class T>
		[[nodiscard]] bool Number(std::string_view a_text, T& a_out)
		{
			return std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out).ec == std::errc{};
		}

		// n numbers from a_fields[a_first..]; false when any does not read
		template <std::size_t N>
		[[nodiscard]] bool Numbers(const std::vector<std::string_view>& a_fields, std::size_t a_first, std::array<float, N>& a_out)
		{
			for (std::size_t i = 0; i < N; ++i) {
				if (!Number(a_fields[a_first + i], a_out[i])) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] RE::NiColor Colour(float a_r, float a_g, float a_b) { return { a_r / 255.0f, a_g / 255.0f, a_b / 255.0f }; }

		[[nodiscard]] RE::FormID ResolveBase(std::string_view a_text)
		{
			// "0x1540E~Dawnguard.esm"
			const auto tilde = a_text.find('~');
			if (tilde == std::string_view::npos || tilde < 3) {
				return 0;
			}
			auto hex = a_text.substr(0, tilde);
			if (hex.starts_with("0x") || hex.starts_with("0X")) {
				hex.remove_prefix(2);
			}
			std::uint32_t local = 0;
			if (std::from_chars(hex.data(), hex.data() + hex.size(), local, 16).ec != std::errc{}) {
				return 0;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupFormID(local, a_text.substr(tilde + 1)) : 0;
		}

		// everything one file holds, applied to the tables only when the whole file has read
		struct File
		{
			Option                                      option;
			std::vector<std::string>                    meshes;
			std::vector<RE::FormID>                     bases;
			std::vector<Stream>                         streams;
			std::vector<Hand>                           hands;
			std::vector<std::pair<std::string, HandFx>> fx;
			std::vector<std::pair<std::string, Tint>>   tints;
			bool                                        sawVersion{ false }, sawFile{ false }, sawMenu{ false };
		};

		// one line into `a_file`; false when it does not read
		[[nodiscard]] bool ParseLine(const std::vector<std::string_view>& p, File& a_file)
		{
			const auto kind = p[0];
			auto&      opt = a_file.option;
			if (kind == "version" && p.size() >= 2) {
				int v = 0;
				a_file.sawVersion = Number(p[1], v) && v == 1;
				return a_file.sawVersion;
			}
			if (kind == "file" && p.size() >= 3) {
				opt.download = std::string(p[1]);
				opt.name = std::string(p[2]);
				opt.id = opt.download + " - " + opt.name;
				a_file.sawFile = true;
				return true;
			}
			if (kind == "order" && p.size() >= 2) {
				if (!Number(p[1], opt.order)) {
					return false;
				}
				if (!a_file.sawMenu) {
					opt.menu = opt.order;  // a file written before `menu` existed sorts by its order
				}
				return true;
			}
			if (kind == "menu" && p.size() >= 2) {
				a_file.sawMenu = true;
				return Number(p[1], opt.menu);  // menu position, separate from `order` (which decides mesh ownership)
			}
			if (kind == "switch" && p.size() >= 2) {
				opt.switchable = p[1] == "1";
				return true;
			}
			if (kind == "menugroup" && p.size() == 3) {
				opt.category = std::string(p[1]);
				opt.author = p[2] == "-" ? std::string() : std::string(p[2]);
				return true;
			}
			if (kind == "mesh" && p.size() >= 2) {
				a_file.meshes.emplace_back(p[1]);
				return true;
			}
			if (kind == "base" && p.size() >= 2) {
				if (const auto id = ResolveBase(p[1])) {
					a_file.bases.push_back(id);
				}
				return true;
			}
			if (kind == "spraylight" && p.size() >= 2) {
				if (const auto id = ResolveBase(p[1])) {
					gSprayLights.insert(id);
				}
				return true;
			}
			if (kind == "stream" && p.size() == 13) {
				std::array<float, 10> v{};
				if (!Numbers(p, 3, v)) {
					return false;
				}
				Stream s;
				s.key = std::string(p[1]);
				s.node = std::string(p[2]);
				s.color = Colour(v[0], v[1], v[2]);
				s.fade = v[3];
				s.radius = v[4];
				s.size = v[5];
				s.cutoff = v[6];
				s.positions = { RE::NiPoint3{ v[7], v[8], v[9] } };
				a_file.streams.push_back(std::move(s));
				return true;
			}
			if (kind == "hand" && p.size() == 11) {
				std::array<float, 9> v{};
				if (!Numbers(p, 2, v)) {
					return false;
				}
				Hand h;
				h.key = std::string(p[1]);
				for (std::size_t i = 0; i < 3; ++i) {
					h.rgb[i] = static_cast<std::uint8_t>(std::clamp(v[i], 0.0f, 255.0f));
				}
				h.color = Colour(v[0], v[1], v[2]);
				h.fade = v[3];
				h.radius = v[4];
				h.size = v[5];
				h.cutoff = v[6];
				h.inverseSquare = v[7] != 0.0f;
				h.portalStrict = v[8] != 0.0f;
				a_file.hands.push_back(std::move(h));
				return true;
			}
			if (kind == "handfx" && p.size() == 5 && (p[2] == "pulse" || p[2] == "flicker")) {
				HandFx f;
				f.flicker = p[2] == "flicker";
				if (!Number(p[3], f.perSecond) || !Number(p[4], f.intensity)) {
					return false;
				}
				a_file.fx.emplace_back(std::string(p[1]), f);
				return true;
			}
			if (kind == "tint" && p.size() == 5) {
				std::array<float, 3> v{};
				if (!Numbers(p, 2, v)) {
					return false;
				}
				a_file.tints.emplace_back(std::string(p[1]), Tint{ Colour(v[0], v[1], v[2]) });
				return true;
			}
			return false;
		}

		template <class Map, class Value>
		void KeepHighest(Map& a_map, const typename Map::key_type& a_key, Value&& a_value, int a_order)
		{
			if (auto it = a_map.find(a_key); it == a_map.end() || it->second.order <= a_order) {
				a_map.insert_or_assign(a_key, std::forward<Value>(a_value));
			}
		}

		// one file -> one option; false when it does not read, and nothing of it is kept
		[[nodiscard]] bool ReadFile(const fs::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return false;
			}
			File        file;
			std::string line;
			for (std::size_t n = 1; std::getline(in, line); ++n) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.empty() || line[0] == '#') {
					continue;
				}
				if (!ParseLine(Split(line), file)) {
					SKSE::log::warn("{} line {}: not a line this plugin reads", a_path.filename().string(), n);
					return false;
				}
			}
			if (!file.sawVersion || !file.sawFile) {
				return false;
			}
			auto&      opt = file.option;
			const auto idx = gOptions.size();
			opt.meshes = file.meshes.size();
			opt.bases = file.bases.size();
			opt.streams = file.streams.size();
			gOptions.push_back(opt);
			const auto wins = [&](std::size_t a_current) { return gOptions[a_current].order <= opt.order; };
			const auto own = [&](auto& a_map, const auto& a_key) {
				if (auto it = a_map.find(a_key); it == a_map.end() || wins(it->second)) {
					a_map.insert_or_assign(a_key, idx);
				}
			};
			for (const auto& m : file.meshes) {
				own(gMeshOwner, m);
			}
			for (const auto id : file.bases) {
				own(gBaseOwner, id);
			}
			// lines for one mesh in one file are one ladder; a later layer replaces an earlier layer's whole ladder
			std::unordered_set<std::string> fresh;
			for (auto& s : file.streams) {
				s.order = opt.order;
				s.option = idx;
				own(gMeshOwner, s.key);  // a stream's object is ours too, for the slider and the switch
				auto it = gStreams.find(s.key);
				if (it != gStreams.end() && it->second.order == s.order && fresh.contains(s.key)) {
					it->second.positions.push_back(s.positions.front());
				} else if (it == gStreams.end() || it->second.order <= s.order) {
					fresh.insert(s.key);
					gStreams.insert_or_assign(s.key, std::move(s));
				}
			}
			for (auto& [key, f] : file.fx) {
				f.order = opt.order;
				KeepHighest(gHandFx, key, f, opt.order);
			}
			for (auto& [key, t] : file.tints) {
				t.order = opt.order;
				if (const auto id = key.find('~') != std::string::npos ? ResolveBase(key) : 0) {
					KeepHighest(gBaseTints, id, t, opt.order);
				} else {
					KeepHighest(gMeshTints, key, t, opt.order);
				}
			}
			for (auto& h : file.hands) {
				h.order = opt.order;
				h.option = idx;
				gHands[h.key].push_back(std::move(h));
			}
			return true;
		}

		[[nodiscard]] std::string KeyOf(const RE::TESForm* a_base)
		{
			const auto* model = a_base ? a_base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			return (path && *path) ? MeshKey(path) : std::string{};
		}

		// RE::Light's rule: the exact key, else the longest of ours the object's key contains
		template <class Map>
		[[nodiscard]] auto Find(const Map& a_map, const std::vector<std::string>& a_longestFirst, std::string_view a_key)
		{
			if (auto it = a_map.find(a_key); it != a_map.end()) {
				return it;
			}
			for (const auto& k : a_longestFirst) {
				if (k.size() < a_key.size() && a_key.find(k) != std::string_view::npos) {
					return a_map.find(k);
				}
			}
			return a_map.end();
		}

		template <class Map>
		[[nodiscard]] std::vector<std::string> LongestFirst(const Map& a_map)
		{
			std::vector<std::string> keys;
			keys.reserve(a_map.size());
			for (const auto& [k, _v] : a_map) {
				keys.push_back(k);
			}
			std::ranges::sort(keys, [](const auto& a, const auto& b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
			return keys;
		}

		// a per-form answer, worked out once and cached
		template <class V, class F>
		[[nodiscard]] V Cached(std::unordered_map<RE::FormID, V>& a_cache, const RE::TESForm* a_base, F&& a_work)
		{
			const auto id = a_base->GetFormID();
			{
				std::lock_guard l{ gCacheLock };
				if (auto it = a_cache.find(id); it != a_cache.end()) {
					return it->second;
				}
			}
			const V value = a_work();
			std::lock_guard l{ gCacheLock };
			return a_cache.emplace(id, value).first->second;
		}
	}

	std::string MeshKey(std::string_view a_path)
	{
		if (const auto slash = a_path.find_last_of("\\/"); slash != std::string_view::npos) {
			a_path.remove_prefix(slash + 1);
		}
		std::string out{ a_path };
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (out.size() > 4 && out.ends_with(".nif")) {
			out.resize(out.size() - 4);
		}
		return out;
	}

	// The full key, for two mods' art under one file name: a `hand` line may name the whole path, looked up before the
	// bare name. Same rule as relightgen.path_key: lowercase, backslashes, no leading `meshes\`, no `.nif`.
	std::string PathKey(std::string_view a_path)
	{
		std::string out{ a_path };
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
		const auto first = out.find_first_not_of('\\');
		out.erase(0, first == std::string::npos ? out.size() : first);
		if (out.starts_with("meshes\\")) {
			out.erase(0, 7);
		}
		if (out.size() > 4 && out.ends_with(".nif")) {
			out.resize(out.size() - 4);
		}
		return out;
	}

	void LoadData()
	{
		gOptions.clear();
		gMeshOwner.clear();
		gBaseOwner.clear();
		gStreams.clear();
		gHands.clear();
		gSprayLights.clear();
		gHandFx.clear();
		gMeshTints.clear();
		gBaseTints.clear();
		{
			std::lock_guard l{ gCacheLock };
			gOwnerCache.clear();
			gStreamCache.clear();
			gTintCache.clear();
		}
		gFiles = 0;
		std::vector<fs::path> files;
		std::error_code       ec;
		for (fs::directory_iterator it{ fs::path(kFolder), ec }, end; !ec && it != end; it.increment(ec)) {
			if (it->is_regular_file() && it->path().extension() == ".txt") {
				files.push_back(it->path());
			}
		}
		std::ranges::sort(files);
		std::size_t bad = 0;
		for (const auto& f : files) {
			if (ReadFile(f)) {
				++gFiles;
			} else {
				++bad;
				SKSE::log::warn("{} does not read - its lights stay as RE::Light makes them", f.filename().string());
			}
		}
		gKeysLongestFirst = LongestFirst(gMeshOwner);
		gStreamKeysLongestFirst = LongestFirst(gStreams);
		gTintKeysLongestFirst = LongestFirst(gMeshTints);
		for (auto& [_k, list] : gHands) {
			std::ranges::stable_sort(list, std::greater{}, &Hand::order);
		}
		const auto switches = std::ranges::count_if(gOptions, &Option::switchable);
		SKSE::log::info("data: {} file(s) read, {} did not; {} switch(es), {} object key(s), {} form(s), {} stream(s), {} hand key(s), "
						"{} tint(s)",
			gFiles, bad, switches, gMeshOwner.size(), gBaseOwner.size(), gStreams.size(), gHands.size(),
			gMeshTints.size() + gBaseTints.size());
	}

	std::vector<Option>& Options() { return gOptions; }
	std::size_t          DataFiles() { return gFiles; }

	const std::vector<std::size_t>& OptionsInMenuOrder()
	{
		static std::vector<std::size_t> order;
		if (order.size() != gOptions.size()) {
			order.resize(gOptions.size());
			std::iota(order.begin(), order.end(), std::size_t{ 0 });
			std::ranges::stable_sort(order, [](std::size_t a, std::size_t b) {
				const auto& x = gOptions[a];
				const auto& y = gOptions[b];
				return x.menu != y.menu ? x.menu < y.menu : x.id < y.id;
			});
		}
		return order;
	}

	std::size_t OptionOf(const RE::TESForm* a_base)
	{
		if (!a_base) {
			return kNone;
		}
		return Cached(gOwnerCache, a_base, [&] {
			if (auto it = gBaseOwner.find(a_base->GetFormID()); it != gBaseOwner.end()) {
				return it->second;
			}
			const auto key = KeyOf(a_base);
			const auto it = key.empty() ? gMeshOwner.cend() : Find(gMeshOwner, gKeysLongestFirst, key);
			return it != gMeshOwner.cend() ? it->second : kNone;
		});
	}

	const Stream* StreamOf(const RE::TESForm* a_base)
	{
		if (!a_base || gStreams.empty()) {
			return nullptr;
		}
		return Cached(gStreamCache, a_base, [&]() -> const Stream* {
			const auto key = KeyOf(a_base);
			const auto it = key.empty() ? gStreams.cend() : Find(gStreams, gStreamKeysLongestFirst, key);
			return it != gStreams.cend() ? &it->second : nullptr;
		});
	}

	const RE::NiColor* TintOf(const RE::TESForm* a_base)
	{
		if (!a_base || (gMeshTints.empty() && gBaseTints.empty())) {
			return nullptr;
		}
		return Cached(gTintCache, a_base, [&]() -> const RE::NiColor* {
			if (auto it = gBaseTints.find(a_base->GetFormID()); it != gBaseTints.end()) {
				return &it->second.color;
			}
			const auto key = KeyOf(a_base);
			const auto it = key.empty() ? gMeshTints.cend() : Find(gMeshTints, gTintKeysLongestFirst, key);
			return it != gMeshTints.cend() ? &it->second.color : nullptr;
		});
	}

	const StringMap<std::vector<Hand>>&   Hands() { return gHands; }
	const std::unordered_set<RE::FormID>& SprayLightRecords() { return gSprayLights; }

	const HandFx* HandFxOf(std::string_view a_key)
	{
		const auto it = gHandFx.find(a_key);
		return it == gHandFx.end() ? nullptr : &it->second;
	}
}
