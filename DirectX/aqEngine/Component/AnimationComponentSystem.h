#pragma once
#include <map>
#include <string>
#include "ECS/ECS.h"
#include "Graphics/AnimationClip.h"


namespace aq
{
	namespace ecs
	{
		class SkeletalMeshComponent;


		class AnimationComponent : public aq::ecs::IComponent
		{
			ecsComponent(aq::ecs::AnimationComponent);

		public:
			struct AnimationSlot
			{
				std::string                   path;
				aq::res::RefAnimationResource resource;
				aq::graphics::AnimationClip   clip;
				bool                          loadRequested = false;
			};

		private:
			std::map<uint32_t, AnimationSlot> slots_;
			uint32_t                          currentHashKey_;
			float                             currentTime_;
			float                             playSpeed_;
			bool                              isPlaying_;
			bool                              isLooping_;

		public:
			AnimationComponent();
			~AnimationComponent() = default;

			void AddAnimation(uint32_t nameHash, const char* path);

			void Play(uint32_t nameHash, bool looping = true);
			void Stop() { isPlaying_ = false; }

			float GetCurrentTime() const { return currentTime_; }
			float GetPlaySpeed()   const { return playSpeed_; }
			void  SetPlaySpeed(float speed) { playSpeed_ = speed; }

			void Update(float deltaTime, SkeletalMeshComponent* skelMeshComp);
		};


		class AnimationSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
