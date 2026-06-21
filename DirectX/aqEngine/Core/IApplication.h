#pragma once
#include "Graphics/RenderContext.h"

namespace aq
{
	/**
	 * アプリケーションを動作させる基本機能
	 */
	class IApplication
	{
	public:
		virtual bool Initialize(aq::graphics::RenderContext& renderContext) = 0;
		virtual void Finalize() = 0;

		/**
		 * ゲームロジックの更新。RenderContext は受け取らない。
		 * 描画処理は RenderCommandList に積んで RenderThread::Submit() で行う。
		 */
		virtual void Update() = 0;

		/**
		 * Engine が Update() の直後に呼ぶ。
		 * RenderThread がフレームの描画・CopyToBackBuffer・Present を完了するまでここで待機する。
		 */
		virtual void FlushRender() {}

		/**
		 * 登録用関数
		 * NOTE:Initialize後に呼ばれる
		 */
		virtual void Register() = 0;
	};
}
