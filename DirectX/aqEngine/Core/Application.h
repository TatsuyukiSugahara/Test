#pragma once
#include "IApplication.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/RenderingDebugPanel.h"
#include "Ocean/OceanDebugPanel.h"
#include <memory>
#endif

namespace aq
{
	/**
	 * エンジンサブシステムの初期化・更新・終了を担うアプリケーション基底クラス。
	 * ゲーム側は OnInitialize / OnFinalize / OnUpdate / OnRegister / OnPreRender を override する。
	 */
	class Application : public IApplication
	{
	protected:
		aq::rendering::Renderer     renderer_;
		aq::rendering::RenderThread renderThread_;

	private:
		bool renderThreadReady_ = false;

	public:
		bool Initialize(aq::graphics::RenderContext& renderContext) override;
		void Finalize() override;
		void Update() override;
		void FlushRender() override;
		void Register() override;

	protected:
		/** ゲーム固有の初期化（エンジンサブシステム初期化後に呼ばれる） */
		virtual bool OnInitialize() { return true; }
		/** ゲーム固有の終了処理（エンジンサブシステム終了前に呼ばれる） */
		virtual void OnFinalize() {}
		/** ゲーム固有の更新（Input/ECS/ResourceManager 更新後に呼ばれる） */
		virtual void OnUpdate() {}
		/** ゲーム固有のリソース・システム登録 */
		virtual void OnRegister() {}
		/** メインパス Submit 前に呼ばれる（オフスクリーンパス等を Submit する） */
		virtual void OnPreRender() {}
#ifdef AQ_DEBUG_IMGUI
		/** メインメニューバー内にゲーム固有のメニュー項目を追加する */
		virtual void OnDebugRenderMenu() {}
		/** ImGui ウィンドウ構築（EntityContext::DebugRender() の後に呼ばれる） */
		virtual void OnDebugRender() {}
#endif

	private:
		void Render();

#ifdef AQ_IMGUI
		bool imguiReady_ = false;
#endif
#ifdef AQ_DEBUG_IMGUI
		bool                                                showDebugUI_ = true;
		std::unique_ptr<aq::ocean::OceanDebugPanel>         oceanDebugPanel_;
		std::unique_ptr<aq::rendering::RenderingDebugPanel> renderingDebugPanel_;
#endif
	};
}
