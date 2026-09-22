// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Reads the data files the installer put down, once, when the game has loaded its data.
//
// A line is TAB-separated, and there are seven kinds (the build writes them; `relightgen.py read_data_text`
// is the same reader in Python and the build refuses a file it cannot read):
//   version 1 | file <download> <option> | order <n> | switch <0|1> | mesh <key> | base <0xID~Plugin>
//   stream <key> <node> <r> <g> <b> <fade> <radius> <size> <cutoff> <x> <y> <z>
//
// How an object is matched, and why: RE::Light keys a config by the bare end of the object's mesh path,
// lowercased, with no folder and no .nif - "Magic\RuneFireProjectile01.nif" is "runefireprojectile01" - and
// takes a config whose key the object's key CONTAINS. So this does the same: the exact key first, then the
// longest of our keys the object's key contains. A form claim (`base`) is checked before either, exactly as
// RE::Light checks a base form before a mesh.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		namespace fs = std::filesystem;
		constexpr std::string_view kFolder = "Data/SKSE/Plugins/RelightSpellAddon";
		constexpr std::string_view kHandLights = "RELight - Spell Addon - Hand Lights.esp";

		std::vector<Option>                          gOptions;
		std::unordered_map<std::string, std::size_t> gMeshOwner;    // key -> option (highest order wins)
		std::unordered_map<RE::FormID, std::size_t>  gBaseOwner;    // form -> option
		std::unordered_map<std::string, Stream>      gStreams;      // key -> recipe (highest order wins)
		std::vector<std::string>                     gKeysLongestFirst;
		std::vector<std::string>                     gStreamKeysLongestFirst;
		std::size_t                                  gFiles = 0;

		// what a base form resolves to is cached: the answer never changes after the data has loaded
		std::mutex                                           gCacheLock;
		std::unordered_map<RE::FormID, std::size_t>          gOwnerCache;
		std::unordered_map<RE::FormID, const Stream*>        gStreamCache;

		std::vector<std::string_view> Split(std::string_view a_line)
		{
			std::vector<std::string_view> out;
			std::size_t                   start = 0;
			while (true) {
				const auto tab = a_line.find('\t', start);
				out.push_back(a_line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
				if (tab == std::string_view::npos) {
					break;
				}
				start = tab + 1;
			}
			return out;
		}

		bool Float(std::string_view a_text, float& a_out)
		{
			const auto r = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out);
			return r.ec == std::errc{};
		}

		bool Int(std::string_view a_text, int& a_out)
		{
			const auto r = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out);
			return r.ec == std::errc{};
		}

		RE::FormID ResolveBase(std::string_view a_text)
		{
			// "0x1540E~Dawnguard.esm"
			const auto tilde = a_text.find('~');
			if (tilde == std::string_view::npos || tilde < 3) {
				return 0;
			}
			std::uint32_t local = 0;
			auto          hex = a_text.substr(0, tilde);
			if (hex.starts_with("0x") || hex.starts_with("0X")) {
				hex.remove_prefix(2);
			}
			if (std::from_chars(hex.data(), hex.data() + hex.size(), local, 16).ec != std::errc{}) {
				return 0;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupFormID(local, a_text.substr(tilde + 1)) : 0;
		}

		// one file -> one option; false when it does not read, and nothing of it is kept
		bool ReadFile(const fs::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return false;
			}
			Option                               opt;
			std::vector<std::string>             meshes;
			std::vector<RE::FormID>              bases;
			std::vector<Stream>                  streams;
			bool                                 sawVersion = false, sawFile = false;
			std::string                          line;
			std::size_t                          n = 0;
			while (std::getline(in, line)) {
				++n;
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.empty() || line[0] == '#') {
					continue;
				}
				const auto p = Split(line);
				if (p[0] == "version" && p.size() >= 2) {
					int v = 0;
					if (!Int(p[1], v) || v != 1) {
						SKSE::log::warn("{}: version {} is not one this plugin reads", a_path.filename().string(), std::string(p[1]));
						return false;
					}
					sawVersion = true;
				} else if (p[0] == "file" && p.size() >= 3) {
					opt.download = std::string(p[1]);
					opt.name = std::string(p[2]);
					opt.id = opt.download + " - " + opt.name;
					sawFile = true;
				} else if (p[0] == "order" && p.size() >= 2) {
					if (!Int(p[1], opt.order)) {
						return false;
					}
				} else if (p[0] == "switch" && p.size() >= 2) {
					opt.switchable = p[1] == "1";
				} else if (p[0] == "mesh" && p.size() >= 2) {
					meshes.emplace_back(p[1]);
				} else if (p[0] == "base" && p.size() >= 2) {
					if (const auto id = ResolveBase(p[1])) {
						bases.push_back(id);
					}
				} else if (p[0] == "stream" && p.size() == 13) {
					Stream s;
					s.key = std::string(p[1]);
					s.node = std::string(p[2]);
					float v[10]{};
					for (std::size_t i = 0; i < 10; ++i) {
						if (!Float(p[3 + i], v[i])) {
							SKSE::log::warn("{} line {}: a stream number does not read", a_path.filename().string(), n);
							return false;
						}
					}
					s.color = { v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f };
					s.fade = v[3];
					s.radius = v[4];
					s.size = v[5];
					s.cutoff = v[6];
					s.position = { v[7], v[8], v[9] };
					streams.push_back(std::move(s));
				} else {
					SKSE::log::warn("{} line {}: not a line this plugin reads", a_path.filename().string(), n);
					return false;
				}
			}
			if (!sawVersion || !sawFile) {
				return false;
			}
			const auto idx = gOptions.size();
			opt.meshes = meshes.size();
			opt.bases = bases.size();
			opt.streams = streams.size();
			gOptions.push_back(opt);
			const auto wins = [&](std::size_t a_current) { return a_current == kNone || gOptions[a_current].order <= opt.order; };
			for (auto& m : meshes) {
				auto it = gMeshOwner.find(m);
				if (it == gMeshOwner.end() || wins(it->second)) {
					gMeshOwner[m] = idx;
				}
			}
			for (auto id : bases) {
				auto it = gBaseOwner.find(id);
				if (it == gBaseOwner.end() || wins(it->second)) {
					gBaseOwner[id] = idx;
				}
			}
			for (auto& s : streams) {
				s.order = opt.order;
				s.option = idx;
				auto it = gStreams.find(s.key);
				if (it == gStreams.end() || it->second.order <= s.order) {
					gStreams[s.key] = s;
				}
				// a stream's object is ours too, for the slider and the switch
				auto mo = gMeshOwner.find(s.key);
				if (mo == gMeshOwner.end() || wins(mo->second)) {
					gMeshOwner[s.key] = idx;
				}
			}
			return true;
		}

		std::string KeyOf(RE::TESForm* a_base)
		{
			auto* model = a_base ? a_base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			return (path && *path) ? MeshKey(path) : std::string{};
		}

		// RE::Light's rule: the exact key, else a key of ours the object's key contains (longest first here)
		template <class Map>
		auto Find(const Map& a_map, const std::vector<std::string>& a_longestFirst, const std::string& a_key)
		{
			if (auto it = a_map.find(a_key); it != a_map.end()) {
				return it;
			}
			for (const auto& k : a_longestFirst) {
				if (k.size() < a_key.size() && a_key.find(k) != std::string::npos) {
					return a_map.find(k);
				}
			}
			return a_map.end();
		}
	}

	std::string MeshKey(std::string_view a_path)
	{
		const auto slash = a_path.find_last_of("\\/");
		if (slash != std::string_view::npos) {
			a_path.remove_prefix(slash + 1);
		}
		std::string out{ a_path };
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
		gFiles = 0;
		std::size_t bad = 0;
		std::error_code ec;
		std::vector<fs::path> files;
		for (fs::directory_iterator it{ fs::path(kFolder), ec }, end; !ec && it != end; it.increment(ec)) {
			if (it->is_regular_file() && it->path().extension() == ".txt") {
				files.push_back(it->path());
			}
		}
		std::ranges::sort(files);
		for (auto& f : files) {
			if (ReadFile(f)) {
				++gFiles;
			} else {
				++bad;
				SKSE::log::warn("{} does not read - its lights stay as RE::Light makes them", f.filename().string());
			}
		}
		gKeysLongestFirst.clear();
		for (auto& [k, _v] : gMeshOwner) {
			gKeysLongestFirst.push_back(k);
		}
		std::ranges::sort(gKeysLongestFirst, [](const auto& a, const auto& b) { return a.size() > b.size() || (a.size() == b.size() && a < b); });
		gStreamKeysLongestFirst.clear();
		for (auto& [k, _v] : gStreams) {
			gStreamKeysLongestFirst.push_back(k);
		}
		std::ranges::sort(gStreamKeysLongestFirst, [](const auto& a, const auto& b) { return a.size() > b.size() || (a.size() == b.size() && a < b); });
		std::size_t sw = 0;
		for (auto& o : gOptions) {
			sw += o.switchable ? 1 : 0;
		}
		SKSE::log::info("data: {} file(s) read, {} did not; {} switch(es), {} object key(s), {} form(s), {} stream(s)",
			gFiles, bad, sw, gMeshOwner.size(), gBaseOwner.size(), gStreams.size());
	}

	std::vector<Option>& Options() { return gOptions; }
	std::size_t          DataFiles() { return gFiles; }

	std::size_t OptionOf(RE::TESForm* a_base)
	{
		if (!a_base) {
			return kNone;
		}
		const auto id = a_base->GetFormID();
		{
			std::lock_guard l{ gCacheLock };
			if (auto it = gOwnerCache.find(id); it != gOwnerCache.end()) {
				return it->second;
			}
		}
		std::size_t owner = kNone;
		if (auto it = gBaseOwner.find(id); it != gBaseOwner.end()) {
			owner = it->second;
		} else if (const auto key = KeyOf(a_base); !key.empty()) {
			if (auto it2 = Find(gMeshOwner, gKeysLongestFirst, key); it2 != gMeshOwner.cend()) {
				owner = it2->second;
			}
		}
		std::lock_guard l{ gCacheLock };
		gOwnerCache[id] = owner;
		return owner;
	}

	const Stream* StreamOf(RE::TESForm* a_base)
	{
		if (!a_base || gStreams.empty()) {
			return nullptr;
		}
		const auto id = a_base->GetFormID();
		{
			std::lock_guard l{ gCacheLock };
			if (auto it = gStreamCache.find(id); it != gStreamCache.end()) {
				return it->second;
			}
		}
		const Stream* s = nullptr;
		if (const auto key = KeyOf(a_base); !key.empty()) {
			if (auto it = Find(gStreams, gStreamKeysLongestFirst, key); it != gStreams.cend()) {
				s = &it->second;
			}
		}
		std::lock_guard l{ gCacheLock };
		gStreamCache[id] = s;
		return s;
	}

	bool IsHandLightRecord(RE::TESObjectLIGH* a_light)
	{
		auto* file = a_light ? a_light->GetFile(0) : nullptr;
		return file && file->GetFilename() == kHandLights;
	}
}
