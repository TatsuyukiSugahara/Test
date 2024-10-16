#pragma once
#include "../Utility.h"
#include "../../Engine/ECS/ECS.h"
#include "../Actor/StateMachine.h"


namespace app
{
	namespace ecs
	{
		/**
		 * ステートマシン機能
		 */
		class StateMachineComponent : public engine::ecs::IComponent
		{
			ecsComponent(app::ecs::StateMachineComponent);

		private:
			app::actor::StateMachine stateMachine_;


		public:
			StateMachineComponent() {}
			~StateMachineComponent() {}


		public:
			app::actor::StateMachine* GetStateMachine() { return &stateMachine_; }
		};




		/**
		 * ステートマシン機能用のシステム
		 */
		class ActorStateMachineSystem : public engine::ecs::SystemBase
		{
		public:
			ActorStateMachineSystem();
			~ActorStateMachineSystem();

			void Update() override;
		};
	}
}