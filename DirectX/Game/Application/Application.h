#pragma once
#include "aqEngine/Core/Application.h"
#include <memory>
#include "aqEngine/Rendering/PostProcess/BloomRenderer.h"
#ifdef AQ_DEBUG_IMGUI
#include "aqEngine/Rendering/Shadow/ShadowDebugPanel.h"
#include "aqEngine/Rendering/PostProcess/BloomDebugPanel.h"
#include "aqEngine/Ocean/OceanDebugPanel.h"
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

		aq::rendering::BloomRenderer* bloomRenderer_ = nullptr; // renderer_ が所有、ここは非所有参照

#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<aq::rendering::ShadowDebugPanel> shadowDebugPanel_;
		std::unique_ptr<aq::rendering::BloomDebugPanel>  bloomDebugPanel_;
		std::unique_ptr<aq::ocean::OceanDebugPanel>      oceanDebugPanel_;
#endif

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;
	};
}
