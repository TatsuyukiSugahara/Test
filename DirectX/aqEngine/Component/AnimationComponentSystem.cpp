#include "aq.h"
#include "AnimationComponentSystem.h"
#include "ECS/EntityContext.h"
#include "Util/SimpleJson.h"
#ifdef AQ_DEBUG_IMGUI
#include <imgui/imgui.h>
#endif


namespace aq
{
	namespace ecs
	{
		AnimationComponent::AnimationComponent()
			: currentHashKey_(0)
			, currentTime_(0.0f)
			, playSpeed_(1.0f)
			, isPlaying_(false)
			, isLooping_(true)
			, blendFromHashKey_(0)
			, blendFromTime_(0.0f)
			, blendRemaining_(0.0f)
			, blendDuration_(0.0f)
		{
		}


		void AnimationComponent::AddAnimation(uint32_t nameHash, const char* path)
		{
			AnimationSlot slot;
			slot.path          = path ? path : "";
			slot.loadRequested = false;
			slots_.emplace(nameHash, std::move(slot));
		}


		void AnimationComponent::AddAnimation(const char* name, const char* path)
		{
			const uint32_t nameHash = aqHash32(name ? name : "");
			AnimationSlot slot;
			slot.name          = name ? name : "";
			slot.path          = path ? path : "";
			slot.loadRequested = false;
			slots_[nameHash]   = std::move(slot);   // 同名は上書き
		}


		void AnimationComponent::SerializeTo(util::JsonValue& out) const
		{
			out = util::JsonValue::MakeObject();
			out.Set("playSpeed", util::JsonValue(static_cast<double>(playSpeed_)));

			util::JsonValue clips = util::JsonValue::MakeArray();
			for (const auto& kv : slots_)
			{
				util::JsonValue c = util::JsonValue::MakeObject();
				c.Set("name", util::JsonValue(kv.second.name));
				c.Set("path", util::JsonValue(kv.second.path));
				clips.PushBack(std::move(c));
			}
			out.Set("clips", std::move(clips));

			// 再生中クリップ名を保存 (ロード時に自動再生させる)
			if (isPlaying_ && currentHashKey_ != 0)
			{
				const auto it = slots_.find(currentHashKey_);
				if (it != slots_.end() && !it->second.name.empty())
					out.Set("current", util::JsonValue(it->second.name));
			}
		}


		void AnimationComponent::DeserializeFrom(const util::JsonValue& in)
		{
			slots_.clear();
			if (in.Contains("playSpeed")) playSpeed_ = in["playSpeed"].AsFloat(playSpeed_);

			const util::JsonValue& clips = in["clips"];
			for (size_t i = 0; i < clips.Size(); ++i)
			{
				const util::JsonValue& c = clips[i];
				AddAnimation(c["name"].AsString().c_str(), c["path"].AsString().c_str());
			}

			// 保存された再生中クリップをループ再生する
			if (in.Contains("current"))
			{
				const std::string cur = in["current"].AsString();
				if (!cur.empty()) Play(aqHash32(cur.c_str()), true);
			}
		}


#ifdef AQ_DEBUG_IMGUI
		void AnimationComponent::DrawInspectorImGui(const std::string& fbxModelPath)
		{
			ImGui::DragFloat("Play Speed", &playSpeed_, 0.01f, 0.0f, 10.0f);

			// --- FBX 内クリップの候補表示 (SkeletalMesh のモデルが .fbx のとき) ---
			// 拡張子を大文字小文字問わず判定 (ASCII 前提で |32 して小文字化)。
			bool isFbx = false;
			if (fbxModelPath.size() >= 4)
			{
				const char* e = fbxModelPath.c_str() + fbxModelPath.size() - 4;
				isFbx = e[0] == '.' && (e[1] | 32) == 'f' && (e[2] | 32) == 'b' && (e[3] | 32) == 'x';
			}
			if (isFbx)
			{
				if (fbxModelPath != clipListModel_)   // モデルが変わった時だけ再列挙 (キャッシュ)
				{
					clipListModel_ = fbxModelPath;
					aq::res::GetFbxAnimationClipNames(fbxModelPath, clipList_);
				}

				ImGui::Separator();
				ImGui::TextUnformatted("FBX clips:");
				for (const std::string& clip : clipList_)
				{
					const uint32_t h      = aqHash32(clip.c_str());
					const bool     exists = slots_.find(h) != slots_.end();
					ImGui::PushID(clip.c_str());
					if (ImGui::SmallButton(exists ? "added" : "add"))
					{
						if (!exists) AddAnimation(clip.c_str(), (fbxModelPath + "#" + clip).c_str());
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("play"))   // 未登録なら登録してから再生
					{
						if (!exists) AddAnimation(clip.c_str(), (fbxModelPath + "#" + clip).c_str());
						Play(h, true);
					}
					ImGui::SameLine();
					ImGui::TextUnformatted(clip.c_str());
					ImGui::PopID();
				}
				if (clipList_.empty())
					ImGui::TextDisabled("(no animations in this FBX)");
			}

			// --- 登録済みクリップ一覧 ---
			ImGui::Separator();
			uint32_t removeKey = 0;
			bool     doRemove  = false;
			for (auto& kv : slots_)
			{
				ImGui::PushID(static_cast<int>(kv.first));
				const bool  playing = (kv.first == currentHashKey_ && isPlaying_);
				const char* label   = kv.second.name.empty() ? "(no name)" : kv.second.name.c_str();
				if (ImGui::SmallButton(playing ? "stop" : "play"))
				{
					if (playing) Stop(); else Play(kv.first, true);
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(label);
				ImGui::SameLine();

				char buf[512];
				// snprintf は切り詰めても必ず NUL 終端する。
				snprintf(buf, sizeof(buf), "%s", kv.second.path.c_str());
				ImGui::SetNextItemWidth(220.0f);
				if (ImGui::InputText("##path", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					kv.second.path          = buf;
					kv.second.loadRequested = false;   // 次 Update で再ロード
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) { removeKey = kv.first; doRemove = true; }
				ImGui::PopID();
			}
			if (doRemove) slots_.erase(removeKey);

			// --- 手動追加 (パス直接入力) ---
			static char newName[64] = "";
			static char newPath[512] = "";
			ImGui::Separator();
			ImGui::InputText("name##add", newName, sizeof(newName));
			ImGui::InputText("path##add", newPath, sizeof(newPath));
			if (ImGui::Button("+ Add Clip") && newName[0] != '\0')
			{
				AddAnimation(newName, newPath);
				newName[0] = '\0';
				newPath[0] = '\0';
			}
		}
#endif


		void AnimationComponent::Play(uint32_t nameHash, bool looping, float blendSec)
		{
			auto it = slots_.find(nameHash);
			if (it == slots_.end()) return;

			// 別クリップからの切替なら、切替元のポーズを凍結してクロスフェードする
			// (再生停止後の最終フレームからの切替も同様に滑らかになる)。
			if (blendSec > 0.0f && currentHashKey_ != 0 && currentHashKey_ != nameHash
			    && slots_.find(currentHashKey_) != slots_.end())
			{
				blendFromHashKey_ = currentHashKey_;
				blendFromTime_    = currentTime_;
				blendRemaining_   = blendSec;
				blendDuration_    = blendSec;
			}

			currentHashKey_ = nameHash;
			isLooping_      = looping;
			isPlaying_      = true;
			currentTime_    = 0.0f;
		}


		void AnimationComponent::Update(float deltaTime, SkeletalMeshComponent* skelMeshComp)
		{
			auto it = slots_.find(currentHashKey_);
			if (it == slots_.end()) return;
			AnimationSlot& slot = it->second;

			if (!slot.loadRequested && !slot.path.empty()) {
				slot.resource      = aq::res::ResourceManager::Get().Load<aq::res::AnimationResource>(slot.path.c_str());
				slot.clip.Initialize(slot.resource);
				slot.loadRequested = true;
			}

			if (!slot.clip.IsLoaded()) return;
			if (!skelMeshComp || !skelMeshComp->IsCompleted()) return;

			const auto* bones = skelMeshComp->GetSkeletalMesh()->GetBones();
			if (!bones || bones->empty()) return;

			if (isPlaying_) {
				currentTime_ += deltaTime * playSpeed_;
				const float duration = slot.clip.GetDuration();
				if (duration > 0.0f) {
					if (isLooping_) {
						while (currentTime_ >= duration) currentTime_ -= duration;
					} else {
						if (currentTime_ >= duration) {
							currentTime_ = duration;
							isPlaying_   = false;
						}
					}
				}
			}

			auto boneMatrices = std::make_shared<std::vector<aq::math::Matrix4x4>>();
			slot.clip.CalcBoneMatrices(currentTime_, *bones, *boneMatrices);

			// クロスフェード中は切替元の凍結ポーズとスキニング行列を線形補間する。
			// ローカルポーズの球面補間ではない近似だが、0.1〜0.2 秒の短いフェードでは
			// 破綻が見えず、クリップ側の変更なしで済む。
			if (blendRemaining_ > 0.0f)
			{
				blendRemaining_ -= deltaTime;
				auto fromIt = slots_.find(blendFromHashKey_);
				if (blendRemaining_ > 0.0f && fromIt != slots_.end() && fromIt->second.clip.IsLoaded())
				{
					const float fromWeight = blendRemaining_ / blendDuration_;
					std::vector<aq::math::Matrix4x4> fromMatrices;
					fromIt->second.clip.CalcBoneMatrices(blendFromTime_, *bones, fromMatrices);
					if (fromMatrices.size() == boneMatrices->size())
					{
						for (size_t i = 0; i < boneMatrices->size(); ++i)
						{
							float*       dst = &(*boneMatrices)[i]._11;
							const float* src = &fromMatrices[i]._11;
							for (int e = 0; e < 16; ++e)
							{
								dst[e] += (src[e] - dst[e]) * fromWeight;
							}
						}
					}
				}
				if (blendRemaining_ <= 0.0f) { blendFromHashKey_ = 0; }
			}

			skelMeshComp->GetSkeletalMesh()->SetBoneMatrices(boneMatrices);
		}


		void AnimationSystem::Update()
		{
			const float deltaTime = aq::Engine::GetDeltaTime();
			aq::ecs::Foreach<SkeletalMeshComponent, AnimationComponent>([deltaTime](const aq::ecs::Entity&, SkeletalMeshComponent* skeletalMeshComponent, AnimationComponent* animationComponent)
				{
					animationComponent->Update(deltaTime, skeletalMeshComponent);
				});
		}
	}
}
