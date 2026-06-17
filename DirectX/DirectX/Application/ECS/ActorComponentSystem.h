#pragma once
#include "Utility.h"
#include "ECS/ECS.h"
#include "Actor/StateMachine.h"


namespace app
{
	namespace ecs
	{
		/**
		 * ステートマシン機能
		 */
		class StateMachineComponent : public aq::ecs::IComponent
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
		class ActorStateMachineSystem : public aq::ecs::SystemBase
		{
		public:
			ActorStateMachineSystem();
			~ActorStateMachineSystem();

			void Update() override;
		};
	}
}
