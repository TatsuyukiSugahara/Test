#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "Sound/SoundTypes.h"
#include "Sound/SoundFwd.h"


namespace aq
{
	namespace audio
	{
		using NameId = uint32_t;   // 名前の CRC32

		// クリップの常駐/ストリーミング方針（§4 loadPolicy）。
		enum class LoadPolicy : uint8_t
		{
			Auto,
			Preload,
			Stream,
		};

		// 2D/3D 判定（§4 spatialMode）。
		enum class SpatialMode : uint8_t
		{
			Auto,   // GameObject 指定時のみ 3D
			Is2D,
			Is3D,
		};

		// 3D 距離減衰プリセット（§3.4）。
		struct AttenuationDef
		{
			NameId                  nameId      = 0;
			sound::AttenuationModel model       = sound::AttenuationModel::Inverse;
			float                   minDistance = 1.0f;
			float                   maxDistance = 1000.0f;
		};

		// ボイス枯渇時の処理（§8）。フェーズ2 は Oldest を実装、Quietest/LowestPriority は Oldest 代替。
		enum class VoiceStealing : uint8_t
		{
			None,           // 上限超過時は新規を鳴らさない
			Reject,         // 同上（明示）
			Oldest,         // 最古を停止
			Quietest,       // （暫定 Oldest 代替）
			LowestPriority, // （暫定 Oldest 代替）
		};

		// Kind = サウンドの種別プリセット（§4）。
		struct KindDef
		{
			NameId           nameId        = 0;
			sound::SoundBusId bus          = sound::SoundBusId::SE;
			LoadPolicy       loadPolicy    = LoadPolicy::Auto;
			float            cooldownMs    = 0.0f;
			float            fadeInMs      = 0.0f;
			float            fadeOutMs     = 0.0f;
			float            volumeDb      = 0.0f;
			float            pitch         = 1.0f;
			uint32_t         maxVoices     = 0;     // 0 = 無制限
			VoiceStealing    voiceStealing = VoiceStealing::Oldest;
			SpatialMode      spatialMode   = SpatialMode::Auto;
			NameId           attenuationId = 0;     // 3D 減衰プリセット名
		};

		// SoundObject。フェーズ2 で Random/Sequence、3 で Switch、追加で Blend。
		enum class ObjectType : uint8_t
		{
			Sound,
			Random,
			Sequence,
			Switch,
			Blend,
		};

		// カーブ点（RTPC 値 → プロパティ値）。Blend レイヤ/RTPC binding で共用。
		struct CurvePoint
		{
			float x = 0.0f;   // RTPC 値
			float y = 0.0f;   // プロパティ値（volumeDb なら dB、pitch なら倍率）
		};

		// Blend のレイヤ（同時再生する子 + 任意の RTPC 音量制御）。§3.5
		struct BlendLayer
		{
			NameId                  childId = 0;   // レイヤの子オブジェクト
			NameId                  rtpcId  = 0;   // レイヤ音量を制御する RTPC（0=固定）
			std::vector<CurvePoint> curve;         // rtpc値 → volumeDb
		};

		struct SoundObjectDef
		{
			NameId      nameId   = 0;
			ObjectType  type     = ObjectType::Sound;
			NameId      kindId   = 0;        // 所属 Kind
			std::string clip;                // Sound: clip パス（basePath 連結済み）
			bool        loop     = false;
			float       volumeDb = 0.0f;
			float       pitch    = 1.0f;

			// コンテナ（Random/Sequence）用
			std::vector<NameId> childIds;
			std::vector<float>  weights;          // Random の重み（空なら等確率）
			uint32_t            avoidRepeat = 0;  // Random: 直近 N を再選択しない
			float               volumeRandomDb[2] = { 0.0f, 0.0f };  // [min,max]
			float               pitchRandom[2]    = { 1.0f, 1.0f };  // [min,max]

			// Switch コンテナ用
			NameId                            switchGroupId = 0;  // 参照する Switch グループ
			NameId                            defaultValue  = 0;  // 値未設定/未一致時の既定値
			std::unordered_map<NameId, NameId> switchMap;          // 値 → 子オブジェクト

			// Blend コンテナ用
			std::vector<BlendLayer> blendLayers;

			// 非ストリーミング Sound はロード時に事前ロードしてキャッシュする。
			sound::RefSoundClip cachedClip;
		};

		// Action（§6）。フェーズ1 は Play / Stop のみ。
		enum class ActionType : uint8_t
		{
			Unknown,
			Play,
			Stop,
		};
		enum class TargetType : uint8_t
		{
			None,
			Object,
			Kind,
			Bus,
		};

		struct ActionDef
		{
			ActionType type       = ActionType::Unknown;
			TargetType targetType = TargetType::None;
			NameId     targetId   = 0;        // object/kind 名の CRC32
			sound::SoundBusId targetBus = sound::SoundBusId::SE;
			float      fadeMs     = 0.0f;
		};

		struct EventDef
		{
			NameId                 nameId = 0;
			std::vector<ActionDef> actions;
		};


		// ── RTPC（連続パラメータ → プロパティ変調）§7 ──
		struct RtpcDef
		{
			NameId nameId       = 0;
			float  minValue     = 0.0f;
			float  maxValue     = 1.0f;
			float  defaultValue = 0.0f;
		};

		enum class RtpcProperty : uint8_t
		{
			VolumeDb,
			Pitch,
		};
		enum class CurveInterp : uint8_t
		{
			Linear,
			Ease,   // smoothstep
		};
		// State 変化時に実行するルール（§3.6 stateRules）。
		struct StateRuleDef
		{
			NameId                 groupId = 0;
			NameId                 valueId = 0;
			std::vector<ActionDef> actions;
		};

		// 自動ダッキング（§9）。trigger が鳴っている間 target バスを下げる。
		struct DuckingDef
		{
			NameId            nameId       = 0;
			TargetType        triggerType  = TargetType::Bus;   // Bus or Kind
			NameId            triggerKind  = 0;
			sound::SoundBusId triggerBus   = sound::SoundBusId::Voice;
			sound::SoundBusId targetBus    = sound::SoundBusId::BGM;
			float             amountDb     = -6.0f;
			float             attackMs     = 100.0f;
			float             releaseMs    = 300.0f;
		};

		// RTPC → (target の property) を curve で変調する binding（§3.6）。
		struct RtpcBindingDef
		{
			NameId                  rtpcId     = 0;
			TargetType              targetType = TargetType::Kind;
			NameId                  targetId   = 0;
			sound::SoundBusId       targetBus  = sound::SoundBusId::SE;
			RtpcProperty            property   = RtpcProperty::VolumeDb;
			CurveInterp             interp     = CurveInterp::Linear;
			std::vector<CurvePoint> curve;     // x 昇順
		};
	}
}
