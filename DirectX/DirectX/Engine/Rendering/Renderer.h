#pragma once
#include "RenderFrame.h"
#include "RenderCommandList.h"


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
			/** ゲームスレッドでフレームデータを outList に記録する。*/
			void BuildCommandList(const RenderFrame& frame, RenderCommandList& outList) const;

#if _DEBUG
			/**
			 * 同期実行（デバッグ専用）。BuildCommandList + Execute を 1 呼び出しで行う。
			 * 必ずレンダースレッドから呼ぶこと。メインスレッドから呼ぶと
			 * D3D11 immediate context の単一スレッド規則に違反する。
			 */
			void RenderDebugSync(graphics::RenderContext& context, const RenderFrame& frame);
#endif

		private:
			void RecordDrawItem(const RenderItem&  item,
			                    const CameraData&  camera,
			                    RenderCommandList& outList) const;
		};
	}
}
