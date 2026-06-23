#include "aq.h"
#include "UIAnimationComponent.h"
#include "UI/UIObject.h"
#include <cmath>
#include <cassert>

namespace aq
{
	namespace ui
	{
		// ---- パブリック API ---------------------------------------------------------

		void UIAnimationComponent::Play(const std::string& clipName)
		{
			auto it = clips.find(clipName);
			if (it == clips.end()) return;
			ActivateClip(&it->second);
		}

		void UIAnimationComponent::Stop()
		{
			currentClip_ = nullptr;
			runtimes_.clear();
		}

		void UIAnimationComponent::TriggerTrack(const std::string& conditionParam)
		{
			for (auto& rt : runtimes_)
			{
				if (rt.def
					&& rt.def->condition == UITrackCondition::Trigger
					&& rt.def->conditionParam == conditionParam)
				{
					rt.triggered = true;
				}
			}
		}

		void UIAnimationComponent::SetCondition(const std::string& name, bool value)
		{
			conditions_[name] = value;
		}

		bool UIAnimationComponent::GetCondition(const std::string& name) const
		{
			auto it = conditions_.find(name);
			return it != conditions_.end() && it->second;
		}


		// ---- 毎フレーム更新 ---------------------------------------------------------

		void UIAnimationComponent::Update(float dt)
		{
			if (!currentClip_ || runtimes_.empty()) return;

			const float clipDuration = currentClip_->duration;
			bool        clipComplete = false;
			UIObject*   obj          = GetOwner();

			for (auto& rt : runtimes_)
			{
				const UIClipTrack& def = *rt.def;

				// --- アクティブ条件を評価 ---
				bool shouldBeActive = false;
				switch (def.condition)
				{
					case UITrackCondition::Default:
						shouldBeActive = !rt.finished;
						break;
					case UITrackCondition::Bool:
						shouldBeActive = GetCondition(def.conditionParam);
						break;
					case UITrackCondition::Trigger:
						// triggered フラグが立っていれば一回だけ起動
						if (rt.triggered && !rt.active && !rt.finished)
						{
							shouldBeActive   = true;
							rt.triggered     = false;
							TakeSnapshot(rt);  // スナップショットはトリガー起動時に取得
						}
						else
						{
							shouldBeActive = rt.active;
						}
						break;
				}

				// アクティブ → 非アクティブ (Bool track が false になった)
				if (rt.active && !shouldBeActive)
				{
					rt.active = false;
					rt.time   = 0.f;
					continue;
				}

				// 非アクティブ → アクティブ
				if (!rt.active && shouldBeActive)
				{
					rt.active = true;
					rt.time   = 0.f;
					if (def.condition != UITrackCondition::Trigger)
						TakeSnapshot(rt);  // Default / Bool はここでスナップショット
				}

				if (!rt.active) continue;

				rt.time += dt;

				// --- ループ処理 ---
				float sampleTime = rt.time;
				if (def.loopFrom >= 0.f && rt.time >= clipDuration)
				{
					float loopDuration = clipDuration - def.loopFrom;
					if (loopDuration <= 0.f) { loopDuration = clipDuration; }
					const float excess = rt.time - clipDuration;
					rt.time    = def.loopFrom + std::fmod(excess, loopDuration);
					sampleTime = rt.time;
				}

				// --- 終了判定 (ループなしのみ) ---
				bool trackComplete = (def.loopFrom < 0.f && rt.time >= clipDuration);
				if (trackComplete)
				{
					sampleTime = clipDuration;
					rt.active  = false;
					rt.finished = true;

					// Default トラックだけがクリップ完了を担う
					if (def.condition == UITrackCondition::Default)
						clipComplete = true;

					// restoreOnComplete: スナップショットへ戻す
					if (def.restoreOnComplete)
					{
						RestoreSnapshot(rt);
						continue;  // Apply はスキップ (リストア済み)
					}
				}

				// --- プロパティ適用 ---
				for (const auto& animTrack : def.tracks)
				{
					const float value = animTrack.Sample(sampleTime);
					animTrack.Apply(obj, value);
				}
			}

			if (clipComplete)
				Stop();
		}


		// ---- プライベートヘルパー ---------------------------------------------------

		void UIAnimationComponent::ActivateClip(const UIAnimationClip* clip)
		{
			currentClip_ = clip;
			runtimes_.clear();
			if (!clip) return;

			runtimes_.reserve(clip->clipTracks.size());
			for (const auto& ct : clip->clipTracks)
			{
				TrackRuntime rt;
				rt.def = &ct;
				runtimes_.push_back(std::move(rt));
			}
		}

		void UIAnimationComponent::TakeSnapshot(TrackRuntime& rt) const
		{
			const UIObject* obj = GetOwner();
			if (!obj) return;
			rt.snapshot.clear();
			for (const auto& animTrack : rt.def->tracks)
			{
				rt.snapshot[animTrack.property] = animTrack.ReadFrom(obj);
			}
		}

		void UIAnimationComponent::RestoreSnapshot(const TrackRuntime& rt)
		{
			UIObject* obj = GetOwner();
			if (!obj) return;
			for (const auto& animTrack : rt.def->tracks)
			{
				auto it = rt.snapshot.find(animTrack.property);
				if (it != rt.snapshot.end())
					animTrack.Apply(obj, it->second);
			}
		}

	} // namespace ui
} // namespace aq
