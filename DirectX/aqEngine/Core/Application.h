#pragma once
#include "IApplication.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"
#include "Rendering/Occlusion/HiZRenderer.h"
#include <memory>
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "Rendering/Debug/LightingDebugPanel.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "UI/Debug/UIEditorDebugPanel.h"
#include "UI/Debug/TextStyleEditorPanel.h"
#include "UI/Debug/UIAnimationEditor.h"
#include "Util/Debug/ProfilerDebugPanel.h"
#include "ECS/PrefabEditor.h"
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
		std::unique_ptr<aq::rendering::HiZRenderer> hiZRenderer_;  // オクリュージョン基盤 (Hi-Z)

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
#ifdef AQ_IMGUI
		/** ImGui::NewFrame() 直後に呼ばれる。ゲーム固有の ImGui ウィンドウをここで構築する */
		virtual void OnImGuiRender() {}
#endif
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
		std::unique_ptr<aq::ui::UIEditorDebugPanel>         uiEditorDebugPanel_;
		std::unique_ptr<aq::ui::TextStyleEditorPanel>       textStyleEditorPanel_;
		std::unique_ptr<aq::ui::UIAnimationEditor>          uiAnimationEditor_;
		std::unique_ptr<aq::profile::ProfilerDebugPanel>    profilerDebugPanel_;
		std::unique_ptr<aq::ecs::PrefabEditorPanel>         prefabEditorPanel_;   // 全シーン共通の Prefab エディタ
#endif
	};
}
