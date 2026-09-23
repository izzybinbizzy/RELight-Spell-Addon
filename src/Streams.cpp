// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// LIGHTS THAT TRAVEL - sprays, breath shouts and beams.
//
// ⚫ WHY THE PLUGIN AND NOT A CONFIG: RE::Light puts a config's light at a POSITION relative to the object's
// root, and its `attachPath` is a list of child INDICES, not node names (LightManager.cpp) - so a config
// cannot bind a light to a beam's BeamEnd node, and it lights a flame spray at its root, which is the
// caster's hand. That is the "two identical lights at [0,0,0]" the configs shipped until now. The build
// takes those claims OUT of the configs and writes them into the data files as `stream` lines; this hangs
// the lights itself.
//
// CREDIT: how a light is made and registered here follows ReLight by Truman (github.com/TrumanGIT/ReLight),
// GPL-3.0-or-later, with his permission and kept under the same license - the same way Luminous Arcana's
// StreamLights.cpp does it, which this is ported from. From ReLight: one master NiPointLight made once and
// cloned for every use (a freshly made light attached straight away crashes), the light's size carried in
// the radius' z, the create parameters a non-shadow light needs (field of view 90, portal-strict, never
// fades), and handing the light to the shadow scene node, which is what renders it. Which objects get lights,
// where they sit and how the settings drive them is ours.
//
// How it works. When the game builds the 3D of an object whose mesh a data file names as a stream, one light
// is hung under that 3D - on the node the build measured for that mesh (beam-nodes.txt: BeamEnd on a beam,
// so the light rides the tip of the bolt, AttachLight on a spray) or at the root when there is none - at the
// position the config gave it. It is a child of the object's own 3D, so it travels and turns with it and goes
// when the object goes. A flame spray is many short-lived objects, so no stream keeps more than a few lights
// at once. Nothing on any record is edited.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::size_t kMaxLivePerStream = 4;   // a spray spawns a new object many times a second
		constexpr std::size_t kMaxLiveTotal = 24;
		constexpr float       kFieldOfView = 90.0f;   // what a light that casts no shadow is given (from ReLight)
		constexpr const char* kLightName = "RSAStream";

		RE::NiPointer<RE::NiPointLight> gMaster;

		struct Live
		{
			RE::NiPointer<RE::BSLight>      bs;
			RE::NiPointer<RE::NiPointLight> light;
			const Stream*                   stream{ nullptr };
			float                           written{ -1.0f };   // the fade we last wrote
			float                           wroteReach{ -1.0f };  // the reach we last wrote
		};

		std::mutex                                      gLock;
		std::vector<Live>                               gLive;
		std::unordered_map<const Stream*, std::size_t>  gCount;

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				auto* fresh = RE::NiPointLight::Create();
				if (!fresh) {
					return nullptr;
				}
				auto* clone = netimmerse_cast<RE::NiPointLight*>(fresh->Clone());
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		bool OptionOff(const Stream* a_s)
		{
			auto& opts = Options();
			return a_s && a_s->option < opts.size() && opts[a_s->option].switchable && !opts[a_s->option].on;
		}

		std::size_t gTold = 0;

		// 🔥 each projectile we hang a travelling light on, and the light its own record gives it
		struct OwnLight
		{
			RE::BGSProjectile*  projectile;
			RE::TESObjectLIGH*  own;
			const Stream*       stream;
		};
		std::vector<OwnLight> gOwn;
		std::unordered_map<RE::FormID, std::size_t> gExplTold;
		std::size_t                                 gExplLines = 0;
	}

	// ⛔ ONE OBJECT, ONE LIGHT - HIS REPORT, 2026-09-22: *"sprays are vanilla"*. His own save of RE::Light's editor showed
	// why: the spray's light in game was the PROJECTILE's own record light (`MagicLightFireball01`, reach ~695), which
	// RE::Light keeps, beside our travelling light (reach 250). So while an option's travelling light is on, the
	// projectile's own light is taken off, in memory; switch the option off and it is given back. Nothing is saved.
	void TakeStreamProjectileLights()
	{
		gOwn.clear();
		for (auto* p : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSProjectile>()) {
			if (!p || !p->data.light) {
				continue;
			}
			if (const Stream* s = StreamOf(p)) {
				gOwn.push_back({ p, p->data.light, s });
			}
		}
		ApplyStreamProjectileLights(true);
	}

	void ApplyStreamProjectileLights(bool a_log)
	{
		std::size_t off = 0, back = 0;
		for (auto& o : gOwn) {
			auto* want = OptionOff(o.stream) ? o.own : nullptr;
			if (o.projectile->data.light != want) {
				o.projectile->data.light = want;
			}
			(want ? back : off) += 1;
		}
		if (a_log) {
			SKSE::log::info("stream projectiles: {} carry only our travelling light, {} keep their own (option off)", off, back);
		}
	}

	// 💥 HIS REPORT, 2026-09-22: *"explosions don't work theres no light"*. What every explosion that loads actually
	// carries once RE::Light has had its turn - its record, whether our data names it, and the lights under its 3D.
	// The first three of each record, 60 lines in all.
	void NoteExplosion(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
	{
		auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || !a_root || gExplLines >= 60) {
			return;
		}
		auto& n = gExplTold[base->GetFormID()];
		if (n >= 3) {
			return;
		}
		++n;
		++gExplLines;
		std::size_t lights = 0, culled = 0;
		RE::BSVisit::TraverseScenegraphLights(a_root, [&](RE::NiPointLight* a_light) {
			++lights;
			culled += (a_light && a_light->GetAppCulled()) ? 1 : 0;
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		auto*       model = base->As<RE::TESModel>();
		const char* path = model ? model->GetModel() : nullptr;
		auto*       file = base->GetFile(0);
		SKSE::log::info("[EXPL] {:08X} {} | model {} | ours {} | lights under it {} ({} culled) | in a cell {}", base->GetFormID(),
			file ? file->GetFilename() : "?", (path && *path) ? MeshKey(path) : "-", OptionOf(base) != kNone ? "yes" : "no", lights,
			culled, a_ref->GetParentCell() ? "yes" : "NO");
	}

	void HangStreamLights(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
	{
		if (!a_ref || !a_root) {
			return;
		}
		const Stream* s = StreamOf(a_ref->GetBaseObject());
		if (!s || OptionOff(s)) {
			return;
		}
		auto* root = a_root->AsNode();
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!root || !scene) {
			return;
		}
		std::lock_guard l{ gLock };
		if (gLive.size() >= kMaxLiveTotal || gCount[s] >= kMaxLivePerStream) {
			return;
		}
		RE::NiNode* parent = root;
		if (s->node != "-") {
			if (auto* n = root->GetObjectByName(RE::BSFixedString(s->node.c_str())); n && n->AsNode()) {
				parent = n->AsNode();
			}
		}
		auto* light = CloneMaster();
		if (!light) {
			return;
		}
		light->name = kLightName;
		auto&       data = light->GetLightRuntimeData();
		const float fade = s->fade * Brightness();
		const float reach = s->radius * Reach();
		data.diffuse = s->color;
		data.fade = fade;
		// x and y are the reach; z carries the light's SIZE, not a third radius (ReLight and Light Placer both do this)
		data.radius = { reach, reach, s->size };
		// without this a light has no attenuation of its own and never brightens anything (Light Placer does it too)
		light->SetLightAttenuation(reach);
		// ⚫ Community Shaders' inverse square lighting reads its flag and cutoff out of the light's own data, in
		// the words before the color - RE::Light's `Overlay` (LightData.h) writes exactly these two, so ours
		// render the way its config lights do. Set AFTER SetLightAttenuation, which writes those words too.
		// ⛔ THE CUTOFF IS DERIVED, NOT COPIED, SINCE 2026-09-22. It used to be `s->cutoff` straight out of
		// the data file, which is right only while both sliders sit at 100%: under inverse square the reach is
		// sqrt(K * fade / cutoff - size²), so a brighter light carried further and the Brightness slider was
		// quietly a reach slider - his report. Re-derived from the house formula, Brightness moves the peak
		// with the reach held and Reach moves the reach with the peak held. At 100%/100% this is the number
		// the file carries, to four figures.
		{
			auto* words = reinterpret_cast<std::uint32_t*>(&data);
			words[0] |= 1u << 10;  // kInverseSquare
			*reinterpret_cast<float*>(&words[1]) =
				std::clamp(kK * fade / (reach * reach + s->size * s->size), 0.01f, 0.99f);
		}
		light->local.translate = s->position;
		light->local.scale = 1.0f;
		parent->AttachChild(light, true);
		RE::NiUpdateData update{};
		light->Update(update);
		RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
		params.dynamic = true;
		params.shadowLight = false;
		params.portalStrict = true;
		params.affectLand = true;
		params.affectWater = true;
		params.neverFades = true;
		params.fov = kFieldOfView;
		params.falloff = 1.0f;
		params.nearDistance = 5.0f;
		params.depthBias = 1.0f;
		params.sceneGraphIndex = 0;
		params.restrictedNode = nullptr;
		params.lensFlareData = nullptr;
		auto* bs = scene->AddLight(light, params);
		if (!bs) {
			parent->DetachChild(light);
			SKSE::log::warn("[STREAM] {}: the light could not be registered", s->key);
			return;
		}
		gLive.push_back({ RE::NiPointer<RE::BSLight>(bs), RE::NiPointer<RE::NiPointLight>(light), s, fade, reach });
		++gCount[s];
		if (gTold < 12) {
			++gTold;
			SKSE::log::info("[STREAM] {} | on {} | fade {:.2f} | radius {:.0f}", s->key,
				parent == root ? "its root" : s->node.c_str(), fade, s->radius);
		}
	}

	void UpdateStreamLights()
	{
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		const float scale = Brightness();
		const float reachScale = Reach();
		std::lock_guard l{ gLock };
		std::erase_if(gLive, [&](Live& v) {
			// the object's 3D has gone: RE::Light's own rule, a light whose parent is gone leaves the scene
			if (!v.light || !v.light->parent) {
				if (scene && v.bs) {
					scene->RemoveLight(v.bs);
				}
				if (auto it = gCount.find(v.stream); it != gCount.end() && it->second) {
					--it->second;
				}
				return true;
			}
			const bool off = OptionOff(v.stream);
			if (off && !v.light->GetAppCulled()) {
				v.light->SetAppCulled(true);
			} else if (!off && v.light->GetAppCulled() && !(SneakOn() && RE::PlayerCharacter::GetSingleton() &&
															   RE::PlayerCharacter::GetSingleton()->IsSneaking())) {
				v.light->SetAppCulled(false);
			}
			const float want = v.stream->fade * scale;
			const float wantReach = v.stream->radius * reachScale;
			if (want != v.written || wantReach != v.wroteReach) {
				auto& d = v.light->GetLightRuntimeData();
				d.fade = want;
				d.radius.x = wantReach;
				d.radius.y = wantReach;
				auto* words = reinterpret_cast<std::uint32_t*>(&d);
				*reinterpret_cast<float*>(&words[1]) =
					std::clamp(kK * want / (wantReach * wantReach + v.stream->size * v.stream->size), 0.01f, 0.99f);
				v.written = want;
				v.wroteReach = wantReach;
			}
			return false;
		});
	}

	std::size_t LiveStreamLights()
	{
		std::lock_guard l{ gLock };
		return gLive.size();
	}
}
