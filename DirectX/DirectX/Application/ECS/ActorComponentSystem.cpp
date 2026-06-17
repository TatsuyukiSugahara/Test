#include "stdafx.h"
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
			aq::ecs::Foreach<StateMachineComponent>([](const aq::ecs::Entity& entity, StateMachineComponent* component)
				{
					component->GetStateMachine()->Update();
				});
		}
	}
}