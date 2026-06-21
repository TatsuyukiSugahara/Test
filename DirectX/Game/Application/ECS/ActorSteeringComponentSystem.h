#pragma once
#include "Utility.h"
#include "ECS/ECS.h"


namespace app
{
	namespace ecs
	{
		class CharacterSteeringComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::CharacterSteeringComponent);

		private:
			aq::ecs::EntityHandle targetHandle_;


		public:
			CharacterSteeringComponent() {}
			virtual~CharacterSteeringComponent() {}

			inline void SetTarget(const aq::ecs::EntityHandle target)
			{
				targetHandle_ = target;
			}
			inline const aq::ecs::EntityHandle& GetTarget() const
			{
				return targetHandle_;
			}
		};




		class CharacterSteeringSystem : public aq::ecs::SystemBase
		{
		public:
			CharacterSteeringSystem();
			virtual	~CharacterSteeringSystem();

			void Update() override;
#ifdef AQ_DEBUG_IMGUI
			const char* GetDebugGroup()    const override { return "Character"; }
			const char* GetDebugTabLabel() const override { return "Character"; }
			void        RenderContent()          override;
#endif

		private:
			float speed_ = 0.01f;
		};
	}
}
