#include "FrameContext.h"
#include "Graphics/GraphicsDevice.h"


namespace engine
{
	namespace rendering
	{
		ConstantBufferPool::ConstantBufferPool(uint32_t cbSize)
			: cbSize_(cbSize)
		{
		}


		graphics::IConstantBuffer* ConstantBufferPool::Allocate()
		{
			if (cursor_ == static_cast<uint32_t>(pool_.size()))
			{
				auto cb = graphics::GraphicsDevice::Get().CreateConstantBuffer(nullptr, cbSize_);
				if (!cb) return nullptr;  // device lost / OOM
				pool_.push_back(std::move(cb));
			}
			return pool_[cursor_++].get();
		}


		void ConstantBufferPool::Reset()
		{
			cursor_ = 0;
		}
	}
}
