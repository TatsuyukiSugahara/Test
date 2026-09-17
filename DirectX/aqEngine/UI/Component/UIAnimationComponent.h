#pragma once
#include "IUIComponent.h"
#include "UI/Animation/UIAnimationClip.h"
#include "UI/Animation/UIAnimatedProperty.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aq
{
	namespace ui
	{
		/**
		 * UIObject に貼るアニメーション管理コンポーネント。
		 * UIAnimationSystem::Update() から毎フレーム Update(dt) が呼ばれる。
		 *
		 * クリップは private の clips_ に持ち、実行時状態は並列の runtimes_ で持つ (設計書 §5.1)。
		 * 構造を変える操作 (追加 / 削除 / 並べ替え / 全置換) は StopAll() + 全 runtime 再構築、
		 * condition / group の変更は対象 runtime だけの初期化で済ませる (再構築契約)。
		 * clips_ は直接編集させず、構造変更は下記の編集 API に限定する。
		 *
		 * 同じプロパティを複数のクリップが動かす場合は activationSerial が最大のクリップを
		 * レイヤーの勝者として選び、その 1 本だけを適用する (設計書 §6)。基準値 (baseValues_) は
		 * レイヤーが載っていないときの UIObject の値を保持し、最後のレイヤーが外れたら書き戻す。
		 */
		class UIAnimationComponent : public IUIComponent
		{
		private:
			/**
			 * クリップ 1 本分のランタイム状態。clips_[i] と対になる (ポインタは持たない)。
			 * active はレイヤーが載っていること、completed は非ループの Bool クリップが
			 * 終端に達し最終値を保持したまま残っていることを表す (Manual / Trigger は完了と
			 * 同時に active が false になるため completed を使わない)
			 */
			struct ClipRuntime
			{
				float    time             = 0.f;
				bool     active           = false;
				bool     completed        = false;
				uint32_t activationSerial = 0u; // 0 = 未起動
			};


			/** クリップ定義とその並列ランタイム */
			std::vector<UIAnimationClip> clips_;
			std::vector<ClipRuntime>     runtimes_;

			/** Bool 条件の現在値 (キーは conditionParam のハッシュ) */
			std::unordered_map<uint32_t, bool> conditions_;

			/** レイヤーが載っていないときにプロパティへ書き戻す基準値 (設計書 §6.4) */
			std::unordered_map<UIAnimatedProperty, float> baseValues_;

			/** 起動単位ごとに 1 つ発行する連番。Play() 1 回 / Trigger() 1 回 / Bool の false→true 1 回で 1 つ進む */
			uint32_t nextSerial_ = 0u;


			/**
			 * 編集 API (設計書 §5.1)。構造を動かす操作はここに限定する。
			 * condition / group の変更は runtime 初期化が必要なため、必ず setter を通す
			 */
		public:
			inline const std::vector<UIAnimationClip>& GetClips() const { return clips_; }

			size_t AddClip(UIAnimationClip clip);
			void   RemoveClip(const size_t index);
			void   MoveClip(const size_t from, const size_t to);
			void   ReplaceAllClips(std::vector<UIAnimationClip> clips); // JSON Load 用。既存を全部捨てる

			void   SetClipCondition(const size_t index, const UIClipCondition condition, const uint32_t param, std::string_view paramName);
			void   SetClipGroup(const size_t index, std::string_view groupName);

			// Track / Keyframe / duration / loop / finish の編集用。condition / group はここから触らない (setter を使う)
			UIAnimationClip& EditClip(const size_t index);


			/**
			 * ランタイム API (設計書 §5.2)
			 */
		public:
			void Play(const uint32_t group);    // group の Manual クリップを全部起動
			void Stop(const uint32_t group);     // group の Manual クリップを停止
			void StopAll();                      // 全 Manual クリップを停止 (Bool / Trigger は対象外)

			void SetCondition(const uint32_t condition, const bool value);
			void Trigger(const uint32_t trigger);

			bool GetCondition(const uint32_t condition) const;
			bool IsGroupPlaying(const uint32_t group) const; // group の Manual クリップが 1 本でも active
			bool IsPlaying() const;                          // Manual クリップが 1 本でも active

			// UIAnimationSystem から毎フレーム呼ばれる。GetOwner() 経由で UIObject を取得する。
			// AdvanceRuntimes(dt) で状態を進め、ApplyLayers() でプロパティへ反映する (設計書 §6.3)
			void Update(const float dt);

		private:
			void ResetClipRuntime(const size_t index); // 対象 runtime だけ初期化 (condition / group 変更用)

			// 状態更新 (§6.3 の 1 段階目)。起動判定・時刻・ループ・完了を進める。プロパティには触らない
			void AdvanceRuntimes(const float dt);

			// 適用 (§6.3 の 2 段階目)。プロパティごとに勝者を 1 本選び、基準値を管理しながら書き込む
			void ApplyLayers();

			// 非ループの Manual / Trigger クリップが finish == Hold で完了したときの確定処理 (§6.4-3)。
			// clip.duration 時点の値を基準値へ書く
			void CommitHold(const size_t index);

			// property を持つ Track を全クリップから 1 つ探す (基準値の読み書きはどの Track でもよい)
		};

	} // namespace ui
} // namespace aq
