#include "aq.h"
#include "UIAnimationComponent.h"
#include "UI/UIObject.h"
#include <cmath>

namespace aq
{
	namespace ui
	{
		// ---- 編集 API (§5.1) --------------------------------------------------------
		//
		// Clip 追加 / 削除 / 並べ替え / 全置換は再構築契約どおり StopAll() のあと
		// runtimes_ を clips_ と同じ長さへ作り直す。condition / group の変更は
		// ResetClipRuntime() で対象 runtime だけを初期化する。

		size_t UIAnimationComponent::AddClip(UIAnimationClip clip)
		{
			StopAll();
			clips_.push_back(std::move(clip));
			runtimes_.assign(clips_.size(), ClipRuntime{});
			return clips_.size() - 1;
		}

		void UIAnimationComponent::RemoveClip(const size_t index)
		{
			if (index >= clips_.size()) return;

			StopAll();
			clips_.erase(clips_.begin() + index);
			runtimes_.assign(clips_.size(), ClipRuntime{});
		}

		void UIAnimationComponent::MoveClip(const size_t from, const size_t to)
		{
			if (from >= clips_.size() || to >= clips_.size() || from == to) return;

			StopAll();
			UIAnimationClip clip = std::move(clips_[from]);
			clips_.erase(clips_.begin() + from);
			clips_.insert(clips_.begin() + to, std::move(clip));
			runtimes_.assign(clips_.size(), ClipRuntime{});
		}

		void UIAnimationComponent::ReplaceAllClips(std::vector<UIAnimationClip> clips)
		{
			StopAll();
			clips_ = std::move(clips);
			runtimes_.assign(clips_.size(), ClipRuntime{});
		}

		void UIAnimationComponent::SetClipCondition(const size_t index, const UIClipCondition condition, const uint32_t param, std::string_view paramName)
		{
			if (index >= clips_.size()) return;

			UIAnimationClip& clip   = clips_[index];
			clip.condition          = condition;
			clip.conditionParam     = param;
			clip.conditionParamName = std::string(paramName);
			ResetClipRuntime(index);
		}

		void UIAnimationComponent::SetClipGroup(const size_t index, std::string_view groupName)
		{
			if (index >= clips_.size()) return;

			UIAnimationClip& clip = clips_[index];
			clip.groupName = std::string(groupName);
			clip.group     = clip.groupName.empty() ? aqHash32(clip.name.c_str()) : aqHash32(clip.groupName.c_str());
			ResetClipRuntime(index);
		}

		UIAnimationClip& UIAnimationComponent::EditClip(const size_t index)
		{
			EngineAssert(index < clips_.size());
			return clips_[index];
		}


		// ---- ランタイム API (§5.2) ---------------------------------------------------

		void UIAnimationComponent::Play(const uint32_t group)
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Manual || clip.group != group) continue;

				ClipRuntime& rt = runtimes_[i];
				rt.active = true;
				rt.time   = 0.f;
				TakeSnapshot(i);
			}
		}

		void UIAnimationComponent::Stop(const uint32_t group)
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Manual || clip.group != group) continue;

				ClipRuntime& rt = runtimes_[i];
				if (!rt.active) continue;

				RestoreSnapshot(i); // 途中値を確定せずレイヤーを外す (Restore 相当)
				rt.active = false;
				rt.time   = 0.f;
			}
		}

		void UIAnimationComponent::StopAll()
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				if (clips_[i].condition != UIClipCondition::Manual) continue;

				ClipRuntime& rt = runtimes_[i];
				if (!rt.active) continue;

				RestoreSnapshot(i);
				rt.active = false;
				rt.time   = 0.f;
			}
		}

		void UIAnimationComponent::SetCondition(const uint32_t condition, const bool value)
		{
			conditions_[condition] = value;
		}

		void UIAnimationComponent::Trigger(const uint32_t trigger)
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition == UIClipCondition::Trigger && clip.conditionParam == trigger)
					runtimes_[i].triggered = true;
			}
		}

		bool UIAnimationComponent::GetCondition(const uint32_t condition) const
		{
			auto it = conditions_.find(condition);
			return it != conditions_.end() && it->second;
		}

		bool UIAnimationComponent::IsGroupPlaying(const uint32_t group) const
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				if (clips_[i].condition == UIClipCondition::Manual && clips_[i].group == group && runtimes_[i].active)
					return true;
			}
			return false;
		}

		bool UIAnimationComponent::IsPlaying() const
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				if (clips_[i].condition == UIClipCondition::Manual && runtimes_[i].active)
					return true;
			}
			return false;
		}


		// ---- 毎フレーム更新 (P1 暫定版。設計書 §11 の P1 補足) -------------------------
		//
		// 旧 Update() のロジックをクリップ単位に写した実装。レイヤー合成・activationSerial・
		// 基準値のライフサイクル (§6) は P2 で実装するため、ここでは持たない。

		void UIAnimationComponent::Update(const float dt)
		{
			UIObject* obj = GetOwner();
			if (!obj) return;

			for (size_t i = 0; i < clips_.size(); ++i)
			{
				UIAnimationClip& clip = clips_[i];
				ClipRuntime&      rt   = runtimes_[i];

				// --- アクティブ条件を評価 (Manual は Play() / Stop() が直接 active を操作する) ---
				bool shouldBeActive = false;
				switch (clip.condition)
				{
					case UIClipCondition::Manual:
						shouldBeActive = rt.active;
						break;
					case UIClipCondition::Bool:
						shouldBeActive = GetCondition(clip.conditionParam);
						break;
					case UIClipCondition::Trigger:
						// triggered フラグが立っていれば一回だけ起動 (完了後の再発火も可)
						if (rt.triggered && !rt.active)
						{
							shouldBeActive = true;
							rt.triggered   = false;
							TakeSnapshot(i); // スナップショットはトリガー起動時に取得
						}
						else
						{
							shouldBeActive = rt.active;
						}
						break;
				}

				// アクティブ → 非アクティブ (Bool が false に戻った。finish に関係なく戻す)
				if (rt.active && !shouldBeActive)
				{
					RestoreSnapshot(i);
					rt.active = false;
					rt.time   = 0.f;
					continue;
				}

				// 非アクティブ → アクティブ (Bool が true になった。Manual / Trigger は上で処理済み)
				if (!rt.active && shouldBeActive)
				{
					rt.active = true;
					rt.time   = 0.f;
					if (clip.condition == UIClipCondition::Bool)
						TakeSnapshot(i); // Bool はここでスナップショット (Trigger は起動決定時に取得済み)
				}

				if (!rt.active) continue;

				rt.time += dt;

				// --- ループ処理 ---
				float sampleTime = rt.time;
				if (clip.loopFrom >= 0.f && rt.time >= clip.duration)
				{
					float loopDuration = clip.duration - clip.loopFrom;
					if (loopDuration <= 0.f) { loopDuration = clip.duration; }
					const float excess = rt.time - clip.duration;
					rt.time    = clip.loopFrom + std::fmod(excess, loopDuration);
					sampleTime = rt.time;
				}

				// --- 終了判定 (ループなしのみ) ---
				const bool clipComplete = (clip.loopFrom < 0.f && rt.time >= clip.duration);
				if (clipComplete)
				{
					sampleTime = clip.duration;
					rt.active  = false;

					// finish == Restore: スナップショットへ戻す。Hold はこのあとの Apply で最終値を残す
					if (clip.finish == UIAnimationFinishMode::Restore)
					{
						RestoreSnapshot(i);
						continue;
					}
				}

				// --- プロパティ適用 ---
				for (const auto& track : clip.tracks)
				{
					const float value = track.Sample(sampleTime);
					track.Apply(obj, value);
				}
			}
		}


		// ---- プライベートヘルパー -----------------------------------------------------

		void UIAnimationComponent::ResetClipRuntime(const size_t index)
		{
			if (index >= runtimes_.size()) return;
			runtimes_[index] = ClipRuntime{};
		}

		void UIAnimationComponent::TakeSnapshot(const size_t index)
		{
			const UIObject* obj = GetOwner();
			if (!obj) return;

			ClipRuntime& rt = runtimes_[index];
			rt.snapshot.clear();
			for (const auto& track : clips_[index].tracks)
				rt.snapshot[track.property] = track.ReadFrom(obj);
		}

		void UIAnimationComponent::RestoreSnapshot(const size_t index)
		{
			UIObject* obj = GetOwner();
			if (!obj) return;

			const ClipRuntime& rt = runtimes_[index];
			for (const auto& track : clips_[index].tracks)
			{
				auto it = rt.snapshot.find(track.property);
				if (it != rt.snapshot.end())
					track.Apply(obj, it->second);
			}
		}

	} // namespace ui
} // namespace aq
