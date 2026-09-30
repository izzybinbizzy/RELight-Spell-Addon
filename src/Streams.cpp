// RELight - Spell Addon - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Lights that travel - sprays, breath shouts and beams. A RE::Light config cannot bind a light to a named node
// (its `attachPath` is child indices), so the build writes these as `stream` lines and this hangs the lights.
//
// CREDIT: how a light is made and registered here follows ReLight by Truman (github.com/TrumanGIT/ReLight),
// GPL-3.0-or-later, with his permission and kept under the same license - the same way Illuminated's
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
		constexpr std::size_t kMaxObjectsPerStream = 4;  // a spray spawns a new object many times a second
		constexpr std::size_t kMaxLiveTotal = 48;          // lights, all streams together (a ladder is several per object)
		constexpr float       kFieldOfView = 90.0f;   // what a light that casts no shadow is given (from ReLight)
		constexpr const char* kLightName = "RSAStream";

		RE::NiPointer<RE::NiPointLight> gMaster;

		struct Live
		{
			RE::NiPointer<RE::BSLight>      bs;
			RE::NiPointer<RE::NiPointLight> light;
			const Stream*                   stream{ nullptr };
			RE::FormID                      owner{ 0 };         // the reference whose 3D carries it
			float                           written{ -1.0f };   // the fade we last wrote
			float                           wroteReach{ -1.0f };  // the reach we last wrote
			bool                            attached{ false };    // the queued node attach has run (it had a parent once)
		};

		std::mutex                                      gLock;
		std::vector<Live>                               gLive;
		std::unordered_map<const Stream*, std::size_t>  gCount;

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());  // let go once cloned
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

		bool OptionOff(const Stream* a_s) { return a_s && !OptionLit(a_s->option); }

		std::size_t gTold = 0;

		// takes one light out of the scene and the per-stream count (the caller erases it from gLive). `a_detach` only while
		// the owner's 3D is known to be alive (the release hook): after that its parent may already be freed.
		void Retire(Live& a_v, RE::ShadowSceneNode* a_scene, bool a_detach)
		{
			if (a_scene && a_v.bs) {
				a_scene->RemoveLight(a_v.bs);
			}
			if (a_detach && a_v.light && a_v.light->parent) {
				a_v.light->parent->DetachChild(a_v.light.get());
			}
			if (auto it = gCount.find(a_v.stream); it != gCount.end() && it->second) {
				--it->second;
			}
		}

		// the object the light hangs on has left the game. A finished bolt can leave its node tree alive with our light
		// still parented to it (measured 2026-09-24: Thunderbolt's light stayed lit after the bolt), so the light's parent
		// alone does not say the object left. The release hook in main.cpp is the main route; this catches what it misses.
		// ⚠ 2026-09-29: the attach is queued (MakeLight), so a light with no parent YET is waiting for it, not orphaned.
		[[nodiscard]] bool OwnerGone(Live& a_v)
		{
			if (!a_v.light) {
				return true;
			}
			if (a_v.light->parent) {
				a_v.attached = true;
			} else if (a_v.attached) {
				return true;
			}
			const auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_v.owner);
			return !ref || ref->IsDeleted() || ref->IsDisabled();
		}

		// each projectile we hang a travelling light on, and the light its own record gives it
		struct OwnLight
		{
			RE::BGSProjectile*  projectile;
			RE::TESObjectLIGH*  own;
			const Stream*       stream;
		};
		std::vector<OwnLight>                        gOwn;
		std::unordered_set<const RE::TESObjectLIGH*> gSpraySet;
	}

	// One object, one light: while an option's travelling light is on, the projectile's own record light is taken off
	// in memory, and given back when the option is switched off.
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

	// A spray is lit by its own light record, which RE::Light tunes through our `isPluginLight` config. Many of those
	// records are shared (fireballs, casting lights, explosions, hazards, muzzle flashes), so the record is taken off
	// every user that is not a spray, in memory - RE::Light would have switched those unconfigured lights off anyway.
	void ClaimSprayLights()
	{
		gSpraySet.clear();
		for (const auto id : SprayLightRecords()) {
			if (auto* l = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id)) {
				gSpraySet.insert(l);
			}
		}
		if (gSpraySet.empty()) {
			return;
		}
		auto*       dh = RE::TESDataHandler::GetSingleton();
		std::size_t proj = 0, muzzle = 0, effects = 0, expl = 0, hazards = 0;
		for (auto* p : dh->GetFormArray<RE::BGSProjectile>()) {
			if (!p) {
				continue;
			}
			if (p->data.light && gSpraySet.contains(p->data.light) && !p->IsFlamethrower() && !p->IsCone()) {
				p->data.light = nullptr;
				++proj;
			}
			if (p->data.muzzleFlashLight && gSpraySet.contains(p->data.muzzleFlashLight)) {
				p->data.muzzleFlashLight = nullptr;
				++muzzle;
			}
		}
		for (auto* e : dh->GetFormArray<RE::EffectSetting>()) {
			if (e && e->data.light && gSpraySet.contains(e->data.light)) {
				e->data.light = nullptr;
				++effects;
			}
		}
		for (auto* x : dh->GetFormArray<RE::BGSExplosion>()) {
			if (x && x->data.light && gSpraySet.contains(x->data.light)) {
				x->data.light = nullptr;
				++expl;
			}
		}
		for (auto* h : dh->GetFormArray<RE::BGSHazard>()) {
			if (h && h->data.light && gSpraySet.contains(h->data.light)) {
				h->data.light = nullptr;
				++hazards;
			}
		}
		SKSE::log::info("spray lights: {} record(s) are the sprays' alone now - taken off {} projectile(s), {} muzzle flash(es), "
						"{} magic effect(s), {} explosion(s), {} hazard(s)",
			gSpraySet.size(), proj, muzzle, effects, expl, hazards);
	}

	bool IsSprayLight(const RE::TESObjectLIGH* a_light)
	{
		return a_light && gSpraySet.contains(a_light);
	}

	namespace
	{
		// one travelling light, made the way ReLight makes its lights, registered with the shadow scene node
		[[nodiscard]] RE::BSLight* MakeLight(const Stream& a_s, const RE::NiColor& a_colour, const RE::NiPoint3& a_at, float a_fade,
			float a_reach, RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene, RE::NiPointLight*& a_made, bool a_queue)
		{
			auto* light = CloneMaster();
			if (!light) {
				return nullptr;
			}
			light->name = kLightName;
			auto& data = light->GetLightRuntimeData();
			data.diffuse = a_colour;
			data.fade = a_fade;
			data.radius = { a_reach, a_reach, a_s.size };  // x and y are the reach; z carries the light's size
			light->SetLightAttenuation(a_reach);            // without it the light has no attenuation and lights nothing
			// after SetLightAttenuation, which writes the same two words; the cutoff is re-derived so Brightness moves
			// the peak with the reach held (at 100%/100% it equals the file's)
			Isl::SetOn(light);
			Isl::SetCutoff(light, CutoffFor(a_fade, a_reach, a_s.size));
			light->local.translate = a_at;
			light->local.scale = 1.0f;
			const RE::NiPointer<RE::NiPointLight> hold(light);   // freed here if it is never registered
			// ⚠ 2026-09-29, the Lightning Bolt freeze (a race - Truman's call and his method): Load3D also runs on the game's
			// loader threads, and a child attached straight onto the beam's live node tree from there raced the main thread.
			// A travelling light's attach goes through the game's own task queue, which does it on the main thread
			// (TaskQueueInterface::QueueNodeAttach, after the light is registered). A held light (Held.cpp) is made on the main
			// thread and attaches at once, as before.
			if (!a_queue) {
				a_parent->AttachChild(light, true);
				RE::NiUpdateData update{};
				light->Update(update);
			}
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
			auto* bs = a_scene->AddLight(light, params);
			if (!bs) {
				if (!a_queue) {
					a_parent->DetachChild(light);
				}
				return nullptr;
			}
			if (a_queue) {
				if (auto* queue = RE::TaskQueueInterface::GetSingleton()) {
					queue->QueueNodeAttach(light, a_parent);
				} else {
					a_parent->AttachChild(light, true);
				}
			}
			a_made = light;
			return bs;
		}

		[[nodiscard]] bool PlayerHidesLights()
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return SneakOn() && player && player->IsSneaking();
		}
	}

	RE::BSLight* MakeOurLight(const Stream& a_s, const RE::NiColor& a_colour, const RE::NiPoint3& a_at, float a_fade, float a_reach,
		RE::NiNode* a_parent, RE::ShadowSceneNode* a_scene, RE::NiPointLight*& a_made)
	{
		return MakeLight(a_s, a_colour, a_at, a_fade, a_reach, a_parent, a_scene, a_made, false);   // Held.cpp: the main thread
	}

	void HangStreamLights(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root)
	{
		if (!a_ref || !a_root) {
			return;
		}
		const auto*   base = a_ref->GetBaseObject();
		const Stream* s = StreamOf(base);
		if (!s || OptionOff(s)) {
			return;
		}
		auto* root = a_root->AsNode();
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!root || !scene) {
			return;
		}
		std::lock_guard l{ gLock };
		const std::size_t each = s->positions.size();
		if (gLive.size() + each > kMaxLiveTotal || gCount[s] + each > kMaxObjectsPerStream * each) {
			return;
		}
		RE::NiNode* parent = root;
		if (s->node != "-") {
			if (auto* n = root->GetObjectByName(RE::BSFixedString(s->node.c_str())); n && n->AsNode()) {
				parent = n->AsNode();
			}
		}
		const auto* tint = TintOf(base);
		const auto& colour = tint ? *tint : s->color;
		const float fade = s->fade * Brightness();
		const float reach = s->radius * Reach();
		for (const auto& at : s->positions) {
			RE::NiPointLight* light = nullptr;
			auto*             bs = MakeLight(*s, colour, at, fade, reach, parent, scene, light, true);
			if (!bs) {
				SKSE::log::warn("[STREAM] {}: the light could not be made or registered", s->key);
				return;
			}
			gLive.push_back({ RE::NiPointer<RE::BSLight>(bs), RE::NiPointer<RE::NiPointLight>(light), s, a_ref->GetFormID(), fade, reach });
			++gCount[s];
			if (gTold < 24) {
				++gTold;
				SKSE::log::info("[STREAM] {} | on {} at ({:.0f}, {:.0f}, {:.0f}) | fade {:.2f} | radius {:.0f}", s->key,
					parent == root ? "its root" : s->node.c_str(), at.x, at.y, at.z, fade, s->radius);
			}
		}
	}

	void UpdateStreamLights()
	{
		auto*       scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		const float scale = Brightness();
		const float reachScale = Reach();
		const bool  hidden = PlayerHidesLights();
		std::lock_guard l{ gLock };
		std::erase_if(gLive, [&](Live& v) {
			if (OwnerGone(v)) {
				Retire(v, scene, false);
				return true;
			}
			const bool off = OptionOff(v.stream);
			if (off && !v.light->GetAppCulled()) {
				v.light->SetAppCulled(true);
			} else if (!off && !hidden && v.light->GetAppCulled()) {
				v.light->SetAppCulled(false);
			}
			const float fade = v.stream->fade * scale;
			const float reach = v.stream->radius * reachScale;
			if (fade != v.written || reach != v.wroteReach) {
				auto& d = v.light->GetLightRuntimeData();
				d.fade = fade;
				d.radius.x = reach;
				d.radius.y = reach;
				Isl::SetCutoff(v.light.get(), CutoffFor(fade, reach, v.stream->size));
				v.written = fade;
				v.wroteReach = reach;
			}
			return false;
		});
	}

	void DropStreamLights(const RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return;
		}
		auto*           scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		const auto      id = a_ref->GetFormID();
		std::lock_guard l{ gLock };
		std::erase_if(gLive, [&](Live& v) {
			if (v.owner != id) {
				return false;
			}
			Retire(v, scene, true);
			return true;
		});
	}

	std::size_t LiveStreamLights()
	{
		std::lock_guard l{ gLock };
		return gLive.size();
	}
}
