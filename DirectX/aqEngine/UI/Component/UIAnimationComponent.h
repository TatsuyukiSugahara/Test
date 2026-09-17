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
		 */
		class UIAnimationComponent : public IUIComponent
		{
		private:
			/**
			 * クリップ 1 本分のランタイム状態。clips_[i] と対になる (ポインタは持たない)。
			 * 本命は P2 のレイヤー評価 (activationSerial の運用 / 基準値ライフサイクル) で書き直す。
			 * snapshot / triggered は P1 の暫定 Update() 専用のメンバで、P2 で削除する前提
			 */
			struct ClipRuntime
			{
				float    time             = 0.f;
				bool     active           = false;
				uint32_t activationSerial = 0u; // P1 では発行しない (常に 0)

				/** P1 暫定 Update() 専用 (P2 で削除予定) */
				std::unordered_map<UIAnimatedProperty, float> snapshot;          // finish == Restore 用の起動時スナップショット
				bool                                           triggered = false; // Trigger: 起動リクエスト
			};


			/** クリップ定義とその並列ランタイム */
			std::vector<UIAnimationClip> clips_;
			std::vector<ClipRuntime>     runtimes_;

			/** Bool 条件の現在値 (キーは conditionParam のハッシュ) */
			std::unordered_map<uint32_t, bool> conditions_;


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
			// P1 時点は暫定ロジック (設計書 §11 の P1 補足)。レイヤー / serial / 基準値は未実装
			void Update(const float dt);

		private:
			void ResetClipRuntime(const size_t index); // 対象 runtime だけ初期化 (condition / group 変更用)
			void TakeSnapshot(const size_t index);
			void RestoreSnapshot(const size_t index);
		};

	} // namespace ui
} // namespace aq
