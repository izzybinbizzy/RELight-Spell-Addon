// The fading module (Illuminated and RELight - Spell Addon carry identical copies; FadeConfig.h is what differs)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// The lights on a weapon, and scaling them. This module makes no light (but our own, FadeOwnLight.cpp): it dims the ones
// other mods hang on a weapon - Light Placer's, RE::Light's, lights inside Enchantment Art Extender art or a weapon's
// own mesh, anything that is a NiPointLight in the scene graph under the weapon.
//
// Where a hand's lights hang, all of which are searched every frame:
//   - the weapon's 3D in the actor's biped: the right hand's in its weapon-type slot, the left hand's in the shield slot
//     (the game puts a left-hand weapon there); the player's first-person biped too;
//   - the attach root of every enchantment effect on the weapon (WeaponEnchantmentController::attachRoot), handed over
//     by the reference-effect hooks (the plugin's one wrapper per hook). Light Placer hangs an enchantment art's lights
//     under that root - and for
//     the player in first person it moves them to the third-person weapon, which the biped search also covers.
//
// When: Light Placer rewrites a flickering or animated light's fade in ReferenceEffect::UpdatePosition and in the cell's
// animation pass; a steady light it sets once. So the scaling runs twice a frame - right after each enchantment effect's
// update (AfterReferenceEffect) and in the player's update (UpdateHands) - and each light keeps Glow::Scaled's
// "what we wrote / what it was" pair, so running twice, or on a light nobody rewrites, never compounds. The Brightness
// slider scales the same lights just before (the plugin's Brightness pass): each fade written here is reported to it
// (Plugin::NoteFadeWrite), and it reports a slider change on a steady light back (Rebase), so neither takes the other's
// write for a new value.
//
// When a light stops being under a tracked hand (weapon put away into the inventory, swapped, charge refilled on an
// exempt weapon) it is put back as we found it, unless someone else has written it since.
//
// "Dim the enchantment glow" does the same to the glow itself: the enchantment shader's fill and rim alpha
// (AfterShaderEffect) and the emissive scale of an enchantment art's glowing meshes (DimArt: VAER's swirls, EAE's art).

#include "Fade.h"
#include "Plugin.h"

namespace Fade
{
	namespace
	{
		constexpr std::uint32_t kKeepFrames = 2;    // a light, root or shader not seen for this many frames is let go
		constexpr std::uint32_t kForgetHand = 600;  // a hand not seen for this many frames (~10 s) is forgotten

		struct ColorKeep
		{
			RE::NiColor base{}, written{};  // written: what we wrote last (meaningful once `touched`)
			bool        touched{ false };

			[[nodiscard]] static bool Same(const RE::NiColor& a, const RE::NiColor& b) noexcept
			{
				return a.red == b.red && a.green == b.green && a.blue == b.blue;
			}
			// a_fire: the hand's magic is fire - an orange light cools to ember, every other to grey (Glow::TintFor)
			RE::NiColor Apply(const RE::NiColor& a_current, bool a_fire, float a_amount)
			{
				if (!touched || !Same(a_current, written)) {
					base = a_current;
				}
				const Glow::Rgb own{ base.red, base.green, base.blue };
				const auto      c = Glow::Cool(own, Glow::TintFor(a_fire, own), a_amount);
				written = { c.r, c.g, c.b };
				touched = true;
				return written;
			}
			[[nodiscard]] RE::NiColor Restore(const RE::NiColor& a_current) const
			{
				return touched && Same(a_current, written) ? base : a_current;
			}
		};

		struct LightSeen
		{
			RE::NiPointer<RE::NiPointLight> light;
			Glow::Scaled                    fade, radius;
			ColorKeep                       color;
			std::uint32_t                   frame{ 0 };
			std::uint64_t                   hand{ 0 };                               // the hand whose numbers it last took, for ReapplyAll
			float                           shownFade{ 0.0f }, shownRadius{ 0.0f };  // what it held after our last write
		};

		struct ShaderSeen
		{
			RE::NiPointer<RE::ShaderReferenceEffect> effect;
			Glow::Scaled                             fill, rim;
			std::uint32_t                            frame{ 0 };
		};

		// an enchantment art's glowing mesh (VAER's swirls, EAE's art): its effect material's emissive scale, keyed by the
		// material, since two meshes may share one (two keys on one material would each take the other's write as a base)
		struct ArtSeen
		{
			RE::NiPointer<RE::BSShaderProperty> property;  // keeps the material alive while we hold its base
			Glow::Scaled                        scale;
			std::uint32_t                       frame{ 0 };
		};

		struct Root
		{
			RE::NiPointer<RE::NiAVObject> node;
			std::uint32_t                 frame{ 0 };
		};

		struct HandTrack
		{
			RE::ActorHandle  actor;
			std::uint64_t    key{ 0 };              // its own place in gHands (Key, or SpellKey for a spell hand)
			const RE::Actor* actorSeen{ nullptr };  // the actor as last read on the main thread: only compared, never used
			bool             left{ false };
			bool             spell{ false };                  // a spell in hand (Spells.cpp): follows magicka, keyed apart (SpellKey)
			bool             fire{ false };                   // its strongest effect is fire: an orange light cools to ember (Glow::TintFor)
			Glow::Rgb        hudColor{ 0.32f, 0.58f, 1.0f };  // a spell hand's own light at full, for the reticle
			int              school{ -1 };                    // a spell hand's school (Kind::school), for the Crossfire bar
			int              element{ -1 };
			bool             active{ false };
			std::uint32_t    frame{ 0 };
			const void*      weapon{ nullptr };
			const void*      instance{ nullptr };
			Glow::Hand       glow{};
			Glow::Output     out{};
			Verdict          verdict{};
			Reading          reading{};
			// what the verdict was worked out for (it is kept until one of these changes)
			bool              verdictKnown{ false };
			const void*       verdictWeapon{ nullptr };
			const void*       verdictEnch{ nullptr };
			RE::FormID        verdictEnchID{ 0 };  // with the pointer: an enchantment freed and made again is another
			std::uint32_t     verdictRules{ 0 };
			Settings          verdictSettings{};
			float             fraction{ 1.0f };
			std::vector<Root> effectRoots;  // enchantment effects' attach roots, from the reference-effect hooks
			std::size_t       lights{ 0 }, roots{ 0 };
			std::size_t       others{ 0 };       // of `lights`, the ones another mod hung (not our own light)
			float             unlitFor{ 0.0f };  // seconds tracked with no other mod's light on the weapon
			std::string       actorName;
			std::string       weaponLabel, enchLabel;  // made on the main thread, for the Debug page and DevBench
			float             chargeAV{ -1.0f };       // the game's own item-charge value, read on the main thread
		};

		std::mutex                                       gLock;  // the player update and the effect hooks may be on different threads
		std::uint32_t                                    gFrame = 1;
		std::unordered_map<std::uint64_t, HandTrack>     gHands;
		std::unordered_map<RE::NiPointLight*, LightSeen> gLights;
		std::unordered_map<const void*, ShaderSeen>      gShaders;
		std::unordered_map<const void*, ArtSeen>         gArt;
		// how many hands are active, read without the lock by the effect hooks: every art and shader effect in the world
		// calls them each frame, and with no enchanted weapon out they need not look up (RTTI) whose effect it is
		std::atomic<std::size_t> gActiveHands{ 0 };
		// this frame's switches, for the effect hooks, which run for every effect in the world and must not lock and copy
		// the settings each time
		std::atomic<bool> gEnabled{ false }, gDimShader{ false };
		// the HUD's copy (Fade.h HudState): written at the end of each UpdateHands, read by the render thread. Its own small
		// lock, held only while a few dozen bytes are copied - the render thread never waits on the per-frame pass (gLock)
		std::mutex gHudLock;
		HudState   gHud;

		[[nodiscard]] std::uint64_t Key(RE::ActorHandle a_actor, bool a_left)
		{
			return (static_cast<std::uint64_t>(a_actor.native_handle()) << 1) | (a_left ? 1u : 0u);
		}

		// a hand holding a spell: never the same key as the weapon hand, which the effect hooks look up by Key
		[[nodiscard]] std::uint64_t SpellKey(RE::ActorHandle a_actor, bool a_left) { return Key(a_actor, a_left) | (1ull << 63); }

		// the weapon's 3D in a biped: a left-hand weapon sits in the shield slot, a right-hand one in its type's slot
		RE::NiAVObject* WeaponPart(RE::Actor* a_actor, bool a_firstPerson, bool a_left, const RE::TESForm* a_weapon)
		{
			const auto& biped = a_actor->GetBiped(a_firstPerson);
			if (!biped || !a_weapon) {
				return nullptr;
			}
			using B = RE::BIPED_OBJECT;
			if (a_left) {
				const auto& shield = biped->objects[B::kShield];
				if (shield.item == a_weapon && shield.partClone) {
					return shield.partClone.get();
				}
				return nullptr;
			}
			for (auto slot = static_cast<std::uint32_t>(B::kHandToHandMelee); slot <= static_cast<std::uint32_t>(B::kCrossbow); ++slot) {
				const auto& obj = biped->objects[slot];
				if (obj.item == a_weapon && obj.partClone) {
					return obj.partClone.get();
				}
			}
			return nullptr;
		}

		// a_found: the light was found under the hand this frame (a re-apply of this frame's numbers does not count, or a
		// light that has left the weapon would never be let go)
		void ApplyLight(RE::NiPointLight* a_light, const HandTrack& a_hand, bool a_found = true)
		{
			auto [it, added] = gLights.try_emplace(a_light);
			auto& seen = it->second;
			if (added) {
				seen.light.reset(a_light);
			}
			if (a_found) {
				seen.frame = gFrame;
			}
			seen.hand = a_hand.key;  // a spell hand's own key: ReapplyAll must find the hand that scaled it, not the weapon hand
			auto&       data = a_light->GetLightRuntimeData();
			const auto& t = a_hand.verdict.tuning;
			const float before = data.fade;
			data.fade = seen.fade.Apply(before, a_hand.out.brightness);
			Plugin::NoteFadeWrite(a_light, before, data.fade);
			if (t.reachFollows > 0.0f || seen.radius.Written()) {
				const float r = seen.radius.Apply(data.radius.x, a_hand.out.reach);
				data.radius.x = r;  // x and y are the reach, z is the size
				data.radius.y = r;
			}
			if (a_hand.out.cool > 0.0f || seen.color.touched) {
				data.diffuse = seen.color.Apply(data.diffuse, a_hand.fire, a_hand.out.cool);
			}
			seen.shownFade = data.fade;  // for LightsNow, which must not read the game's light off the main thread
			seen.shownRadius = data.radius.x;
		}

		void RestoreLight(LightSeen& a_seen)
		{
			if (!a_seen.light) {
				return;
			}
			auto&       data = a_seen.light->GetLightRuntimeData();
			const float before = data.fade;
			data.fade = a_seen.fade.Restore(before);
			Plugin::NoteFadeWrite(a_seen.light.get(), before, data.fade);
			if (a_seen.radius.Written()) {
				const float r = a_seen.radius.Restore(data.radius.x);
				data.radius.x = r;
				data.radius.y = r;
			}
			data.diffuse = a_seen.color.Restore(data.diffuse);
		}

		void RestoreShader(ShaderSeen& a_seen)
		{
			// a finished effect's shader data may already be gone: nothing to put back on it
			auto* data = a_seen.effect && !a_seen.effect->finished ? a_seen.effect->effectShaderData : nullptr;
			if (data) {
				data->fillColor.alpha = a_seen.fill.Restore(data->fillColor.alpha);
				data->rimColor.alpha = a_seen.rim.Restore(data->rimColor.alpha);
			}
		}

		RE::BSEffectShaderMaterial* EffectMaterial(RE::BSShaderProperty* a_property)
		{
			auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(a_property);
			return effect ? static_cast<RE::BSEffectShaderMaterial*>(effect->GetBaseMaterial()) : nullptr;
		}

		void RestoreArt(ArtSeen& a_seen)
		{
			if (auto* material = a_seen.property ? EffectMaterial(a_seen.property.get()) : nullptr) {
				material->baseColorScale = a_seen.scale.Restore(material->baseColorScale);
			}
		}

		// every glowing mesh of an enchantment art takes the hand's brightness (never past its own: a pulse or flare can
		// bring a dimmed glow back up, not over-brighten it)
		void DimArt(RE::NiAVObject* a_model, float a_brightness)
		{
			const float k = std::clamp(a_brightness, 0.0f, 1.0f);
			RE::BSVisit::TraverseScenegraphGeometries(a_model, [&](RE::BSGeometry* a_geometry) {
				auto* shader = a_geometry ? a_geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
				if (auto* material = EffectMaterial(shader)) {
					auto [it, added] = gArt.try_emplace(material);
					if (added) {
						it->second.property.reset(shader);
					}
					it->second.frame = gFrame;
					material->baseColorScale = it->second.scale.Apply(material->baseColorScale, k);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// every NiPointLight under a root takes this hand's numbers; returns how many (and adds to a_others the ones that are
		// not our own light)
		std::size_t ApplyUnder(RE::NiAVObject* a_root, const HandTrack& a_hand, std::size_t* a_others = nullptr)
		{
			std::size_t n = 0;
			if (!a_root) {
				return n;
			}
			RE::BSVisit::TraverseScenegraphLights(a_root, [&](RE::NiPointLight* a_light) {
				if (a_light) {
					if (!gLights.contains(a_light) && DebugLogOn()) {
						std::string chain;
						for (auto* p = a_light->parent; p && chain.size() < 200; p = p->parent) {
							chain += std::format(" < {}", p->name.empty() ? "(unnamed)" : p->name.c_str());
						}
						SKSE::log::info("  new light under {}: {}{}", a_root->name.empty() ? "(unnamed)" : a_root->name.c_str(),
							a_light->name.empty() ? "(unnamed)" : a_light->name.c_str(), chain);
					}
					ApplyLight(a_light, a_hand);
					++n;
					if (a_others && a_light->name != kOwnLightName) {
						++*a_others;
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return n;
		}

		// the colour of the brightest light another mod (or the game) hangs at this hand's magic node - a staff's spell light
		// from a light mod or the game's own casting light; nullopt when there is none
		std::optional<RE::NiColor> OwnLightHint(RE::Actor* a_actor, bool a_left)
		{
			const auto&                name = a_left ? RE::FixedStrings::GetSingleton()->npcLMagicNode : RE::FixedStrings::GetSingleton()->npcRMagicNode;
			std::optional<RE::NiColor> best;
			float                      bestStrength = 0.0f;
			for (const bool first : { false, true }) {
				if (first && !a_actor->IsPlayerRef()) {
					break;
				}
				auto* root = a_actor->Get3D(first);
				auto* node = root ? root->GetObjectByName(name) : nullptr;
				if (!node) {
					continue;
				}
				RE::BSVisit::TraverseScenegraphLights(node, [&](RE::NiPointLight* a_light) {
					if (a_light && a_light->name != kOwnLightName) {
						const auto& d = a_light->GetLightRuntimeData();
						const float strength = std::fabs(d.fade) * (std::max)({ d.diffuse.red, d.diffuse.green, d.diffuse.blue });
						if (std::isfinite(strength) && strength > bestStrength) {
							bestStrength = strength;
							best = d.diffuse;
						}
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			return best;
		}

		void ApplyHand(RE::Actor* a_actor, HandTrack& a_hand, const Settings& a_settings, float a_delta)
		{
			a_hand.lights = a_hand.roots = a_hand.others = 0;
			const auto*                    weapon = a_hand.reading.weapon;
			std::array<RE::NiAVObject*, 2> parts{ WeaponPart(a_actor, false, a_hand.left, weapon), nullptr };
			if (a_actor->IsPlayerRef()) {
				parts[1] = WeaponPart(a_actor, true, a_hand.left, weapon);
			}
			for (auto* part : parts) {
				if (part) {
					++a_hand.roots;
					a_hand.lights += ApplyUnder(part, a_hand, &a_hand.others);
				}
			}
			std::erase_if(a_hand.effectRoots, [](const Root& r) { return !r.node || gFrame - r.frame > kKeepFrames; });
			for (auto& root : a_hand.effectRoots) {
				// an effect root under the weapon part was covered above; searching it again changes nothing
				++a_hand.roots;
				a_hand.lights += ApplyUnder(root.node.get(), a_hand, &a_hand.others);
			}
			// our own light, on the third-person model (it lights the first-person view too), when nothing else lights it,
			// only while the weapon is drawn: sheathed, its enchantment's own light is gone and the weapon must stay dark.
			// Only where FadeConfig.h offers it: a mod without it neither searches for a hint nor keeps a light.
			if constexpr (Mod::kOwnLight) {
				const auto* state = a_actor->AsActorState();
				const bool  drawn = state && state->IsWeaponDrawn();
				a_hand.unlitFor = (a_hand.others || !drawn) ? 0.0f : a_hand.unlitFor + (std::max)(a_delta, 0.0f);
				const bool want = Glow::WantsOwnLight(a_settings.ownLight, a_hand.reading.bound, a_hand.unlitFor, parts[0] != nullptr);
				if (auto* made = KeepOwnLight(Key(a_hand.actor, a_hand.left), want ? parts[0] : nullptr, a_hand.reading.ench)) {
					ApplyLight(made, a_hand);
					++a_hand.lights;
				}
				if (want && parts[0]) {
					if (const auto hint = OwnLightHint(a_actor, a_hand.left)) {
						TintOwnLight(Key(a_hand.actor, a_hand.left), *hint);
					}
				}
			} else {
				(void)a_settings;
				(void)a_delta;
			}
		}

		// the actors whose hands are read this frame, in a list kept from frame to frame (no allocation once it has grown)
		std::vector<RE::NiPointer<RE::Actor>> gActors;  // emptied by ReleaseAll, so no actor is held across a load

		std::vector<RE::NiPointer<RE::Actor>>& Actors(Who a_who)
		{
			auto& out = gActors;
			out.clear();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->Is3DLoaded()) {
				out.emplace_back(player);
			}
			if (a_who != Who::kPlayer) {
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& handle : lists->highActorHandles) {
						auto actor = handle.get();
						if (actor && actor.get() != player && (a_who == Who::kEveryone || actor->IsPlayerTeammate()) && actor->Is3DLoaded() && !actor->IsDead()) {
							out.push_back(std::move(actor));
						}
					}
				}
			}
			return out;
		}

		void Sweep()
		{
			for (auto it = gLights.begin(); it != gLights.end();) {
				auto&      seen = it->second;
				const bool gone = !seen.light || seen.light->GetRefCount() <= 1;  // only we still hold it: it has left the game
				if (gone || gFrame - seen.frame > kKeepFrames) {
					if (!gone) {
						RestoreLight(seen);
					}
					it = gLights.erase(it);
				} else {
					++it;
				}
			}
			for (auto it = gShaders.begin(); it != gShaders.end();) {
				if (gFrame - it->second.frame > kKeepFrames) {
					RestoreShader(it->second);
					it = gShaders.erase(it);
				} else {
					++it;
				}
			}
			for (auto it = gArt.begin(); it != gArt.end();) {
				if (gFrame - it->second.frame > kKeepFrames) {
					RestoreArt(it->second);
					it = gArt.erase(it);
				} else {
					++it;
				}
			}
			std::erase_if(gHands, [](const auto& kv) { return gFrame - kv.second.frame > kForgetHand; });
		}

		// by the actor's address, not its handle: this runs on any thread (the effect hooks, the API), and making or reading
		// a handle is the main thread's to do. A handful of hands at most, so a walk is as quick as a lookup. Under gLock.
		HandTrack* ActiveHand(const RE::Actor* a_actor, bool a_left)
		{
			if (!a_actor) {
				return nullptr;
			}
			for (auto& [key, h] : gHands) {
				if (h.active && !h.spell && h.left == a_left && h.actorSeen == a_actor) {
					return &h;
				}
			}
			return nullptr;
		}

		// the hand an enchantment effect belongs to, and its actor; false when it is not a weapon enchantment's
		bool HandOfEffect(RE::ReferenceEffect* a_effect, RE::Actor*& a_actor, bool& a_left, RE::NiAVObject*& a_root)
		{
			auto* ctrl = a_effect ? skyrim_cast<RE::WeaponEnchantmentController*>(a_effect->controller) : nullptr;
			if (!ctrl || !ctrl->target || !ctrl->caster) {
				return false;
			}
			a_actor = ctrl->target;
			a_left = ctrl->caster->castingSource == RE::MagicSystem::CastingSource::kLeftHand;
			a_root = ctrl->attachRoot.get();
			return true;
		}

		// with the debug log on, when a weapon starts being tracked: every light hanging anywhere on the actor, with the nodes
		// above it - so a hand that finds 0 lights says where the lights ARE (a lighting mod hanging them somewhere new)
		void LogActorLights(RE::Actor* a_actor)
		{
			std::size_t n = 0;
			for (const bool first : { false, true }) {
				auto* root = a_actor->Get3D(first);
				if (!root || (first && !a_actor->IsPlayerRef())) {
					continue;
				}
				RE::BSVisit::TraverseScenegraphLights(root, [&](RE::NiPointLight* a_light) {
					if (a_light && n < 16) {
						++n;
						std::string chain;
						for (auto* p = a_light->parent; p && chain.size() < 160; p = p->parent) {
							chain += std::format(" < {}", p->name.empty() ? "(unnamed)" : p->name.c_str());
						}
						const auto& d = a_light->GetLightRuntimeData();
						SKSE::log::info("  light {} ({} view): {}{} | fade {:.2f} radius {:.0f}", n, first ? "first" : "third",
							a_light->name.empty() ? "(unnamed)" : a_light->name.c_str(), chain, d.fade, d.radius.x);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			SKSE::log::info("  {} light(s) on {} in all", n, a_actor->GetName());
		}

		// every light, shader and art mesh back as we found it, and every hand forgotten (switched off, a load)
		void RestoreAll()
		{
			DropOwnLights();
			for (auto& [light, seen] : gLights) {
				RestoreLight(seen);
			}
			for (auto& [key, seen] : gShaders) {
				RestoreShader(seen);
			}
			for (auto& [key, seen] : gArt) {
				RestoreArt(seen);
			}
			gLights.clear();
			gShaders.clear();
			gArt.clear();
			gHands.clear();
			gActiveHands = 0;
		}

		// one spell hand, one frame (from UpdateHands, under the lock)
		void UpdateSpellHand(RE::Actor* a_actor, bool a_left, const Settings& a_s, float a_delta, bool a_previewOn, bool a_previewToggled,
			float a_previewFraction)
		{
			auto& h = gHands[SpellKey(a_actor->GetHandle(), a_left)];
			h.actor = a_actor->GetHandle();
			h.key = SpellKey(h.actor, a_left);
			h.actorSeen = a_actor;
			h.left = a_left;
			h.spell = true;
			h.frame = gFrame;
			SpellHand sp;
			if (!ReadSpellHand(a_actor, a_left, sp)) {
				h.active = false;
				h.weapon = nullptr;
				h.reading = {};
				return;
			}
			h.reading = {};
			h.reading.tracked = true;
			h.reading.current = sp.current;
			h.reading.max = sp.max;
			h.reading.fraction = sp.fraction;
			const Kind kind = KindOf(sp.spell);
			h.fire = kind.element == Element::kFire;
			h.element = static_cast<int>(kind.element);
			h.school = kind.school;
			if (const auto off = KindOff(a_s, kind); !off.empty()) {
				if (h.active || h.weapon != sp.spell) {
					h.weaponLabel = Label(sp.spell);
					h.verdict.why = off;
					if (a_s.debugLog) {
						SKSE::log::info("{} {} hand: spell left alone | {} - {}", a_actor->GetName(), a_left ? "left" : "right", h.weaponLabel, off);
					}
				}
				h.active = false;
				h.weapon = sp.spell;
				return;
			}
			h.fraction = a_previewOn ? Glow::Clamp01(a_previewFraction) : sp.fraction;
			if (a_previewToggled) {
				h.glow.lastFraction = h.fraction;
			}
			h.verdict.tuning = a_s.tuning;
			h.verdict.mode = Mode::kCharge;
			h.verdict.why = "spell - follows magicka";
			if (!h.active || h.weapon != sp.spell) {
				h.weapon = sp.spell;
				h.glow.Reset(h.fraction, static_cast<std::uint32_t>(SpellKey(h.actor, a_left) * 2654435761u));
				h.actorName = a_actor->GetName();
				h.weaponLabel = Label(sp.spell);
				h.enchLabel = "(spell - follows magicka)";
				if (a_s.debugLog) {
					SKSE::log::info("{} {} hand: spell now tracked | {} - magicka {:.0f}/{:.0f}", h.actorName, a_left ? "left" : "right",
						h.weaponLabel, sp.current, sp.max);
				}
			}
			h.active = true;
			// magicka spent on a cast is not a hit, nor magicka coming back a soul gem: no pulse, no flare
			h.out = Glow::Step(a_s.tuning, h.glow, h.fraction, a_delta, true);
			h.lights = h.roots = h.others = 0;
			for (auto* node : sp.nodes) {
				if (!node) {
					continue;
				}
				++h.roots;
				h.lights += ApplyUnder(node, h, &h.others);
				if (a_s.dimShader) {
					DimArt(node, h.out.brightness);  // the casting art's glow on the hand
				}
			}
			if (sp.casterLight) {
				// the game's casting light: it may hang under the magic node (then this is the same light again, which
				// Glow::Scaled never compounds) or elsewhere
				if (!gLights.contains(sp.casterLight) || gLights[sp.casterLight].frame != gFrame) {
					++h.lights;
				}
				ApplyLight(sp.casterLight, h);
			}
			// the reticle's color (his ask 2026-10-10: "change color with spells"): the spell's own light as it is before any
			// cooling - the casting light, else the brightest light at the hand; none: magicka blue
			RE::NiPointLight* own = sp.casterLight;
			float             best = 0.0f;
			if (!own) {
				for (auto* node : sp.nodes) {
					if (!node) {
						continue;
					}
					RE::BSVisit::TraverseScenegraphLights(node, [&](RE::NiPointLight* a_light) {
						if (a_light) {
							const auto& d = a_light->GetLightRuntimeData();
							const float s = std::fabs(d.fade) * (std::max)({ d.diffuse.red, d.diffuse.green, d.diffuse.blue });
							if (std::isfinite(s) && s > best) {
								best = s;
								own = a_light;
							}
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
			}
			if (own) {
				const auto  it = gLights.find(own);
				const auto& c = it != gLights.end() && it->second.color.touched ? it->second.color.base : own->GetLightRuntimeData().diffuse;
				const float top = (std::max)({ c.red, c.green, c.blue });
				if (top > 0.02f && std::isfinite(top)) {
					h.hudColor = { c.red / top, c.green / top, c.blue / top };
				}
			} else {
				h.hudColor = { 0.32f, 0.58f, 1.0f };
			}
		}

		// what the HUD shows this frame: the player's hands that fade (not one left alone), the switches, and whether her
		// weapons or spells are out (the reticle shows only then). Main thread, under gLock.
		void PublishHud(const Settings& a_s)
		{
			HudState hud;
			hud.gems = a_s.enabled && a_s.hudGems;
			hud.reticle = a_s.enabled && a_s.reticle && !OtherReticleLoaded();  // off beside Reticle Arcs (FadeMain.cpp)
			hud.reticleStyle = a_s.reticleStyle;
			hud.reticleSize = a_s.reticleSize;
			hud.reticleOpacity = a_s.reticleOpacity;
			hud.gemSize = a_s.hudGemSize;
			hud.gemOpacity = a_s.hudGemOpacity;
			hud.gemPercent = a_s.hudGemPercent;
			hud.low = a_s.tuning.sputterBelow;
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const auto* state = player ? player->AsActorState() : nullptr;
			hud.drawn = state && state->IsWeaponDrawn();
			if (a_s.enabled) {
				for (const auto& [key, h] : gHands) {
					if (h.active && h.frame == gFrame && player && h.actorSeen == player) {
						auto& side = hud.hands[h.left ? 0 : 1];
						// a hand holds a spell OR a weapon: a spell hand of last frame's shape must not hide this frame's weapon
						if (side.shown && side.spell && !h.spell) {
							continue;
						}
						side.shown = true;
						side.spell = h.spell;
						side.bound = h.reading.bound;
						side.fraction = Glow::Clamp01(h.fraction);
						if (h.spell) {
							side.color = h.hudColor;
							side.element = h.element;
							side.school = h.school;
							if (const auto* av = h.school >= 0 && h.school < 5 ? player->AsActorValueOwner() : nullptr) {
								static constexpr RE::ActorValue kSkill[]{ RE::ActorValue::kDestruction, RE::ActorValue::kRestoration,
									RE::ActorValue::kConjuration, RE::ActorValue::kAlteration, RE::ActorValue::kIllusion };
								side.skill = static_cast<int>(av->GetActorValue(kSkill[h.school]));
							}
						}
					}
				}
			}
			std::lock_guard lock(gHudLock);
			gHud = hud;
		}

		void LogHand(const HandTrack& a_h, std::string_view a_what, bool a_on)
		{
			if (a_on) {
				SKSE::log::info("{} {} hand: {} | {} | {} - charge {:.0f}/{:.0f} ({:.0f}%) - {}", a_h.actorName, a_h.left ? "left" : "right",
					a_what, Label(a_h.reading.weapon), Label(a_h.reading.ench), a_h.reading.current, a_h.reading.max,
					a_h.fraction * 100.0f, a_h.verdict.why);
			}
		}
	}

	void UpdateHands(float a_delta)
	{
		std::lock_guard lock(gLock);
		++gFrame;
		const Settings s = Config();  // one copy for the frame
		gEnabled = s.enabled;
		gDimShader = s.dimShader;
		if (!s.enabled) {
			RestoreAll();
			PublishHud(s);
			Items::Apply();  // Lights by Item works with Fading off too
			return;
		}
		auto&       preview = PreviewState();
		const bool  pulseNow = preview.pulse.exchange(false);
		const bool  previewOn = preview.on;
		static bool previewWas = false;
		const bool  previewToggled = previewOn != previewWas;
		previewWas = previewOn;
		const bool flareNow = preview.flare.exchange(false);
		for (auto& actor : Actors(s.who)) {
			for (const bool left : { false, true }) {
				auto& h = gHands[Key(actor->GetHandle(), left)];
				h.actor = actor->GetHandle();
				h.key = Key(h.actor, left);
				h.actorSeen = actor.get();
				h.left = left;
				h.frame = gFrame;
				h.reading = ReadHand(actor.get(), left);
				if (!h.reading.tracked) {
					h.active = false;
					h.weapon = nullptr;
					h.effectRoots.clear();  // nothing of ours here: let go of the old effects' 3D
					continue;
				}
				// the verdict changes only with the weapon, the settings or the rules: worked out again only then
				if (!h.verdictKnown || h.verdictWeapon != h.reading.weapon || h.verdictEnch != h.reading.ench ||
					h.verdictEnchID != (h.reading.ench ? h.reading.ench->GetFormID() : 0) || h.verdictRules != RulesGeneration() ||
					!(h.verdictSettings == s)) {
					h.verdict = Judge(h.reading.weapon, h.reading.ench, s);
					h.verdictKnown = true;
					h.verdictWeapon = h.reading.weapon;
					h.verdictEnch = h.reading.ench;
					h.verdictEnchID = h.reading.ench ? h.reading.ench->GetFormID() : 0;
					h.verdictRules = RulesGeneration();
					h.verdictSettings = s;
					h.weaponLabel = Label(h.reading.weapon);
					h.enchLabel = h.reading.ench ? Label(h.reading.ench) : std::string(h.reading.bound ? "(bound weapon)" : "(none)");
					h.fire = h.reading.ench && KindOf(h.reading.ench).element == Element::kFire;
				}
				if (auto* av = actor->AsActorValueOwner()) {
					h.chargeAV = av->GetActorValue(left ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge);
				}
				if (h.verdict.mode == Mode::kExempt) {
					if (h.active || h.weapon != h.reading.weapon) {
						h.actorName = actor->GetName();
						LogHand(h, "left alone", s.debugLog);
					}
					h.active = false;
					h.weapon = h.reading.weapon;
					h.effectRoots.clear();
					continue;
				}
				const Glow::Follow follow = Glow::FollowOf(h.reading.bound, h.verdict.mode == Mode::kBound, h.reading.current,
					h.reading.max, h.verdict.boundFadeSeconds, h.reading.fraction);
				h.fraction = follow.fraction;
				if (previewOn) {  // before a new weapon's reset, so the preview's charge is not taken as a hit or a refill
					h.fraction = Glow::Clamp01(preview.fraction);
				}
				if (previewToggled) {
					h.glow.lastFraction = h.fraction;  // the preview going on or off is not a hit or a refill either
				}
				if (!h.active || h.weapon != h.reading.weapon || h.instance != h.reading.instance) {
					h.weapon = h.reading.weapon;
					h.instance = h.reading.instance;
					h.glow.Reset(h.fraction, static_cast<std::uint32_t>(Key(h.actor, left) * 2654435761u));
					h.unlitFor = 0.0f;
					h.actorName = actor->GetName();
					LogHand(h, "now tracked", s.debugLog);
					if (s.debugLog) {
						LogActorLights(actor.get());
					}
				}
				if (pulseNow) {
					h.glow.TriggerPulse(h.verdict.tuning);
				}
				if (flareNow) {
					h.glow.TriggerFlare();
				}
				h.active = true;
				// a spell's time left is not a charge; nor is the preview's slider, which moves the charge by hand (its
				// buttons pulse and flare on purpose, above)
				h.out = Glow::Step(h.verdict.tuning, h.glow, h.fraction, a_delta, follow.timed || previewOn);
				if ((h.out.pulsed || h.out.flared) && s.debugLog) {
					LogHand(h, h.out.pulsed ? "spent charge (pulse)" : "recharged (flare)", true);
				}
				ApplyHand(actor.get(), h, s, a_delta);
			}
		}
		// the spells in hand: their lights and glow follow the caster's magicka
		if (s.spells) {
			for (auto& actor : gActors) {
				for (const bool left : { false, true }) {
					UpdateSpellHand(actor.get(), left, s, a_delta, previewOn, previewToggled, preview.fraction);
				}
			}
		}
		// a hand not read this frame (a follower dismissed, an actor unloaded) is no longer active: the effect hooks must not
		// keep putting last frame's numbers on its lights
		for (auto& [key, hand] : gHands) {
			if (hand.frame != gFrame) {
				hand.active = false;
				hand.effectRoots.clear();  // its effects' 3D is not ours to keep
			}
		}
		SweepOwnLights();  // a hand that did not ask for its light this frame (put away, exempt, gone) lets it go
		Sweep();
		gActiveHands = static_cast<std::size_t>(std::ranges::count_if(gHands, [](const auto& kv) { return kv.second.active; }));
		PublishHud(s);
		Items::Apply();  // last: an item's own choice wins over the fading (his order 2026-10-10, FadeItems.cpp)
	}

	void AfterReferenceEffect(RE::ReferenceEffect* a_effect, bool a_own3D)
	{
		if (gActiveHands.load(std::memory_order_relaxed) == 0) {
			return;
		}
		RE::Actor*      actor = nullptr;
		bool            left = false;
		RE::NiAVObject* root = nullptr;
		if (!HandOfEffect(a_effect, actor, left, root)) {
			return;
		}
		std::lock_guard lock(gLock);
		if (!gEnabled) {
			return;
		}
		auto* found = ActiveHand(actor, left);
		if (!found) {
			return;
		}
		auto& hand = *found;
		// the root the effect hangs on (Light Placer hangs its lights there), and an art effect's own model. Not a shader
		// effect's own 3D: that is the whole actor it glows over, torch and armour lights and all
		std::array<RE::NiAVObject*, 2> nodes{ root, a_own3D ? a_effect->Get3D() : nullptr };
		for (auto* node : nodes) {
			if (!node) {
				continue;
			}
			auto kept = std::ranges::find_if(hand.effectRoots, [node](const Root& r) { return r.node.get() == node; });
			if (kept == hand.effectRoots.end()) {
				hand.effectRoots.push_back({ RE::NiPointer<RE::NiAVObject>(node), gFrame });
			} else {
				kept->frame = gFrame;
			}
			ApplyUnder(node, hand);  // right after Light Placer wrote this frame's values for this effect
		}
		if (nodes[1] && gDimShader.load(std::memory_order_relaxed)) {
			DimArt(nodes[1], hand.out.brightness);  // the art's own glowing meshes: VAER's swirls, EAE's art
		}
	}

	void AfterShaderEffect(RE::ShaderReferenceEffect* a_effect)
	{
		// the cheap test first: every effect shader in the loaded world comes through here every frame
		if (gActiveHands.load(std::memory_order_relaxed) == 0 || !gDimShader.load(std::memory_order_relaxed)) {
			return;
		}
		RE::Actor*      actor = nullptr;
		bool            left = false;
		RE::NiAVObject* root = nullptr;
		if (!a_effect || !a_effect->effectShaderData || !HandOfEffect(a_effect, actor, left, root)) {
			return;
		}
		std::lock_guard lock(gLock);
		const auto*     hand = ActiveHand(actor, left);
		if (!hand) {
			return;
		}
		auto [it, added] = gShaders.try_emplace(a_effect);
		auto& seen = it->second;
		if (added) {
			seen.effect.reset(a_effect);
		}
		seen.frame = gFrame;
		// a shader's alpha tops out at 1: a pulse or flare can brighten a dimmed glow back up, not past its own
		const float k = (std::min)(hand->out.brightness, 1.0f);
		auto*       data = a_effect->effectShaderData;
		data->fillColor.alpha = std::clamp(seen.fill.Apply(data->fillColor.alpha, k), 0.0f, 1.0f);
		data->rimColor.alpha = std::clamp(seen.rim.Apply(data->rimColor.alpha, k), 0.0f, 1.0f);
	}

	void ReapplyAll()
	{
		std::lock_guard lock(gLock);
		if (gEnabled) {
			for (auto& [light, seen] : gLights) {
				if (!seen.light || gFrame - seen.frame > 1) {
					continue;
				}
				const auto it = gHands.find(seen.hand);
				if (it != gHands.end() && it->second.active) {
					ApplyLight(light, it->second, false);
				}
			}
		}
		Items::Reapply();  // an item's own choice still wins after the lights were written again (fading on or off)
	}

	void NoteItemWrite(RE::NiPointLight* a_light, float a_fadeBefore, float a_fadeAfter, const RE::NiColor& a_colorBefore,
		const RE::NiColor& a_colorAfter)
	{
		Plugin::NoteFadeWrite(a_light, a_fadeBefore, a_fadeAfter);
		const auto it = gLights.find(a_light);
		if (it == gLights.end()) {
			return;
		}
		auto& seen = it->second;
		if (seen.fade.touched && seen.fade.written == a_fadeBefore) {
			seen.fade.written = a_fadeAfter;  // its base stays the light's own: the next pass scales that, not the item's write
			seen.shownFade = a_fadeAfter;
		}
		if (seen.color.touched && ColorKeep::Same(seen.color.written, a_colorBefore)) {
			seen.color.written = a_colorAfter;
		}
	}

	void Rebase(RE::NiPointLight* a_light, float a_ratio, float a_now)
	{
		std::lock_guard lock(gLock);
		const auto      it = gLights.find(a_light);
		if (it == gLights.end() || !it->second.fade.touched) {
			return;
		}
		auto& seen = it->second;
		seen.fade.base *= a_ratio;  // the Brightness slider moved the light's own value, not ours
		seen.fade.written = a_now;
		seen.shownFade = a_now;
	}

	void ReleaseAll()
	{
		std::lock_guard lock(gLock);
		RestoreAll();
		Items::Release();
		gActors.clear();
	}

	std::vector<HandView> Snapshot()
	{
		std::vector<HandView> out;
		std::lock_guard       lock(gLock);
		for (const auto& [key, h] : gHands) {
			if (!h.reading.tracked || gFrame - h.frame > kKeepFrames) {
				continue;
			}
			HandView v;
			v.actor = h.actorName;
			v.player = h.actorSeen && h.actorSeen == RE::PlayerCharacter::GetSingleton();  // a pointer compare, nothing read
			v.left = h.left;
			v.weapon = h.weaponLabel;
			v.enchantment = h.enchLabel;
			v.why = h.verdict.why;
			v.bound = h.reading.bound;
			v.spell = h.spell;
			v.exempt = !h.active;
			v.fraction = h.fraction;
			v.current = h.reading.current;
			v.max = h.reading.max;
			v.brightness = h.active ? h.out.brightness : 1.0f;
			v.reach = h.active ? h.out.reach : 1.0f;
			v.cool = h.active ? h.out.cool : 0.0f;
			v.lights = h.lights;
			v.roots = h.roots;
			v.chargeAV = h.chargeAV;  // nothing of the game is touched here: this runs on the menu's and DevBench's threads
			out.push_back(std::move(v));
		}
		std::ranges::sort(out, {}, [](const HandView& v) { return std::make_pair(v.actor, v.left); });
		return out;
	}

	HudState Hud()
	{
		std::lock_guard lock(gHudLock);
		return gHud;
	}

	std::vector<LightNow> LightsNow()
	{
		std::vector<LightNow> out;
		std::lock_guard       lock(gLock);
		out.reserve(gLights.size());
		for (const auto& [light, seen] : gLights) {
			if (seen.light) {
				out.push_back({ seen.shownFade, seen.fade.base, seen.shownRadius, seen.fade.frozen });
			}
		}
		return out;
	}

	std::size_t DimmedGlowCount()
	{
		std::lock_guard lock(gLock);
		return gShaders.size() + gArt.size();
	}

	std::vector<std::pair<float, float>> ArtNow()
	{
		std::vector<std::pair<float, float>> out;
		std::lock_guard                      lock(gLock);
		for (const auto& [material, seen] : gArt) {
			out.emplace_back(seen.scale.base, seen.scale.Written() ? seen.scale.written : seen.scale.base);
		}
		return out;
	}

	std::size_t ScaledLightCount()
	{
		std::lock_guard lock(gLock);
		return gLights.size();
	}

	std::size_t FrozenLightCount()
	{
		std::lock_guard lock(gLock);
		return static_cast<std::size_t>(std::ranges::count_if(gLights, [](const auto& kv) { return kv.second.fade.frozen; }));
	}

	bool Query(RE::Actor* a_actor, bool a_left, float& a_fraction, float& a_brightness)
	{
		std::lock_guard lock(gLock);
		const auto*     hand = ActiveHand(a_actor, a_left);
		if (!hand) {
			return false;
		}
		a_fraction = hand->fraction;
		a_brightness = hand->out.brightness;
		return true;
	}
}
