#pragma once
#include "../Engine/Application.h"
#include "../Engine/Rendering/Renderer.h"

namespace app
{
	class Application : public engine::IApplication
	{
	public:
		Application();
		virtual ~Application();

		bool Initialize() override;
		void Finalize() override;
		void Update(engine::graphics::RenderContext& context) override;

		void Register() override;


	private:
		void Render(engine::graphics::RenderContext& context);

		engine::rendering::Renderer renderer_;
	};
}