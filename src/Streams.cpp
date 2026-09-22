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
			float                           written{ -1.0f };
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
		data.diffuse = s->color;
		data.fade = fade;
		// x and y are the reach; z carries the light's SIZE, not a third radius (ReLight and Light Placer both do this)
		data.radius = { s->radius, s->radius, s->size };
		// without this a light has no attenuation of its own and never brightens anything (Light Placer does it too)
		light->SetLightAttenuation(s->radius);
		// ⚫ Community Shaders' inverse square lighting reads its flag and cutoff out of the light's own data, in
		// the words before the color - RE::Light's `Overlay` (LightData.h) writes exactly these two, so ours
		// render the way its config lights do. Set AFTER SetLightAttenuation, which writes those words too.
		{
			auto* words = reinterpret_cast<std::uint32_t*>(&data);
			words[0] |= 1u << 10;                              // kInverseSquare
			*reinterpret_cast<float*>(&words[1]) = s->cutoff;  // cutoffOverride
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
		gLive.push_back({ RE::NiPointer<RE::BSLight>(bs), RE::NiPointer<RE::NiPointLight>(light), s, fade });
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
			if (want != v.written) {
				v.light->GetLightRuntimeData().fade = want;
				v.written = want;
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
