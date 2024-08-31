#include "../EnginePreCompile.h"
#include "../Engine.h"
#include "Component.h"

namespace engine
{
	namespace component
	{
		void ComponentManager::Start()
		{
			for (auto* component : components_) {
				component->Start();
			}
		}


		void ComponentManager::Update()
		{
			for (auto* component : components_) {
				component->Update();
			}
		}


		void ComponentManager::Render(graphics::RenderContext& context)
		{
			for (auto* component : components_) {
				component->Render(context);
			}
		}


		void ComponentManager::PreUpdate()
		{
			for (auto* component : components_) {
				component->PreUpdate();
			}
		}


		void ComponentManager::PostUpdate()
		{
			for (auto* component : components_) {
				component->PostUpdate();
			}
		}


		void ComponentManager::PreRender(graphics::RenderContext& context)
		{
			for (auto* component : components_) {
				component->PreRender(context);
			}
		}


		void ComponentManager::PostRender(graphics::RenderContext& context)
		{
			for (auto* component : components_) {
				component->PostRender(context);
			}
		}
	}
}