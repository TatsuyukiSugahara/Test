#include "aq.h"
#include "HiZReadbackCommand.h"
#include "HiZRenderer.h"


namespace aq
{
	namespace rendering
	{
		void HiZReadbackCommand::Execute(graphics::RenderContext&, FrameContext&) const
		{
			if (!renderer_ || !level_.IsValid()) return;

			std::vector<float> data;
			// 直近で GPU 完了済みのデータがあれば data へ格納し、今フレームのコピーを記録する。
			if (graphics::GraphicsDevice::Get().ReadbackOffscreenR32(level_, width_, height_, data))
			{
				renderer_->StoreReadback(std::move(data), width_, height_);
			}
		}
	}
}
