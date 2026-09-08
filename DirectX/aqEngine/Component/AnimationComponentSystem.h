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

			/** クロスフェード (切替元クリップの凍結ポーズと合成する。実行時のみ) */
			uint32_t                          blendFromHashKey_;    // 0 = フェードなし
			float                             blendFromTime_;       // 切替時点で凍結した再生時刻
			float                             blendRemaining_;      // 残りフェード時間 [s]
			float                             blendDuration_;       // フェード全長 [s]
#ifdef AQ_DEBUG_IMGUI
			// エディタのクリップ候補表示用キャッシュ (モデルパスが変わった時だけ再列挙)。
			std::string                       clipListModel_;
			std::vector<std::string>          clipList_;
#endif

		public:
			AnimationComponent();
			~AnimationComponent() = default;

			void AddAnimation(uint32_t nameHash, const char* path);
			// 名前から hash を計算し、name も保持する（serialize 対応版）。
			void AddAnimation(const char* name, const char* path);

			/**
			 * 再生を開始する。別クリップからの切替時は blendSec の間、切替元の
			 * 凍結ポーズとクロスフェードして合成する (0 で即時切替)。
			 */
			void Play(uint32_t nameHash, bool looping = true, float blendSec = 0.15f);
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
			// fbxModelPath が .fbx の場合、その中のクリップ名を候補表示して選ぶだけで登録できる。
			void DrawInspectorImGui(const std::string& fbxModelPath = "");
#endif
		};


		class AnimationSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
