#pragma once
#include "Graphics/RenderContext.h"

namespace engine
{
	/**
	 * アプリケーションを動作させる基本機能
	 */
	class IApplication
	{
	public:
		/**
		 * renderContext はレンダースレッドの初期化にのみ使う。
		 * ゲームロジックは renderContext を直接操作してはならない（RenderThread 専有）。
		 */
		virtual bool Initialize(graphics::RenderContext& renderContext) = 0;
		virtual void Finalize() = 0;

		/**
		 * ゲームロジックの更新。RenderContext は受け取らない。
		 * 描画処理は RenderCommandList に積んで RenderThread::Submit() で行う。
		 */
		virtual void Update() = 0;

		/**
		 * Engine が Update() の直後に呼ぶ。
		 * RenderThread がフレームの描画・CopyToBackBuffer・Present を完了するまでここで待機する。
		 * 完了を待ってから Engine::Update() が次のフレームのループに入るため、
		 * メインスレッドは Submit() 以降 D3D11 immediate context に触れない。
		 *
		 * 1フレーム遅延パイプライン（ダブルバッファ RT が必要）への切り替え方:
		 *   WaitForCompletion() の代わりに RenderThread::WaitForPreviousFrame() を呼ぶ。
		 *   ゲームスレッドがフレーム N を構築しながらレンダースレッドはフレーム N-1 を実行できる。
		 */
		virtual void FlushRender() {}

		/**
		 * 登録用関数
		 * NOTE:Initialize後に呼ばれる
		 */
		virtual void Register() = 0;
	};
}
