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
		// runtimes_ を clips_ と同じ長さへ作り直し、レイヤーが外れた分を ApplyLayers() で
		// 基準値へ反映する。condition / group の変更は ResetClipRuntime() で対象 runtime だけを
		// 初期化したあと同様に ApplyLayers() を呼ぶ。

		size_t UIAnimationComponent::AddClip(UIAnimationClip clip)
		{
			StopAll();
			clips_.push_back(std::move(clip));
			runtimes_.assign(clips_.size(), ClipRuntime{});
			ApplyLayers();
			return clips_.size() - 1;
		}


		void UIAnimationComponent::RemoveClip(const size_t index)
		{
			if (index >= clips_.size()) return;

			StopAll();
			clips_.erase(clips_.begin() + index);
			runtimes_.assign(clips_.size(), ClipRuntime{});
			ApplyLayers();
		}


		void UIAnimationComponent::MoveClip(const size_t from, const size_t to)
		{
			if (from >= clips_.size() || to >= clips_.size() || from == to) return;

			StopAll();
			UIAnimationClip clip = std::move(clips_[from]);
			clips_.erase(clips_.begin() + from);
			clips_.insert(clips_.begin() + to, std::move(clip));
			runtimes_.assign(clips_.size(), ClipRuntime{});
			ApplyLayers();
		}


		void UIAnimationComponent::ReplaceAllClips(std::vector<UIAnimationClip> clips)
		{
			StopAll();
			clips_ = std::move(clips);
			runtimes_.assign(clips_.size(), ClipRuntime{});
			ApplyLayers();
		}


		void UIAnimationComponent::SetClipCondition(const size_t index, const UIClipCondition condition, const uint32_t param, std::string_view paramName)
		{
			if (index >= clips_.size()) return;

			UIAnimationClip& clip   = clips_[index];
			clip.condition          = condition;
			clip.conditionParam     = param;
			clip.conditionParamName = std::string(paramName);
			ResetClipRuntime(index);
			ApplyLayers();
		}


		void UIAnimationComponent::SetClipGroup(const size_t index, std::string_view groupName)
		{
			if (index >= clips_.size()) return;

			UIAnimationClip& clip = clips_[index];
			clip.groupName = std::string(groupName);
			clip.group     = clip.groupName.empty() ? aqHash32(clip.name.c_str()) : aqHash32(clip.groupName.c_str());
			ResetClipRuntime(index);
			ApplyLayers();
		}


		UIAnimationClip& UIAnimationComponent::EditClip(const size_t index)
		{
			EngineAssert(index < clips_.size());
			return clips_[index];
		}


		// ---- ランタイム API (§5.2) ---------------------------------------------------
		//
		// Play() / Trigger() / SetCondition() は runtime の状態を変えた直後に ApplyLayers() を
		// 呼び、時刻 0 のサンプルをその場で適用する (設計書 §5.3)。Stop() / StopAll() も同様に
		// 呼び、レイヤーが外れた分を基準値へ戻す。

		void UIAnimationComponent::Play(const uint32_t group)
		{
			const uint32_t serial = ++nextSerial_;

			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Manual || clip.group != group) continue;

				ClipRuntime& rt      = runtimes_[i];
				rt.active            = true;
				rt.completed         = false;
				rt.time              = 0.f;
				rt.activationSerial  = serial;
			}

			ApplyLayers();
		}


		void UIAnimationComponent::Stop(const uint32_t group)
		{
			bool changed = false;
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Manual || clip.group != group) continue;

				ClipRuntime& rt = runtimes_[i];
				if (!rt.active) continue;

				rt.active    = false; // 途中値を確定せずレイヤーを外す (Restore 相当)
				rt.completed = false;
				changed = true;
			}

			if (changed) ApplyLayers();
		}


		void UIAnimationComponent::StopAll()
		{
			bool changed = false;
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				if (clips_[i].condition != UIClipCondition::Manual) continue;

				ClipRuntime& rt = runtimes_[i];
				if (!rt.active) continue;

				rt.active    = false;
				rt.completed = false;
				changed = true;
			}

			if (changed) ApplyLayers();
		}


		void UIAnimationComponent::SetCondition(const uint32_t condition, const bool value)
		{
			bool& current = conditions_[condition];
			if (current == value) return; // 値が変わらなければ何もしない (毎フレーム呼ばれても無駄がない)
			current = value;

			// false → true の起動単位は 1 つ。同じ conditionParam を持つ全クリップへ同じ serial を配る
			const uint32_t serial = value ? ++nextSerial_ : 0u;

			bool changed = false;
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Bool || clip.conditionParam != condition) continue;

				ClipRuntime& rt = runtimes_[i];
				if (value)
				{
					rt.active           = true; // false → true: 先頭から再生する新しい起動単位
					rt.completed        = false;
					rt.time             = 0.f;
					rt.activationSerial = serial;
				}
				else
				{
					rt.active    = false; // true → false: finish に関係なくレイヤーを外す
					rt.completed = false;
				}
				changed = true;
			}

			if (changed) ApplyLayers();
		}


		void UIAnimationComponent::Trigger(const uint32_t trigger)
		{
			const uint32_t serial = ++nextSerial_;

			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const UIAnimationClip& clip = clips_[i];
				if (clip.condition != UIClipCondition::Trigger || clip.conditionParam != trigger) continue;

				// 再生中でも即座に先頭から (pending フラグは持たない)
				ClipRuntime& rt      = runtimes_[i];
				rt.active            = true;
				rt.completed         = false;
				rt.time              = 0.f;
				rt.activationSerial  = serial;
			}

			ApplyLayers();
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


		// ---- 毎フレーム更新 (設計書 §6.3) ---------------------------------------------
		//
		// AdvanceRuntimes() で全 runtime の起動判定・時刻・ループ・完了を進め (プロパティには
		// 触らない)、ApplyLayers() でプロパティごとに勝者を選んで 1 回だけ書き込む。

		void UIAnimationComponent::Update(const float dt)
		{
			AdvanceRuntimes(dt);
			ApplyLayers();
		}


		// ---- 状態更新 (§6.3 の 1 段階目) -----------------------------------------------

		void UIAnimationComponent::AdvanceRuntimes(const float dt)
		{
			for (size_t i = 0; i < clips_.size(); ++i)
			{
				UIAnimationClip& clip = clips_[i];
				ClipRuntime&      rt   = runtimes_[i];

				// Bool: 条件と active のズレを拾う (クリップ追加や JSON Load 直後に条件が
				// 既に真 / 偽な場合の保険。通常は SetCondition() が起動 / 解除を行う)
				if (clip.condition == UIClipCondition::Bool)
				{
					const bool conditionValue = GetCondition(clip.conditionParam);
					if (conditionValue && !rt.active)
					{
						rt.active            = true;
						rt.completed         = false;
						rt.time              = 0.f;
						rt.activationSerial  = ++nextSerial_;
					}
					else if (!conditionValue && rt.active)
					{
						rt.active    = false;
						rt.completed = false;
					}
				}

				if (!rt.active || rt.completed) continue; // completed は最終値を保持したまま時刻を進めない

				rt.time += dt;

				// --- ループ処理 ---
				if (clip.loopFrom >= 0.f && rt.time >= clip.duration)
				{
					float loopDuration = clip.duration - clip.loopFrom;
					if (loopDuration <= 0.f) { loopDuration = clip.duration; }
					const float excess = rt.time - clip.duration;
					rt.time = clip.loopFrom + std::fmod(excess, loopDuration);
					continue; // ループ中は完了しない
				}

				// --- 非ループの完了判定 ---
				if (clip.loopFrom < 0.f && rt.time >= clip.duration)
				{
					if (clip.condition == UIClipCondition::Bool)
					{
						// 最終値を保持したままレイヤーに残る。再スタートしない
						rt.completed = true;
						rt.time      = clip.duration;
					}
					else
					{
						// Manual / Trigger: Hold なら基準値を確定してからレイヤーを外す
						if (clip.finish == UIAnimationFinishMode::Hold)
							CommitHold(i);
						rt.active = false;
					}
				}
			}
		}


		// ---- 適用 (§6.3 の 2 段階目) ---------------------------------------------------

		void UIAnimationComponent::ApplyLayers()
		{
			UIObject* obj = GetOwner();
			if (!obj) return;

			// 1. プロパティごとに勝者 (serial 最大。同 serial は clips_ の後ろが勝つ) を選ぶ
			struct Winner
			{
				size_t clipIndex;
				float  sampleTime;
			};
			std::unordered_map<UIAnimatedProperty, Winner> winners;

			for (size_t i = 0; i < clips_.size(); ++i)
			{
				const ClipRuntime& rt = runtimes_[i];
				if (!rt.active) continue;

				const UIAnimationClip& clip       = clips_[i];
				const float             sampleTime = rt.completed ? clip.duration : rt.time;

				for (const auto& track : clip.tracks)
				{
					const auto it = winners.find(track.property);
					if (it != winners.end() && rt.activationSerial < runtimes_[it->second.clipIndex].activationSerial)
						continue; // 既存の勝者の方が新しい

					winners[track.property] = Winner{ i, sampleTime };
				}
			}

			// 2. 新しく勝者が付いたプロパティは、書き込む前に現在値を基準値として取得する
			for (const auto& [property, winner] : winners)
			{
				if (baseValues_.find(property) != baseValues_.end()) continue;

				for (const auto& track : clips_[winner.clipIndex].tracks)
				{
					if (track.property != property) continue;
					baseValues_[property] = track.ReadFrom(obj);
					break;
				}
			}

			// 3. 基準値はあるが今フレームは勝者が無いプロパティ → 基準値へ戻してキャッシュを破棄する
			for (auto it = baseValues_.begin(); it != baseValues_.end(); )
			{
				if (winners.find(it->first) != winners.end())
				{
					++it;
					continue;
				}

				// Track を持つクリップが既に無くても (RemoveClip 直後など) 基準値は必ず書き戻す
				UIAnimationTrack writer;
				writer.property = it->first;
				writer.Apply(obj, it->second);

				it = baseValues_.erase(it);
			}

			// 4. 勝者の値を 1 回だけ書き込む
			for (const auto& [property, winner] : winners)
			{
				for (const auto& track : clips_[winner.clipIndex].tracks)
				{
					if (track.property != property) continue;
					track.Apply(obj, track.Sample(winner.sampleTime));
					break;
				}
			}
		}


		// ---- プライベートヘルパー -----------------------------------------------------

		void UIAnimationComponent::ResetClipRuntime(const size_t index)
		{
			if (index >= runtimes_.size()) return;
			runtimes_[index] = ClipRuntime{};
		}


		void UIAnimationComponent::CommitHold(const size_t index)
		{
			const UIAnimationClip& clip = clips_[index];
			for (const auto& track : clip.tracks)
				baseValues_[track.property] = track.Sample(clip.duration);
		}

	} // namespace ui
} // namespace aq
