#include "ActorComponentSystem.h"


namespace app
{
	namespace ecs
	{
		ActorStateMachineSystem::ActorStateMachineSystem()
		{
		}


		ActorStateMachineSystem::~ActorStateMachineSystem()
		{
		}


		void ActorStateMachineSystem::Update()
		{
			engine::ecs::Foreach<StateMachineComponent>([](StateMachineComponent* component)
				{
					component->GetStateMachine()->Update();
				});
		}
	}
}