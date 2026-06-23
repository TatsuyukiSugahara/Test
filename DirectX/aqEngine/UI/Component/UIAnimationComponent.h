#pragma once
#include "IUIComponent.h"
#include "UI/Animation/UIAnimationClip.h"
#include "UI/Animation/UIAnimatedProperty.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace aq
{
	namespace ui
	{
		// UIAnimationComponent: UIObject に貼るアニメーション管理コンポーネント。
		// UIAnimationSystem::Update() から毎フレーム Update(dt) が呼ばれる。
		class UIAnimationComponent : public IUIComponent
		{
		public:
			// ゲームコードが OnCreate 等で登録するクリップ定義
			std::unordered_map<std::string, UIAnimationClip> clips;

			// ---- ランタイム API ----

			// クリップを再生開始 (Play 中のクリップは即中断)
			void Play(const std::string& clipName);

			// 現在のクリップを停止
			void Stop();

			// Trigger 条件を持つクリップトラックを起動 (conditionParam が name と一致するもの)
			void TriggerTrack(const std::string& conditionParam);

			// Bool 条件を持つクリップトラックの値を設定
			void SetCondition(const std::string& name, bool value);

			bool GetCondition(const std::string& name) const;

			bool IsPlaying() const { return currentClip_ != nullptr; }

			// UIAnimationSystem から毎フレーム呼ばれる。GetOwner() 経由で UIObject を取得する。
			void Update(float dt);

		private:
			// クリップトラックのランタイム状態
			struct TrackRuntime
			{
				const UIClipTrack* def       = nullptr;
				float              time      = 0.f;
				bool               active    = false;
				bool               finished  = false;
				bool               triggered = false; // Trigger: 起動リクエスト

				// restoreOnComplete 用スナップショット (起動時の各プロパティ値)
				std::unordered_map<UIAnimatedProperty, float> snapshot;
			};

			void ActivateClip(const UIAnimationClip* clip);
			void TakeSnapshot(TrackRuntime& rt) const;
			void RestoreSnapshot(const TrackRuntime& rt);

			const UIAnimationClip*                         currentClip_ = nullptr;
			std::vector<TrackRuntime>                      runtimes_;
			std::unordered_map<std::string, bool>          conditions_;
		};

	} // namespace ui
} // namespace aq
