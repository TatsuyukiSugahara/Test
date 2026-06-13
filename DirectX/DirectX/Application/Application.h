#pragma once
#include "../Engine/Application.h"
#include "../Engine/Rendering/Renderer.h"
#include "../Engine/Rendering/RenderThread.h"

namespace app
{
	class Application : public engine::IApplication
	{
	public:
		Application();
		virtual ~Application();

		bool Initialize(engine::graphics::RenderContext& renderContext) override;
		void Finalize() override;

		/**
		 * ゲームロジックの更新。RenderContext は受け取らない。
		 * 描画は Render() → RenderThread::Submit() 経由で行う。
		 */
		void Update() override;

		/**
		 * レンダースレッドの完了を待ちバックバッファへの Present 後に復帰する。
		 * Engine::Update() が Update() の直後に呼び出す。
		 */
		void FlushRender() override;

		void Register() override;


	private:
		void Render();

		engine::rendering::Renderer       renderer_;
		engine::rendering::RenderThread   renderThread_;
		bool                              renderThreadReady_ = false;

		engine::rendering::RenderTargetHandle offscreenRTHandle_;
		static constexpr float kOffscreenRTWidth  = 512.0f;
		static constexpr float kOffscreenRTHeight = 512.0f;
	};
}
