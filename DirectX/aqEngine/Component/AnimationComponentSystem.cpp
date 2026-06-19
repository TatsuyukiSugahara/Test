#include "aq.h"
#include "AnimationComponentSystem.h"
#include "BodyComponentSystem.h"
#include "Engine.h"
#include "ECS/EntityContext.h"
#include "Resource/Resource.h"


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
			const float deltaTime = engine::Engine::GetDeltaTime();
			aq::ecs::Foreach<SkeletalMeshComponent, AnimationComponent>([deltaTime](const aq::ecs::Entity&, SkeletalMeshComponent* skeletalMeshComponent, AnimationComponent* animationComponent)
				{
					animationComponent->Update(deltaTime, skeletalMeshComponent);
				});
		}
	}
}
