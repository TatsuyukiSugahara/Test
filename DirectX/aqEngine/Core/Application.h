#pragma once
#include "IApplication.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"

namespace engine
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

	private:
		void Render();
	};
}
