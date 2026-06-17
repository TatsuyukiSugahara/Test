#pragma once
#include <memory>
#include "RenderFrame.h"
#include "RenderCommandList.h"
#include "RenderTargetHandle.h"
#include "Shadow/IShadowRenderer.h"


namespace aq
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
			 * mainRTHandle / mainViewportW / mainViewportH は RenderDebugSync 専用
			 * （デバッグ同期パスでメイン RT に描画する際に使う）。
			 */
			void SetShadowRenderer(std::unique_ptr<IShadowRenderer> sr,
			                       RenderTargetHandle mainRTHandle,
			                       float mainViewportW, float mainViewportH);

			IShadowRenderer* GetShadowRenderer() const { return shadowRenderer_.get(); }

			/**
			 * ゲームスレッドでフレームデータを outList に記録する。
			 * シャドウレンダラーが設定されている場合は frame.shadow を自動的に埋める。
			 * rtHandle / viewportW / viewportH はシャドウパス後に復元するRTとビューポートを指定する。
			 */
			void BuildCommandList(RenderFrame& frame, RenderCommandList& outList,
			                      RenderTargetHandle rtHandle,
			                      float viewportW, float viewportH) const;

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
			RenderTargetHandle               mainRTHandle_;
			float                            mainViewportW_ = 0.f;
			float                            mainViewportH_ = 0.f;
		};
	}
}
