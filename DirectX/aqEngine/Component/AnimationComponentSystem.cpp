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
		}


#ifdef AQ_DEBUG_IMGUI
		void AnimationComponent::DrawInspectorImGui()
		{
			ImGui::DragFloat("Play Speed", &playSpeed_, 0.01f, 0.0f, 10.0f);

			uint32_t removeKey = 0;
			bool     doRemove  = false;
			for (auto& kv : slots_)
			{
				ImGui::PushID(static_cast<int>(kv.first));
				const char* label = kv.second.name.empty() ? "(no name)" : kv.second.name.c_str();
				ImGui::TextUnformatted(label);
				ImGui::SameLine();

				char buf[512];
				strncpy_s(buf, sizeof(buf), kv.second.path.c_str(), _TRUNCATE);
				if (ImGui::InputText("path", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					kv.second.path          = buf;
					kv.second.loadRequested = false;   // 次 Update で再ロード
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) { removeKey = kv.first; doRemove = true; }
				ImGui::PopID();
			}
			if (doRemove) slots_.erase(removeKey);

			// 新規追加
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


		void AnimationComponent::Play(uint32_t nameHash, bool looping)
		{
			auto it = slots_.find(nameHash);
			if (it == slots_.end()) return;
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
