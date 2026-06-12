#pragma once
#include "RenderFrame.h"


namespace engine
{
	namespace graphics
	{
		class RenderContext;
	}

	namespace rendering
	{
		/**
		 * RenderFrame を受け取り、実際の描画コマンドを発行する。
		 * ゲームスレッド側のデータ（ECS, SceneGraph）には一切触れない。
		 *
		 * 将来 RenderThread 化する際は、このクラスをそちらで動かすだけでよい。
		 */
		class Renderer
		{
		public:
			void Render(graphics::RenderContext& context, const RenderFrame& frame);

		private:
			void DrawItem(graphics::RenderContext& context,
			              const RenderItem&        item,
			              const CameraData&        camera);
		};
	}
}
