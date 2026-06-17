#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Graphics/IBuffer.h"

namespace aq
{
	namespace graphics { class GraphicsDevice; }

	namespace rendering
	{
		/**
		 * Provides per-draw constant buffer slots for one frame.
		 *
		 * DX11: wraps a growing pool of DEFAULT constant buffers updated via UpdateSubresource.
		 * DX12: replace with an upload-heap slice allocator that returns GPU virtual addresses.
		 */
		class IConstantBufferPool
		{
		public:
			virtual ~IConstantBufferPool() = default;
			virtual graphics::IConstantBuffer* Allocate() = 0;
			virtual void Reset() = 0;
		};


		/**
		 * Default IConstantBufferPool implementation.
		 * Allocates IConstantBuffer objects via GraphicsDevice on demand; reuses them
		 * across frames after Reset().  Grows monotonically to the per-frame high-water mark.
		 */
		class ConstantBufferPool final : public IConstantBufferPool
		{
		public:
			explicit ConstantBufferPool(uint32_t cbSize);

			graphics::IConstantBuffer* Allocate() override;
			void Reset() override;

		private:
			uint32_t cbSize_;
			uint32_t cursor_ = 0;
			std::vector<std::unique_ptr<graphics::IConstantBuffer>> pool_;
		};


		/**
		 * Per-frame render resources passed to every IRenderCommand::Execute() call.
		 *
		 * perDrawCBPool  : b0 用 (world/view/proj)、draw ごとに Allocate()
		 * materialCBPool : b2 用 (MaterialCBData)、draw ごとに Allocate()
		 * lightingCB     : b1 用、フレーム先頭で 1 回更新し全 draw で共有 (raw ptr, 寿命は FrameSlot)
		 */
		struct FrameContext
		{
			IConstantBufferPool*       perDrawCBPool  = nullptr;
			IConstantBufferPool*       materialCBPool = nullptr;
			graphics::IConstantBuffer* lightingCB     = nullptr;
			graphics::IConstantBuffer* shadowCB       = nullptr; // b3: per-frame shadow data
		};
	}
}
