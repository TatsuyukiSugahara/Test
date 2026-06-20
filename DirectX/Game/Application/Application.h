#pragma once
#include "aqEngine/Core/Application.h"
#ifdef AQ_DEBUG_IMGUI
#include <memory>
#include "aqEngine/Rendering/Shadow/ShadowDebugPanel.h"
#endif

namespace app
{
	class Application : public engine::Application
	{
	protected:
		aq::rendering::RenderTargetHandle offscreenRTHandle_;

	private:
		static constexpr float kOffscreenRTWidth  = 512.0f;
		static constexpr float kOffscreenRTHeight = 512.0f;

#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<aq::rendering::ShadowDebugPanel> shadowDebugPanel_;
#endif

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;
	};
}
