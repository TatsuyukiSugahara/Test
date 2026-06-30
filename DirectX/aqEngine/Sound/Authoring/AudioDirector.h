#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <random>
#include <string>
#include <utility>
#include "AudioDefs.h"
#include "AudioBank.h"
#include "Math/Vector.h"
#include "Sound/SoundHandle.h"
#include "Sound/VolumeEnvelope.h"
#include "Sound/SoundStream.h"   // PlayingInstance が unique_ptr<SoundStream> を持つため完全型が必要


namespace aq
{
	namespace audio
	{
		using PlayingId = uint32_t;   // 0 = 無効

		// データ駆動オーディオ層の窓口（§1）。Event を解決して SoundEngine に流す。
		// フェーズ1: Bank ロード / PostEvent(Play/Stop) / Kind(bus/cooldown/loadPolicy/fade) / Sound。
		class AudioDirector
		{
		// ── メンバ型 ──
		private:
			struct PlayingInstance
			{
				PlayingId                          id     = 0;
				NameId                             objectId = 0;
				NameId                             kindId   = 0;
				sound::SoundBusId                  bus      = sound::SoundBusId::SE;
				sound::SoundHandle                 handle;          // ワンショット(2D)
				std::unique_ptr<sound::SoundStream> stream;         // ループ/ストリーミング(2D)
				sound::SoundSourceHandle           sourceHandle;    // 3D ソース
				uint64_t                           gameObject = 0;  // 3D の位置追従元
				float                              baseVolume = 1.0f;  // RTPC 適用前の基準音量(linear)
				float                              basePitch  = 1.0f;  // RTPC 適用前の基準ピッチ
				sound::VolumeEnvelope              fadeGate;           // フェード乗数(0..1)。AudioDirector が所有
				NameId                             layerRtpcId = 0;    // Blend レイヤの音量 RTPC（0=なし）
				std::vector<CurvePoint>            layerCurve;         // Blend レイヤの音量カーブ
				sound::RefSoundClip                clip;               // 仮想化からの復帰用（3D ループ）
				uint8_t                            priority = 50;      // 仮想化の優先度
				bool                               isStream = false;
				bool                               is3D     = false;
				bool                               loop     = false;
				bool                               isVirtual = false;  // 実ボイス無しで生存中（§8 virtualization）
			};

			// 3D 発生主体の状態（GameObject）。
			struct GameObjectState
			{
				math::Vector3 position;
				math::Vector3 forward = math::Vector3(0.0f, 0.0f, 1.0f);
				math::Vector3 up      = math::Vector3(0.0f, 1.0f, 0.0f);
				math::Vector3 velocity;
			};

		// ── メンバ変数 ──
		private:
			std::vector<AudioBank>            banks_;
			std::vector<PlayingInstance>      instances_;
			std::unordered_map<NameId, float> kindCooldownUntil_;   // Kind → 再生可能になる時刻(秒)
			std::unordered_map<NameId, uint32_t> randomLast_;       // Random: 直近選択した子 index
			std::unordered_map<NameId, uint32_t> seqNext_;          // Sequence: 次の子 index
			// Switch 値: GameObject(0=グローバル) → (グループ → 値)
			std::unordered_map<uint64_t, std::unordered_map<NameId, NameId>> switchByGo_;
			std::unordered_map<uint64_t, GameObjectState>                    gameObjects_;
			// RTPC 値: グローバル + GameObject 単位
			std::unordered_map<NameId, float>                                rtpcGlobal_;
			std::unordered_map<uint64_t, std::unordered_map<NameId, float>>  rtpcByGo_;
			std::unordered_map<NameId, NameId> currentStates_;     // グループ → 現在値
			std::unordered_map<NameId, float>  duckCurrentDb_;     // ducking 名 → 現在の減衰量(dB)
			std::mt19937                      rng_{ 12345u };
			double                            time_       = 0.0;
			PlayingId                         nextId_     = 1;

		// ── メンバ関数 ──
		private:
			AudioDirector() = default;
			~AudioDirector();   // 定義は .cpp（unique_ptr<SoundStream> の完全型が必要）

		public:
			bool Initialize();
			void Finalize();
			void Update(float deltaTime);

			void LoadBank(const char* path);

			PlayingId PostEvent(NameId eventId, uint64_t gameObject = 0);
			void      StopPlaying(PlayingId id, float fadeSeconds = 0.0f);
			void      StopAllByKind(NameId kindId, float fadeSeconds = 0.0f);

			// Switch 値を設定（gameObject=0 でグローバル）。§7
			void      SetSwitch(NameId groupId, NameId valueId, uint64_t gameObject = 0);
			// RTPC 値を設定（gameObject=0 でグローバル）。§7
			void      SetRTPC(NameId rtpcId, float value, uint64_t gameObject = 0);
			// State 値を設定し、対応する stateRules を実行する（§7）
			void      SetState(NameId groupId, NameId valueId);

			// ── 3D GameObject（§11）──
			void RegisterGameObject(uint64_t go);
			void UnregisterGameObject(uint64_t go);
			void SetGameObjectTransform(uint64_t go, const math::Vector3& pos, const math::Vector3& forward,
			                            const math::Vector3& up, const math::Vector3& velocity);
			// リスナー（カメラ）。SoundEngine の SoundListener へ委譲。
			void SetListener(const math::Vector3& pos, const math::Vector3& forward,
			                 const math::Vector3& up, const math::Vector3& velocity);

			// ── デバッグ UI 用（読み取り）──
			struct DebugVoiceInfo
			{
				PlayingId         id;
				NameId            objectId;
				NameId            kindId;
				sound::SoundBusId bus;
				bool              isStream;
				bool              isVirtual;
			};
			void CollectDebugVoices(std::vector<DebugVoiceInfo>& out) const;
			void GetGlobalSwitches(std::vector<std::pair<NameId, NameId>>& out) const;
			std::string NameOf(NameId id) const;
			const std::vector<AudioBank>& GetBanks() const { return banks_; }

			// ── オーサリングエディタ用 ──
			struct RtpcInfo { NameId id; float minV; float maxV; float defV; float current; };
			struct ObjectInfo { NameId id; ObjectType type; };
			void CollectRtpc(std::vector<RtpcInfo>& out) const;
			void CollectObjects(std::vector<ObjectInfo>& out) const;
			// 各グループ → 値リスト（stateRules / Switch オブジェクトから導出）。
			void CollectStateGroups(std::vector<std::pair<NameId, std::vector<NameId>>>& out) const;
			void CollectSwitchGroups(std::vector<std::pair<NameId, std::vector<NameId>>>& out) const;
			NameId GetCurrentState(NameId groupId) const;   // 未設定は 0
			NameId GetCurrentSwitch(NameId groupId) const;  // グローバル Switch 値。未設定は 0
			// オブジェクト定義の参照（ツリー可視化用）。
			const SoundObjectDef* GetObjectDef(NameId objectId) const;
			// エディタからオブジェクトを直接再生する（イベント不要）。
			PlayingId DebugPlayObject(NameId objectId);

		private:
			// Bank 横断で定義を引く（先勝ち）。
			const KindDef*        FindKind(NameId id)        const;
			const AttenuationDef* FindAttenuation(NameId id) const;
			const SoundObjectDef* FindObject(NameId id)      const;
			const EventDef*       FindEvent(NameId id)       const;
			const RtpcDef*        FindRtpc(NameId id)        const;

			// RTPC 値取得（per-GO → グローバル → 定義の default）。
			float GetRtpc(NameId rtpcId, uint64_t gameObject) const;
			// インスタンス音量/ピッチの調停（毎フレーム）: base × rtpc × fadeGate を合成して適用。
			void  ApplyInstanceVolumes(float deltaTime);

			// コンテナ解決の結果（葉の Sound と累積した音量/ピッチ）。
			struct ResolveResult
			{
				NameId leafId   = 0;
				float  volumeDb = 0.0f;
				float  pitch    = 1.0f;
			};
			// SoundObject ツリーを解決して葉の Sound を決める（Random/Sequence/Switch 含む）。
			bool ResolveObject(NameId objectId, uint64_t gameObject, ResolveResult& out);
			NameId PickRandomChild(const SoundObjectDef& container);
			NameId NextSequenceChild(const SoundObjectDef& container);
			NameId PickSwitchChild(const SoundObjectDef& container, uint64_t gameObject);
			float  RandRange(float a, float b);

			// Switch 値の取得（per-GO → グローバルへフォールバック）。
			NameId GetSwitch(NameId groupId, uint64_t gameObject) const;

			// 1 つの Action を実行（Play/Stop）。event / stateRule 共通。
			PlayingId ExecuteAction(const ActionDef& action, uint64_t gameObject);
			// Play アクションを実行して PlayingId を返す。Blend は各レイヤを同時再生する。
			PlayingId PlayTopObject(NameId topObjectId, uint64_t gameObject, const ActionDef& action);
			// 1 オブジェクト（Sound/Random/Sequence/Switch）を解決して再生する。
			// layerRtpcId/layerCurve は Blend レイヤの音量変調（0/空ならなし）。
			PlayingId PlayResolved(NameId objectId, uint64_t gameObject, const ActionDef& action,
			                       NameId layerRtpcId, const std::vector<CurvePoint>& layerCurve);
			// Stop アクションを実行（target 種別で対象を絞る）。
			void      StopByTarget(const ActionDef& action);

			// ダッキング更新（毎フレーム）。
			void UpdateDucking(float deltaTime);
			bool IsDuckTriggerActive(const DuckingDef& def) const;

			// ボイス制限（§8）。
			uint32_t CountActiveByKind(NameId kindId) const;
			void     StealOldestByKind(NameId kindId);

			// 仮想化（§8 virtualization）。3D ループ音を実ボイス無しで生かす。
			uint32_t CountRealByKind(NameId kindId) const;
			AudioDirector::PlayingInstance* LowestPriorityRealByKind(NameId kindId);
			void VirtualizeInstance(PlayingInstance& inst);
			bool DevirtualizeInstance(PlayingInstance& inst);
			void UpdateVirtualization();

			void StopInstance(PlayingInstance& inst, float fadeSeconds);
			void RecycleInstance(PlayingInstance& inst);

		// ── Singleton ──
		private:
			static AudioDirector* instance_;

		public:
			static void Create()
			{
				if (instance_ == nullptr) { instance_ = new AudioDirector(); }
			}
			static AudioDirector& Get() { return *instance_; }
			static bool IsAvailable() { return instance_ != nullptr; }
			static void Release()
			{
				if (instance_) { delete instance_; instance_ = nullptr; }
			}
		};
	}
}
