#pragma once
#include "../Utility.h"
#include "../../Engine/ECS/ECS.h"


namespace app
{
	namespace ecs
	{
		class CharacterSteeringComponent : public engine::ecs::IComponent
		{
			ecsComponent(app::ecs::CharacterSteeringComponent);

		private:
			engine::ecs::EntityHandle targetHandle_;


		public:
			CharacterSteeringComponent() {}
			virtual~CharacterSteeringComponent() {}

			inline void SetTarget(const engine::ecs::EntityHandle target)
			{
				targetHandle_ = target;
			}
			inline const engine::ecs::EntityHandle& GetTarget() const
			{
				return targetHandle_;
			}
		};




		class CharacterSteeringSystem : public engine::ecs::SystemBase
		{
		public:
			CharacterSteeringSystem();
			virtual	~CharacterSteeringSystem();

			void Update() override;
		};
	}
}