#pragma once
#include <memory>
#include "RenderFrame.h"
#include "RenderCommandList.h"
#include "Shadow/IShadowRenderer.h"


namespace engine
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		/**
		 * RenderFrame を RenderCommandList に変換する（記録フェーズ担当）。
		 *
		 * 非同期パス（推奨）:
		 *   BuildCommandList() でコマンドを記録し、RenderThread::Submit() で実行する。
		 *
		 * 同期パス（デバッグ専用）:
		 *   RenderDebugSync() は BuildCommandList() + 即時 Execute() を 1 呼び出しで行う。
		 *   リリースビルドでは除外されるため、プロダクションコードから呼んではならない。
		 */
		class Renderer
		{
		public:
			/**
			 * シャドウレンダラーを設定する。nullptr を渡すと影なしになる。
			 * メインビューポートのサイズも合わせて指定する（シャドウパス後の復元用）。
			 */
			void SetShadowRenderer(std::unique_ptr<IShadowRenderer> sr,
			                       float mainViewportW, float mainViewportH);

			IShadowRenderer* GetShadowRenderer() const { return shadowRenderer_.get(); }

			/**
			 * ゲームスレッドでフレームデータを outList に記録する。
			 * シャドウレンダラーが設定されている場合は frame.shadow を自動的に埋める。
			 */
			void BuildCommandList(RenderFrame& frame, RenderCommandList& outList) const;

#if _DEBUG
			/**
			 * 同期実行（デバッグ専用）。BuildCommandList + Execute を 1 呼び出しで行う。
			 * 必ずレンダースレッドから呼ぶこと。メインスレッドから呼ぶと
			 * D3D11 immediate context の単一スレッド規則に違反する。
			 */
			void RenderDebugSync(graphics::RenderContext& context, RenderFrame& frame);
#endif

		private:
			void RecordDrawItem(const RenderItem&  item,
			                    const CameraData&  camera,
			                    RenderCommandList& outList) const;

			std::unique_ptr<IShadowRenderer> shadowRenderer_;
			float                            mainViewportW_ = 0.f;
			float                            mainViewportH_ = 0.f;
		};
	}
}
