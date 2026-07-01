#pragma once
#include <map>
#include <string>
#include "ECS/ECS.h"
#include "Graphics/AnimationClip.h"

namespace aq { namespace util { class JsonValue; } }


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
				std::string                   name;   // オーサリング名（serialize/エディタ表示用）
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
			// 名前から hash を計算し、name も保持する（serialize 対応版）。
			void AddAnimation(const char* name, const char* path);

			void Play(uint32_t nameHash, bool looping = true);
			void Stop() { isPlaying_ = false; }

			float GetCurrentTime() const { return currentTime_; }
			float GetPlaySpeed()   const { return playSpeed_; }
			void  SetPlaySpeed(float speed) { playSpeed_ = speed; }

			void Update(float deltaTime, SkeletalMeshComponent* skelMeshComp);

			// JSON シリアライズ（{ playSpeed, clips:[{name,path}] }）。
			// visitor では配列を扱えないため専用実装（.cpp）。
			void SerializeTo(util::JsonValue& out) const;
			void DeserializeFrom(const util::JsonValue& in);
#ifdef AQ_DEBUG_IMGUI
			// Prefab エディタ / Entity インスペクタ共用のクリップ一覧 UI（.cpp）。
			void DrawInspectorImGui();
#endif
		};


		class AnimationSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
