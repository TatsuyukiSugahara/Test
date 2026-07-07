#include "aq.h"
#include "ParticleSystem.h"
#include "ECS/ECS.h"
#include "Particle/ParticleEmitterComponent.h"
#include "Component/HierarchicalTransformComponent.h"


namespace aq
{
	namespace ecs
	{
		void ParticleSystem::Update()
		{
			const float deltaTime = aq::Engine::GetDeltaTime();
			aq::ecs::Foreach<ParticleEmitterComponent, HierarchicalTransformComponent>(
				[deltaTime](const aq::ecs::Entity&,
				            ParticleEmitterComponent*         emitter,
				            HierarchicalTransformComponent*   htc)
				{
					emitter->Simulate(deltaTime, htc->transform.position);
				});
		}
	}
}
