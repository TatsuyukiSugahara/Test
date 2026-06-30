#include "aq.h"
#include "AudioBank.h"
#include "Util/SimpleJson.h"
#include "Util/CRC32.h"
#include "Resource/Resource.h"
#include "Sound/SoundClip.h"


namespace aq
{
	namespace audio
	{
		using util::JsonValue;
		using util::JsonParser;

		namespace
		{
			NameId Hash(const std::string& name)
			{
				return static_cast<NameId>(aq::util::ComputeCrc32(name.c_str()));
			}

			sound::SoundBusId BusFromName(const std::string& s)
			{
				if (s == "Master") return sound::SoundBusId::Master;
				if (s == "BGM")    return sound::SoundBusId::BGM;
				if (s == "Voice")  return sound::SoundBusId::Voice;
				return sound::SoundBusId::SE;   // 既定
			}

			LoadPolicy LoadPolicyFromName(const std::string& s)
			{
				if (s == "preload") return LoadPolicy::Preload;
				if (s == "stream")  return LoadPolicy::Stream;
				return LoadPolicy::Auto;
			}

			VoiceStealing StealingFromName(const std::string& s)
			{
				if (s == "none" || s == "reject") return VoiceStealing::Reject;
				if (s == "quietest")              return VoiceStealing::Quietest;
				if (s == "lowestPriority")        return VoiceStealing::LowestPriority;
				return VoiceStealing::Oldest;   // 既定
			}

			SpatialMode SpatialFromName(const std::string& s)
			{
				if (s == "2d") return SpatialMode::Is2D;
				if (s == "3d") return SpatialMode::Is3D;
				return SpatialMode::Auto;
			}

			sound::AttenuationModel AttenModelFromName(const std::string& s)
			{
				if (s == "None")        return sound::AttenuationModel::None;
				if (s == "Linear")      return sound::AttenuationModel::Linear;
				if (s == "Exponential") return sound::AttenuationModel::Exponential;
				return sound::AttenuationModel::Inverse;   // 既定
			}

			RtpcProperty RtpcPropertyFromName(const std::string& s)
			{
				if (s == "pitch") return RtpcProperty::Pitch;
				return RtpcProperty::VolumeDb;   // 既定
			}
			CurveInterp CurveInterpFromName(const std::string& s)
			{
				if (s == "ease") return CurveInterp::Ease;
				return CurveInterp::Linear;
			}
		}


		NameId AudioBank::ParseChild(const std::string& parentName, int index, const JsonValue& child,
		                             NameId inheritedKind, const std::string& basePath)
		{
			if (!child.IsObject()) {
				EnginePrintf("[Audio] %s の子[%d] は object である必要があります（{\"clip\"}/{\"ref\"}/インライン）。\n",
				             parentName.c_str(), index);
				return 0;
			}
			if (child.Contains("ref")) {
				return Hash(child["ref"].AsString());   // 既存オブジェクト参照（生成しない）
			}
			// clip 短縮 or インライン定義は無名オブジェクトとして生成する。
			const std::string anonName = parentName + "#" + std::to_string(index);
			return ParseObjectNode(anonName, child, inheritedKind, basePath);
		}


		ActionDef AudioBank::ParseAction(const JsonValue& action)
		{
			ActionDef a;
			const std::string act = action["action"].AsString();
			a.type = (act == "Play") ? ActionType::Play
			       : (act == "Stop") ? ActionType::Stop
			       : ActionType::Unknown;

			const JsonValue& tgt = action["target"];
			if (tgt.IsObject()) {
				if (tgt.Contains("object")) { a.targetType = TargetType::Object; a.targetId  = Hash(tgt["object"].AsString()); }
				else if (tgt.Contains("kind")) { a.targetType = TargetType::Kind; a.targetId  = Hash(tgt["kind"].AsString()); }
				else if (tgt.Contains("bus"))  { a.targetType = TargetType::Bus;  a.targetBus = BusFromName(tgt["bus"].AsString()); }
			}
			a.fadeMs = action["fadeMs"].AsFloat();
			return a;
		}


		NameId AudioBank::ParseObjectNode(const std::string& name, const JsonValue& def,
		                                  NameId inheritedKind, const std::string& basePath)
		{
			SoundObjectDef o;
			o.nameId   = Intern(name);
			o.kindId   = def.Contains("kind") ? Hash(def["kind"].AsString()) : inheritedKind;
			o.volumeDb = def["volumeDb"].AsFloat();
			o.pitch    = def["pitch"].AsFloat(1.0f);
			o.loop     = def["loop"].AsBool();

			if (def["volumeRandomDb"].IsArray() && def["volumeRandomDb"].Size() >= 2) {
				o.volumeRandomDb[0] = def["volumeRandomDb"][size_t(0)].AsFloat();
				o.volumeRandomDb[1] = def["volumeRandomDb"][size_t(1)].AsFloat();
			}
			if (def["pitchRandom"].IsArray() && def["pitchRandom"].Size() >= 2) {
				o.pitchRandom[0] = def["pitchRandom"][size_t(0)].AsFloat(1.0f);
				o.pitchRandom[1] = def["pitchRandom"][size_t(1)].AsFloat(1.0f);
			}

			const std::string type = def["type"].AsString();
			if (type == "Random" || type == "Sequence") {
				o.type        = (type == "Random") ? ObjectType::Random : ObjectType::Sequence;
				o.avoidRepeat = static_cast<uint32_t>(def["avoidRepeat"].AsInt());
				const JsonValue& children = def["children"];
				if (children.IsArray()) {
					for (size_t i = 0; i < children.Size(); ++i) {
						const NameId childId = ParseChild(name, static_cast<int>(i), children[i], o.kindId, basePath);
						if (childId != 0) { o.childIds.push_back(childId); }
					}
				}
				const JsonValue& weights = def["weights"];
				if (weights.IsArray()) {
					for (size_t i = 0; i < weights.Size(); ++i) {
						o.weights.push_back(weights[i].AsFloat(1.0f));
					}
				}
			}
			else if (type == "Switch") {
				o.type          = ObjectType::Switch;
				o.switchGroupId = Hash(def["switchGroup"].AsString());
				o.defaultValue  = def.Contains("default") ? Hash(def["default"].AsString()) : 0;
				const JsonValue& map = def["map"];
				if (map.IsObject()) {
					int index = 0;
					for (const auto& entry : map.GetObject()) {
						const NameId valueId = Hash(entry.first);
						const NameId childId = ParseChild(name, index++, entry.second, o.kindId, basePath);
						if (childId != 0) { o.switchMap[valueId] = childId; }
					}
				}
			}
			else if (type == "Blend") {
				o.type = ObjectType::Blend;
				const JsonValue& layers = def["layers"];
				if (layers.IsArray()) {
					for (size_t i = 0; i < layers.Size(); ++i) {
						const JsonValue& L = layers[i];
						BlendLayer layer;
						layer.childId = ParseChild(name, static_cast<int>(i), L["child"], o.kindId, basePath);
						if (L.Contains("rtpc")) { layer.rtpcId = Hash(L["rtpc"].AsString()); }
						const JsonValue& curve = L["curve"];
						if (curve.IsArray()) {
							for (size_t ci = 0; ci < curve.Size(); ++ci) {
								const JsonValue& pt = curve[ci];
								if (pt.IsArray() && pt.Size() >= 2) {
									layer.curve.push_back({ pt[size_t(0)].AsFloat(), pt[size_t(1)].AsFloat() });
								}
							}
						}
						std::sort(layer.curve.begin(), layer.curve.end(),
						          [](const CurvePoint& a, const CurvePoint& c) { return a.x < c.x; });
						if (layer.childId != 0) { o.blendLayers.push_back(std::move(layer)); }
					}
				}
			}
			else {
				// 既定は Sound（type 省略 + clip あり も含む）。
				o.type = ObjectType::Sound;
				o.clip = basePath + def["clip"].AsString();
			}

			objects_[o.nameId] = std::move(o);
			return Hash(name);
		}


		NameId AudioBank::Intern(const std::string& name)
		{
			const NameId id = Hash(name);
			auto it = names_.find(id);
			if (it != names_.end() && it->second != name) {
				EnginePrintf("[Audio] CRC32 衝突: \"%s\" と \"%s\" が同一ハッシュ。名前を変更してください。\n",
				             name.c_str(), it->second.c_str());
			}
			else {
				names_[id] = name;
			}
			return id;
		}


		bool AudioBank::LoadFromFile(const char* path)
		{
			JsonValue root = JsonParser::ParseFile(path);
			if (!root.IsObject()) {
				EnginePrintf("[Audio] Bank をロードできません: %s\n", path ? path : "(null)");
				return false;
			}

			bankId_ = root["bankId"].AsString();
			const std::string basePath = root["basePath"].AsString();

			// ── kinds ──
			const JsonValue& kinds = root["kinds"];
			if (kinds.IsObject()) {
				for (const auto& pair : kinds.GetObject()) {
					const JsonValue& def = pair.second;
					KindDef k;
					k.nameId     = Intern(pair.first);
					k.bus        = BusFromName(def["bus"].AsString());
					k.loadPolicy = LoadPolicyFromName(def["loadPolicy"].AsString());
					k.cooldownMs = def["cooldownMs"].AsFloat();
					k.fadeInMs   = def["fadeInMs"].AsFloat();
					k.fadeOutMs  = def["fadeOutMs"].AsFloat();
					k.volumeDb      = def["volumeDb"].AsFloat();
					k.pitch         = def["pitch"].AsFloat(1.0f);
					k.maxVoices     = static_cast<uint32_t>(def["maxVoices"].AsInt());
					k.voiceStealing = StealingFromName(def["voiceStealing"].AsString());
					k.spatialMode   = SpatialFromName(def["spatialMode"].AsString());
					k.attenuationId = def.Contains("attenuation") ? Hash(def["attenuation"].AsString()) : 0;
					kinds_[k.nameId] = k;
				}
			}

			// ── attenuations ──
			const JsonValue& attens = root["attenuations"];
			if (attens.IsObject()) {
				for (const auto& pair : attens.GetObject()) {
					const JsonValue& def = pair.second;
					AttenuationDef a;
					a.nameId      = Intern(pair.first);
					a.model       = AttenModelFromName(def["model"].AsString());
					a.minDistance = def["minDistance"].AsFloat(1.0f);
					a.maxDistance = def["maxDistance"].AsFloat(1000.0f);
					attenuations_[a.nameId] = a;
				}
			}

			// ── objects（Sound / Random / Sequence。再帰パース）──
			const JsonValue& objs = root["objects"];
			if (objs.IsObject()) {
				for (const auto& pair : objs.GetObject()) {
					ParseObjectNode(pair.first, pair.second, /*inheritedKind*/ 0, basePath);
				}
			}

			// ── events ──
			const JsonValue& evs = root["events"];
			if (evs.IsObject()) {
				for (const auto& pair : evs.GetObject()) {
					EventDef e;
					e.nameId = Intern(pair.first);
					const JsonValue& arr = pair.second;
					if (arr.IsArray()) {
						for (const JsonValue& action : arr.GetArray()) {
							e.actions.push_back(ParseAction(action));
						}
					}
					events_[e.nameId] = e;
				}
			}

			// ── rtpc 定義 ──
			const JsonValue& rtpcs = root["rtpc"];
			if (rtpcs.IsObject()) {
				for (const auto& pair : rtpcs.GetObject()) {
					const JsonValue& def = pair.second;
					RtpcDef r;
					r.nameId       = Intern(pair.first);
					r.minValue     = def["min"].AsFloat(0.0f);
					r.maxValue     = def["max"].AsFloat(1.0f);
					r.defaultValue = def["default"].AsFloat(0.0f);
					rtpc_[r.nameId] = r;
				}
			}

			// ── rtpcBindings（RTPC → プロパティの連続変調）──
			const JsonValue& bindings = root["rtpcBindings"];
			if (bindings.IsArray()) {
				for (size_t bi = 0; bi < bindings.Size(); ++bi) {
					const JsonValue& b = bindings[bi];
					RtpcBindingDef bind;
					bind.rtpcId   = Hash(b["rtpc"].AsString());
					bind.property = RtpcPropertyFromName(b["property"].AsString());
					bind.interp   = CurveInterpFromName(b["interp"].AsString());

					const JsonValue& tgt = b["target"];
					if (tgt.IsObject()) {
						if (tgt.Contains("object")) { bind.targetType = TargetType::Object; bind.targetId = Hash(tgt["object"].AsString()); }
						else if (tgt.Contains("kind")) { bind.targetType = TargetType::Kind; bind.targetId = Hash(tgt["kind"].AsString()); }
						else if (tgt.Contains("bus"))  { bind.targetType = TargetType::Bus;  bind.targetBus = BusFromName(tgt["bus"].AsString()); }
					}

					const JsonValue& curve = b["curve"];
					if (curve.IsArray()) {
						for (size_t ci = 0; ci < curve.Size(); ++ci) {
							const JsonValue& pt = curve[ci];
							if (pt.IsArray() && pt.Size() >= 2) {
								bind.curve.push_back({ pt[size_t(0)].AsFloat(), pt[size_t(1)].AsFloat() });
							}
						}
					}
					std::sort(bind.curve.begin(), bind.curve.end(),
					          [](const CurvePoint& a, const CurvePoint& c) { return a.x < c.x; });
					rtpcBindings_.push_back(std::move(bind));
				}
			}

			// ── stateRules（State 変化時の Action 列）──
			const JsonValue& stateRules = root["stateRules"];
			if (stateRules.IsArray()) {
				for (size_t i = 0; i < stateRules.Size(); ++i) {
					const JsonValue& rule = stateRules[i];
					StateRuleDef sr;
					const JsonValue& when = rule["when"];
					sr.groupId = Hash(when["group"].AsString());
					sr.valueId = Hash(when["value"].AsString());
					const JsonValue& acts = rule["actions"];
					if (acts.IsArray()) {
						for (size_t a = 0; a < acts.Size(); ++a) {
							sr.actions.push_back(ParseAction(acts[a]));
						}
					}
					stateRules_.push_back(std::move(sr));
				}
			}

			// ── ducking（自動ダッキング）──
			const JsonValue& ducks = root["ducking"];
			if (ducks.IsObject()) {
				for (const auto& pair : ducks.GetObject()) {
					const JsonValue& def = pair.second;
					DuckingDef d;
					d.nameId = Intern(pair.first);
					const JsonValue& trig = def["trigger"];
					if (trig.IsObject()) {
						if (trig.Contains("kind")) { d.triggerType = TargetType::Kind; d.triggerKind = Hash(trig["kind"].AsString()); }
						else if (trig.Contains("bus")) { d.triggerType = TargetType::Bus; d.triggerBus = BusFromName(trig["bus"].AsString()); }
					}
					const JsonValue& tgt = def["target"];
					if (tgt.IsObject() && tgt.Contains("bus")) {
						d.targetBus = BusFromName(tgt["bus"].AsString());
					}
					d.amountDb  = def["amountDb"].AsFloat(-6.0f);
					d.attackMs  = def["attackMs"].AsFloat(100.0f);
					d.releaseMs = def["releaseMs"].AsFloat(300.0f);
					duckings_.push_back(std::move(d));
				}
			}

			// ── clip の事前ロード（loadPolicy=stream 以外は常駐。3D ループ等で clip 参照が要る）──
			for (auto& pair : objects_) {
				SoundObjectDef& o = pair.second;
				const KindDef*  k = FindKind(o.kindId);
				const bool isStream = (k && k->loadPolicy == LoadPolicy::Stream);
				if (!isStream && !o.clip.empty()) {
					o.cachedClip = aq::res::ResourceManager::Get().Load<sound::SoundClip>(o.clip.c_str());
				}
			}

			EnginePrintf("[Audio] Bank \"%s\" ロード完了: kinds=%zu objects=%zu events=%zu\n",
			             bankId_.c_str(), kinds_.size(), objects_.size(), events_.size());
			return true;
		}


		const KindDef* AudioBank::FindKind(NameId id) const
		{
			auto it = kinds_.find(id);
			return it != kinds_.end() ? &it->second : nullptr;
		}


		const AttenuationDef* AudioBank::FindAttenuation(NameId id) const
		{
			auto it = attenuations_.find(id);
			return it != attenuations_.end() ? &it->second : nullptr;
		}


		const SoundObjectDef* AudioBank::FindObject(NameId id) const
		{
			auto it = objects_.find(id);
			return it != objects_.end() ? &it->second : nullptr;
		}


		const EventDef* AudioBank::FindEvent(NameId id) const
		{
			auto it = events_.find(id);
			return it != events_.end() ? &it->second : nullptr;
		}


		const RtpcDef* AudioBank::FindRtpc(NameId id) const
		{
			auto it = rtpc_.find(id);
			return it != rtpc_.end() ? &it->second : nullptr;
		}


		const std::string& AudioBank::NameOf(NameId id) const
		{
			static const std::string empty;
			auto it = names_.find(id);
			return it != names_.end() ? it->second : empty;
		}
	}
}
