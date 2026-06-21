#pragma once
#include "aqEngine/Core/Application.h"

namespace app
{
	class Application : public aq::Application
	{
	protected:
		aq::rendering::RenderTargetHandle offscreenRTHandle_;

	private:
		static constexpr float kOffscreenRTWidth  = 512.0f;
		static constexpr float kOffscreenRTHeight = 512.0f;

	protected:
		bool OnInitialize() override;
		void OnFinalize() override;
		void OnUpdate() override;
		void OnRegister() override;
		void OnPreRender() override;
	};
}
