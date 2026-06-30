#include "aq.h"
#include "AudioDirector.h"
#include "Sound/SoundEngine.h"
#include "Sound/SoundStream.h"
#include "Sound/SoundClip.h"
#include <cmath>


namespace aq
{
	namespace audio
	{
		AudioDirector* AudioDirector::instance_ = nullptr;


		AudioDirector::~AudioDirector() = default;


		namespace
		{
			float DbToLinear(float db)
			{
				return std::pow(10.0f, db / 20.0f);
			}

			// カーブ評価（x 昇順の点列を線形/ease 補間）。
			float EvalCurve(const std::vector<CurvePoint>& curve, CurveInterp interp, float x)
			{
				if (curve.empty()) { return 0.0f; }
				if (x <= curve.front().x) { return curve.front().y; }
				if (x >= curve.back().x)  { return curve.back().y; }
				for (size_t i = 1; i < curve.size(); ++i) {
					if (x <= curve[i].x) {
						const CurvePoint& p0 = curve[i - 1];
						const CurvePoint& p1 = curve[i];
						float t = (p1.x != p0.x) ? (x - p0.x) / (p1.x - p0.x) : 0.0f;
						if (interp == CurveInterp::Ease) { t = t * t * (3.0f - 2.0f * t); }
						return p0.y + (p1.y - p0.y) * t;
					}
				}
				return curve.back().y;
			}
		}


		bool AudioDirector::Initialize()
		{
			return true;
		}


		void AudioDirector::Finalize()
		{
			// インスタンス（SoundStream を含む）を SoundEngine 解放前に破棄する。
			instances_.clear();
			banks_.clear();
			kindCooldownUntil_.clear();
		}


		void AudioDirector::Update(float deltaTime)
		{
			time_ += deltaTime;
			if (!sound::SoundEngine::IsAvailable()) {
				return;
			}
			sound::SoundEngine& se = sound::SoundEngine::Get();

			// 3D インスタンスの位置追従 + 終了したインスタンスの回収。
			for (PlayingInstance& inst : instances_) {
				if (inst.id == 0) {
					continue;
				}
				bool finished = false;
				if (inst.is3D) {
					sound::SoundSource* src = se.Resolve(inst.sourceHandle);
					if (src) {
						auto it = gameObjects_.find(inst.gameObject);
						if (it != gameObjects_.end()) {
							src->SetPosition(it->second.position);
							src->SetVelocity(it->second.velocity);
						}
					}
					finished = (src == nullptr) || !src->IsPlaying();
				}
				else if (inst.isStream) {
					finished = !inst.stream || !inst.stream->IsPlaying();
				}
				else {
					finished = !se.IsPlaying(inst.handle);
				}

				if (finished) {
					RecycleInstance(inst);
				}
			}

			// インスタンス音量/ピッチの調停（base × rtpc × fadeGate）。
			ApplyInstanceVolumes(deltaTime);
			// 自動ダッキングを更新する。
			UpdateDucking(deltaTime);
		}


		void AudioDirector::RecycleInstance(PlayingInstance& inst)
		{
			if (inst.is3D && inst.sourceHandle.IsValid() && sound::SoundEngine::IsAvailable()) {
				sound::SoundEngine::Get().DestroySource(inst.sourceHandle);
			}
			inst.id           = 0;
			inst.isStream     = false;
			inst.is3D         = false;
			inst.gameObject   = 0;
			inst.stream.reset();
			inst.handle       = sound::SoundHandle{};
			inst.sourceHandle = sound::SoundSourceHandle{};
		}


		void AudioDirector::LoadBank(const char* path)
		{
			AudioBank bank;
			if (bank.LoadFromFile(path)) {
				banks_.push_back(std::move(bank));
			}
		}


		const KindDef* AudioDirector::FindKind(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				if (const KindDef* k = b.FindKind(id)) { return k; }
			}
			return nullptr;
		}


		const AttenuationDef* AudioDirector::FindAttenuation(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				if (const AttenuationDef* a = b.FindAttenuation(id)) { return a; }
			}
			return nullptr;
		}


		const SoundObjectDef* AudioDirector::FindObject(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				if (const SoundObjectDef* o = b.FindObject(id)) { return o; }
			}
			return nullptr;
		}


		void AudioDirector::RegisterGameObject(uint64_t go)
		{
			if (go != 0) { gameObjects_[go]; }
		}


		void AudioDirector::UnregisterGameObject(uint64_t go)
		{
			gameObjects_.erase(go);
			// この GameObject に紐づく 3D インスタンスを停止する（stopOnGameObjectDestroy 既定）。
			for (PlayingInstance& inst : instances_) {
				if (inst.id != 0 && inst.is3D && inst.gameObject == go) {
					StopInstance(inst, 0.0f);
				}
			}
		}


		void AudioDirector::SetGameObjectTransform(uint64_t go, const math::Vector3& pos, const math::Vector3& forward,
		                                           const math::Vector3& up, const math::Vector3& velocity)
		{
			if (go == 0) { return; }
			GameObjectState& s = gameObjects_[go];
			s.position = pos;
			s.forward  = forward;
			s.up       = up;
			s.velocity = velocity;
		}


		void AudioDirector::SetListener(const math::Vector3& pos, const math::Vector3& forward,
		                                const math::Vector3& up, const math::Vector3& velocity)
		{
			if (sound::SoundEngine::IsAvailable()) {
				sound::SoundListener& l = sound::SoundEngine::Get().GetListener();
				l.SetPosition(pos);
				l.SetOrientation(forward, up);
				l.SetVelocity(velocity);
			}
		}


		const EventDef* AudioDirector::FindEvent(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				if (const EventDef* e = b.FindEvent(id)) { return e; }
			}
			return nullptr;
		}


		PlayingId AudioDirector::PostEvent(NameId eventId, uint64_t gameObject)
		{
			if (!sound::SoundEngine::IsAvailable()) {
				return 0;
			}
			const EventDef* e = FindEvent(eventId);
			if (e == nullptr) {
				EnginePrintf("[Audio] PostEvent: 未知のイベント id=%u\n", eventId);
				return 0;
			}

			PlayingId last = 0;
			for (const ActionDef& action : e->actions) {
				const PlayingId id = ExecuteAction(action, gameObject);
				if (id != 0) { last = id; }
			}
			return last;
		}


		PlayingId AudioDirector::ExecuteAction(const ActionDef& action, uint64_t gameObject)
		{
			switch (action.type) {
			case ActionType::Play:
				if (action.targetType != TargetType::Object) {
					EnginePrintf("[Audio] Play: target は object である必要があります。\n");
					return 0;
				}
				return PlayTopObject(action.targetId, gameObject, action);
			case ActionType::Stop:
				StopByTarget(action);
				return 0;
			default:
				return 0;
			}
		}


		void AudioDirector::SetState(NameId groupId, NameId valueId)
		{
			currentStates_[groupId] = valueId;
			for (const AudioBank& bank : banks_) {
				for (const StateRuleDef& rule : bank.GetStateRules()) {
					if (rule.groupId == groupId && rule.valueId == valueId) {
						for (const ActionDef& action : rule.actions) {
							ExecuteAction(action, 0);
						}
					}
				}
			}
		}


		bool AudioDirector::IsDuckTriggerActive(const DuckingDef& def) const
		{
			for (const PlayingInstance& inst : instances_) {
				if (inst.id == 0) { continue; }
				if (def.triggerType == TargetType::Bus && inst.bus == def.triggerBus) { return true; }
				if (def.triggerType == TargetType::Kind && inst.kindId == def.triggerKind) { return true; }
			}
			return false;
		}


		void AudioDirector::UpdateDucking(float deltaTime)
		{
			if (!sound::SoundEngine::IsAvailable()) {
				return;
			}

			// 各ダッキングの減衰量を target/attack/release でランプし、バス別に最も深い値を採る。
			std::unordered_map<int, float> busMinDb;
			for (const AudioBank& bank : banks_) {
				for (const DuckingDef& d : bank.GetDuckings()) {
					const bool  active   = IsDuckTriggerActive(d);
					const float targetDb = active ? d.amountDb : 0.0f;

					float cur = duckCurrentDb_.count(d.nameId) ? duckCurrentDb_[d.nameId] : 0.0f;
					const float ms   = (targetDb < cur) ? d.attackMs : d.releaseMs;   // 深くなる=attack
					const float full = (d.amountDb != 0.0f) ? -d.amountDb : 1.0f;      // 正の振幅
					const float step = (ms > 0.0f) ? (full / (ms / 1000.0f)) * deltaTime : full;
					if (cur < targetDb)      { cur += step; if (cur > targetDb) cur = targetDb; }
					else if (cur > targetDb) { cur -= step; if (cur < targetDb) cur = targetDb; }
					duckCurrentDb_[d.nameId] = cur;

					const int bi = static_cast<int>(d.targetBus);
					auto it = busMinDb.find(bi);
					if (it == busMinDb.end() || cur < it->second) { busMinDb[bi] = cur; }
				}
			}

			sound::SoundEngine& se = sound::SoundEngine::Get();
			for (const auto& kv : busMinDb) {
				se.SetBusDuck(static_cast<sound::SoundBusId>(kv.first), DbToLinear(kv.second));
			}
		}


		float AudioDirector::RandRange(float a, float b)
		{
			if (a == b) { return a; }
			std::uniform_real_distribution<float> dist(a < b ? a : b, a < b ? b : a);
			return dist(rng_);
		}


		NameId AudioDirector::PickRandomChild(const SoundObjectDef& container)
		{
			const size_t n = container.childIds.size();
			if (n == 0) { return 0; }
			if (n == 1) { return container.childIds[0]; }

			const uint32_t last = randomLast_.count(container.nameId) ? randomLast_[container.nameId] : ~0u;

			// 重み合計（avoidRepeat で直近を除外）。
			float total = 0.0f;
			for (size_t i = 0; i < n; ++i) {
				if (container.avoidRepeat > 0 && i == last) { continue; }
				total += (i < container.weights.size()) ? container.weights[i] : 1.0f;
			}
			if (total <= 0.0f) { total = 1.0f; }

			std::uniform_real_distribution<float> dist(0.0f, total);
			float r = dist(rng_);
			size_t chosen = 0;
			for (size_t i = 0; i < n; ++i) {
				if (container.avoidRepeat > 0 && i == last) { continue; }
				const float w = (i < container.weights.size()) ? container.weights[i] : 1.0f;
				if (r < w) { chosen = i; break; }
				r -= w;
				chosen = i;
			}
			randomLast_[container.nameId] = static_cast<uint32_t>(chosen);
			return container.childIds[chosen];
		}


		NameId AudioDirector::NextSequenceChild(const SoundObjectDef& container)
		{
			const size_t n = container.childIds.size();
			if (n == 0) { return 0; }
			uint32_t idx = seqNext_.count(container.nameId) ? seqNext_[container.nameId] : 0u;
			if (idx >= n) {
				if (!container.loop) { return 0; }   // 非ループは末尾で終了
				idx = 0;
			}
			seqNext_[container.nameId] = idx + 1;
			return container.childIds[idx];
		}


		NameId AudioDirector::GetSwitch(NameId groupId, uint64_t gameObject) const
		{
			// per-GameObject → グローバル(0) の順にフォールバック。
			if (gameObject != 0) {
				auto goIt = switchByGo_.find(gameObject);
				if (goIt != switchByGo_.end()) {
					auto vIt = goIt->second.find(groupId);
					if (vIt != goIt->second.end()) { return vIt->second; }
				}
			}
			auto globalIt = switchByGo_.find(0);
			if (globalIt != switchByGo_.end()) {
				auto vIt = globalIt->second.find(groupId);
				if (vIt != globalIt->second.end()) { return vIt->second; }
			}
			return 0;
		}


		void AudioDirector::SetSwitch(NameId groupId, NameId valueId, uint64_t gameObject)
		{
			switchByGo_[gameObject][groupId] = valueId;
		}


		const RtpcDef* AudioDirector::FindRtpc(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				if (const RtpcDef* r = b.FindRtpc(id)) { return r; }
			}
			return nullptr;
		}


		void AudioDirector::SetRTPC(NameId rtpcId, float value, uint64_t gameObject)
		{
			if (const RtpcDef* def = FindRtpc(rtpcId)) {
				if (value < def->minValue) { value = def->minValue; }
				if (value > def->maxValue) { value = def->maxValue; }
			}
			if (gameObject == 0) { rtpcGlobal_[rtpcId] = value; }
			else                 { rtpcByGo_[gameObject][rtpcId] = value; }
		}


		float AudioDirector::GetRtpc(NameId rtpcId, uint64_t gameObject) const
		{
			if (gameObject != 0) {
				auto goIt = rtpcByGo_.find(gameObject);
				if (goIt != rtpcByGo_.end()) {
					auto vIt = goIt->second.find(rtpcId);
					if (vIt != goIt->second.end()) { return vIt->second; }
				}
			}
			auto git = rtpcGlobal_.find(rtpcId);
			if (git != rtpcGlobal_.end()) { return git->second; }

			const RtpcDef* def = FindRtpc(rtpcId);
			return def ? def->defaultValue : 0.0f;
		}


		void AudioDirector::ApplyInstanceVolumes(float deltaTime)
		{
			if (!sound::SoundEngine::IsAvailable()) {
				return;
			}
			sound::SoundEngine& se = sound::SoundEngine::Get();

			for (PlayingInstance& inst : instances_) {
				if (inst.id == 0) {
					continue;
				}

				// フェードゲートを進める（フェードアウト完了で停止・回収）。
				const bool gateDone = inst.fadeGate.Update(deltaTime);

				// RTPC バインディングを集計（base × Σ binding）。
				float volMul   = 1.0f;
				float pitchMul = 1.0f;
				for (const AudioBank& bank : banks_) {
					for (const RtpcBindingDef& b : bank.GetRtpcBindings()) {
						bool match = false;
						switch (b.targetType) {
						case TargetType::Object: match = (inst.objectId == b.targetId); break;
						case TargetType::Kind:   match = (inst.kindId   == b.targetId); break;
						case TargetType::Bus:    match = (inst.bus      == b.targetBus); break;
						default: break;
						}
						if (!match) { continue; }
						const float y = EvalCurve(b.curve, b.interp, GetRtpc(b.rtpcId, inst.gameObject));
						if (b.property == RtpcProperty::VolumeDb) { volMul   *= DbToLinear(y); }
						else                                      { pitchMul *= y; }
					}
				}

				// Blend レイヤの音量変調（RTPC → volumeDb）。
				if (inst.layerRtpcId != 0 && !inst.layerCurve.empty()) {
					volMul *= DbToLinear(EvalCurve(inst.layerCurve, CurveInterp::Linear,
					                               GetRtpc(inst.layerRtpcId, inst.gameObject)));
				}

				// 合成: base × rtpc × fadeGate（音量） / base × rtpc（ピッチ）。
				const float volume = inst.baseVolume * volMul * inst.fadeGate.current;
				const float pitch  = inst.basePitch * pitchMul;

				if (inst.is3D) {
					if (auto* s = se.Resolve(inst.sourceHandle)) { s->SetVolume(volume); s->SetPitch(pitch); }
				}
				else if (inst.isStream) {
					if (inst.stream) { inst.stream->SetVolume(volume); inst.stream->SetPitch(pitch); }
				}
				else {
					se.SetVolume(inst.handle, volume);
					se.SetPitch(inst.handle, pitch);
				}

				// フェードアウト完了 → 実体を停止して回収する。
				if (gateDone && inst.fadeGate.stopAtEnd) {
					StopInstance(inst, 0.0f);
					RecycleInstance(inst);
				}
			}
		}


		NameId AudioDirector::PickSwitchChild(const SoundObjectDef& container, uint64_t gameObject)
		{
			NameId value = GetSwitch(container.switchGroupId, gameObject);
			if (value == 0) { value = container.defaultValue; }

			auto it = container.switchMap.find(value);
			if (it != container.switchMap.end()) { return it->second; }

			// 既定値で再試行。
			if (container.defaultValue != 0) {
				auto dIt = container.switchMap.find(container.defaultValue);
				if (dIt != container.switchMap.end()) { return dIt->second; }
			}
			return 0;
		}


		bool AudioDirector::ResolveObject(NameId objectId, uint64_t gameObject, ResolveResult& out)
		{
			const SoundObjectDef* o = FindObject(objectId);
			if (o == nullptr) {
				return false;
			}
			// このノードの音量/ピッチ補正（ランダム含む）を累積。
			out.volumeDb += o->volumeDb + RandRange(o->volumeRandomDb[0], o->volumeRandomDb[1]);
			out.pitch    *= o->pitch * RandRange(o->pitchRandom[0], o->pitchRandom[1]);

			switch (o->type) {
			case ObjectType::Sound:
				out.leafId = objectId;
				return true;
			case ObjectType::Random: {
				const NameId child = PickRandomChild(*o);
				return child != 0 && ResolveObject(child, gameObject, out);
			}
			case ObjectType::Sequence: {
				const NameId child = NextSequenceChild(*o);
				return child != 0 && ResolveObject(child, gameObject, out);
			}
			case ObjectType::Switch: {
				const NameId child = PickSwitchChild(*o, gameObject);
				return child != 0 && ResolveObject(child, gameObject, out);
			}
			default:
				return false;
			}
		}


		uint32_t AudioDirector::CountActiveByKind(NameId kindId) const
		{
			uint32_t count = 0;
			for (const PlayingInstance& inst : instances_) {
				if (inst.id != 0 && inst.kindId == kindId) { ++count; }
			}
			return count;
		}


		void AudioDirector::StealOldestByKind(NameId kindId)
		{
			// 最古 = 最小 id（おおむね）。停止し即時回収して枠を空ける。
			PlayingInstance* oldest = nullptr;
			for (PlayingInstance& inst : instances_) {
				if (inst.id != 0 && inst.kindId == kindId) {
					if (oldest == nullptr || inst.id < oldest->id) { oldest = &inst; }
				}
			}
			if (oldest) {
				StopInstance(*oldest, 0.0f);
				RecycleInstance(*oldest);
			}
		}


		PlayingId AudioDirector::PlayTopObject(NameId topObjectId, uint64_t gameObject, const ActionDef& action)
		{
			// Blend: 各レイヤを同時再生（レイヤごとに RTPC 音量変調を付与）。
			const SoundObjectDef* top = FindObject(topObjectId);
			if (top != nullptr && top->type == ObjectType::Blend) {
				PlayingId last = 0;
				for (const BlendLayer& layer : top->blendLayers) {
					const PlayingId id = PlayResolved(layer.childId, gameObject, action, layer.rtpcId, layer.curve);
					if (id != 0) { last = id; }
				}
				return last;
			}
			return PlayResolved(topObjectId, gameObject, action, /*layerRtpcId*/ 0, {});
		}


		PlayingId AudioDirector::PlayResolved(NameId topObjectId, uint64_t gameObject, const ActionDef& action,
		                                      NameId layerRtpcId, const std::vector<CurvePoint>& layerCurve)
		{
			sound::SoundEngine& se = sound::SoundEngine::Get();

			// ツリー解決 → 葉の Sound + 累積音量/ピッチ。
			ResolveResult r;
			if (!ResolveObject(topObjectId, gameObject, r)) {
				return 0;   // Sequence 末尾など、鳴らすものが無い
			}
			const SoundObjectDef* leaf = FindObject(r.leafId);
			if (leaf == nullptr || leaf->type != ObjectType::Sound) {
				return 0;
			}

			const KindDef* k = FindKind(leaf->kindId);
			const sound::SoundBusId bus = k ? k->bus : sound::SoundBusId::SE;
			const NameId kindId = leaf->kindId;

			// クールダウン（Kind 単位）。
			const float cooldownMs = k ? k->cooldownMs : 0.0f;
			if (cooldownMs > 0.0f) {
				auto it = kindCooldownUntil_.find(kindId);
				if (it != kindCooldownUntil_.end() && time_ < it->second) {
					return 0;
				}
			}

			// ボイス制限（§8）。
			if (k && k->maxVoices > 0 && CountActiveByKind(kindId) >= k->maxVoices) {
				if (k->voiceStealing == VoiceStealing::None || k->voiceStealing == VoiceStealing::Reject) {
					return 0;   // 拒否
				}
				StealOldestByKind(kindId);   // Oldest/Quietest/LowestPriority は暫定 Oldest
			}

			const float fadeMs    = action.fadeMs > 0.0f ? action.fadeMs : (k ? k->fadeInMs : 0.0f);
			const float fadeSec   = fadeMs / 1000.0f;
			const float volLinear = DbToLinear(r.volumeDb + (k ? k->volumeDb : 0.0f));
			const float pitch     = r.pitch * (k ? k->pitch : 1.0f);

			// 3D 判定（§4 spatialMode）: Is3D もしくは Auto+GameObject、かつ clip 常駐済み。
			const SpatialMode mode = k ? k->spatialMode : SpatialMode::Auto;
			const bool want3D = ((mode == SpatialMode::Is3D) || (mode == SpatialMode::Auto && gameObject != 0))
			                    && gameObject != 0 && static_cast<bool>(leaf->cachedClip);
			const bool stream = !want3D && (leaf->loop || (k && k->loadPolicy == LoadPolicy::Stream));

			PlayingInstance inst;
			inst.objectId   = leaf->nameId;
			inst.kindId     = kindId;
			inst.bus        = bus;
			inst.gameObject = gameObject;
			inst.baseVolume  = volLinear;   // RTPC/フェード適用の基準
			inst.basePitch   = pitch;
			inst.layerRtpcId = layerRtpcId; // Blend レイヤの音量変調（あれば）
			inst.layerCurve  = layerCurve;

			// フェードゲート(0..1)。SoundEngine 側フェードは使わず AudioDirector が所有して調停する。
			if (fadeSec > 0.0f) { inst.fadeGate.current = 0.0f; inst.fadeGate.FadeTo(1.0f, fadeSec, false); }
			else                { inst.fadeGate.SetImmediate(1.0f); }
			const float initVol = volLinear * inst.fadeGate.current;

			if (want3D) {
				const sound::SoundSourceHandle sh = se.CreateSource(leaf->cachedClip, bus);
				sound::SoundSource* src = se.Resolve(sh);
				if (src == nullptr) {
					return 0;
				}
				if (const AttenuationDef* at = k ? FindAttenuation(k->attenuationId) : nullptr) {
					src->SetAttenuation(at->model);
					src->SetDistances(at->minDistance, at->maxDistance);
				}
				src->SetPitch(pitch);
				auto goIt = gameObjects_.find(gameObject);
				if (goIt != gameObjects_.end()) {
					src->SetPosition(goIt->second.position);
					src->SetVelocity(goIt->second.velocity);
				}
				src->SetVolume(initVol);
				src->Play(leaf->loop ? sound::LoopRegion{ 0, 1, 0 } : sound::LoopRegion{});
				inst.is3D         = true;
				inst.sourceHandle = sh;
			}
			else if (stream) {
				std::unique_ptr<sound::SoundStream> s = se.OpenStream(leaf->clip.c_str(), bus);
				if (!s) {
					return 0;
				}
				if (pitch != 1.0f) { s->SetPitch(pitch); }
				s->Play(leaf->loop ? sound::LoopRegion{ 0, 1, 0 } : sound::LoopRegion{});
				s->SetVolume(initVol);
				inst.isStream = true;
				inst.stream   = std::move(s);
			}
			else {
				if (!leaf->cachedClip) {
					EnginePrintf("[Audio] object id=%u: clip 未ロード（事前ロード失敗?）。\n", leaf->nameId);
					return 0;
				}
				const sound::SoundHandle h = se.Play(leaf->cachedClip, bus, 0.0f);   // engine フェードは使わない
				if (!h.IsValid()) {
					return 0;
				}
				se.SetVolume(h, initVol);
				if (pitch != 1.0f) { se.SetPitch(h, pitch); }
				inst.handle = h;
			}

			const PlayingId id = nextId_++;
			if (nextId_ == 0) { nextId_ = 1; }
			inst.id = id;

			bool placed = false;
			for (PlayingInstance& slot : instances_) {
				if (slot.id == 0) { slot = std::move(inst); placed = true; break; }
			}
			if (!placed) { instances_.push_back(std::move(inst)); }

			if (cooldownMs > 0.0f) {
				kindCooldownUntil_[kindId] = static_cast<float>(time_) + cooldownMs / 1000.0f;
			}
			return id;
		}


		void AudioDirector::StopByTarget(const ActionDef& action)
		{
			const float fadeSec = action.fadeMs / 1000.0f;
			for (PlayingInstance& inst : instances_) {
				if (inst.id == 0) {
					continue;
				}
				bool match = false;
				switch (action.targetType) {
				case TargetType::Object: match = (inst.objectId == action.targetId); break;
				case TargetType::Kind:   match = (inst.kindId   == action.targetId); break;
				case TargetType::Bus:    match = (inst.bus      == action.targetBus); break;
				default: break;
				}
				if (match) {
					StopInstance(inst, fadeSec);
				}
			}
		}


		void AudioDirector::StopInstance(PlayingInstance& inst, float fadeSeconds)
		{
			if (!sound::SoundEngine::IsAvailable()) {
				return;
			}
			sound::SoundEngine& se = sound::SoundEngine::Get();

			// フェード停止はゲートに委譲（ApplyInstanceVolumes が 0 まで下げて停止・回収する）。
			if (fadeSeconds > 0.0f) {
				inst.fadeGate.FadeTo(0.0f, fadeSeconds, /*stopWhenDone*/ true);
				return;
			}

			// 即時停止。実体の回収は呼び出し側 or Update（!IsPlaying）で行う。
			if (inst.is3D) {
				if (sound::SoundSource* src = se.Resolve(inst.sourceHandle)) { src->Stop(); }
			}
			else if (inst.isStream) {
				if (inst.stream) { inst.stream->Stop(); }
			}
			else {
				se.Stop(inst.handle);
			}
		}


		void AudioDirector::StopPlaying(PlayingId id, float fadeSeconds)
		{
			if (id == 0) {
				return;
			}
			for (PlayingInstance& inst : instances_) {
				if (inst.id == id) {
					StopInstance(inst, fadeSeconds);
					break;
				}
			}
		}


		void AudioDirector::StopAllByKind(NameId kindId, float fadeSeconds)
		{
			for (PlayingInstance& inst : instances_) {
				if (inst.id != 0 && inst.kindId == kindId) {
					StopInstance(inst, fadeSeconds);
				}
			}
		}


		void AudioDirector::CollectDebugVoices(std::vector<DebugVoiceInfo>& out) const
		{
			out.clear();
			for (const PlayingInstance& inst : instances_) {
				if (inst.id != 0) {
					out.push_back({ inst.id, inst.objectId, inst.kindId, inst.bus, inst.isStream });
				}
			}
		}


		void AudioDirector::GetGlobalSwitches(std::vector<std::pair<NameId, NameId>>& out) const
		{
			out.clear();
			auto it = switchByGo_.find(0);
			if (it != switchByGo_.end()) {
				for (const auto& kv : it->second) {
					out.emplace_back(kv.first, kv.second);
				}
			}
		}


		std::string AudioDirector::NameOf(NameId id) const
		{
			for (const AudioBank& b : banks_) {
				const std::string& n = b.NameOf(id);
				if (!n.empty()) { return n; }
			}
			return std::string();
		}


		void AudioDirector::CollectRtpc(std::vector<RtpcInfo>& out) const
		{
			out.clear();
			for (const AudioBank& b : banks_) {
				for (const auto& kv : b.GetRtpcDefs()) {
					const RtpcDef& d = kv.second;
					out.push_back({ d.nameId, d.minValue, d.maxValue, d.defaultValue, GetRtpc(d.nameId, 0) });
				}
			}
		}


		void AudioDirector::CollectObjects(std::vector<ObjectInfo>& out) const
		{
			out.clear();
			for (const AudioBank& b : banks_) {
				for (const auto& kv : b.GetObjects()) {
					out.push_back({ kv.second.nameId, kv.second.type });
				}
			}
		}


		void AudioDirector::CollectStateGroups(std::vector<std::pair<NameId, std::vector<NameId>>>& out) const
		{
			out.clear();
			auto addValue = [&out](NameId group, NameId value) {
				for (auto& g : out) {
					if (g.first == group) {
						for (NameId v : g.second) { if (v == value) return; }
						g.second.push_back(value);
						return;
					}
				}
				out.push_back({ group, { value } });
			};
			for (const AudioBank& b : banks_) {
				for (const StateRuleDef& r : b.GetStateRules()) {
					addValue(r.groupId, r.valueId);
				}
			}
		}


		void AudioDirector::CollectSwitchGroups(std::vector<std::pair<NameId, std::vector<NameId>>>& out) const
		{
			out.clear();
			auto addValue = [&out](NameId group, NameId value) {
				for (auto& g : out) {
					if (g.first == group) {
						for (NameId v : g.second) { if (v == value) return; }
						g.second.push_back(value);
						return;
					}
				}
				out.push_back({ group, { value } });
			};
			for (const AudioBank& b : banks_) {
				for (const auto& kv : b.GetObjects()) {
					const SoundObjectDef& o = kv.second;
					if (o.type == ObjectType::Switch) {
						for (const auto& m : o.switchMap) { addValue(o.switchGroupId, m.first); }
					}
				}
			}
		}


		NameId AudioDirector::GetCurrentState(NameId groupId) const
		{
			auto it = currentStates_.find(groupId);
			return it != currentStates_.end() ? it->second : 0;
		}


		PlayingId AudioDirector::DebugPlayObject(NameId objectId)
		{
			ActionDef action;
			action.type       = ActionType::Play;
			action.targetType = TargetType::Object;
			action.targetId   = objectId;
			return PlayTopObject(objectId, 0, action);
		}
	}
}
