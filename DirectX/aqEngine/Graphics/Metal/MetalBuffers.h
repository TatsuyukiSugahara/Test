#pragma once
// Metal のバッファ群(設計書/MetalBackend設計.md §6)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IBuffer.h"


namespace aq
{
	namespace graphics
	{
		// ── Metal バッファ群 (P0: 確保のみ。バインドは P2) ──
		//
		// ユニファイドメモリなので **ステージングバッファを作らない**(設計書 §6)。
		// すべて MTLResourceStorageModeShared で確保し、contents へ直接 memcpy する。
		//
		// frames-in-flight 対応: 動的バッファ(毎フレーム Update する VB/IB)は 1 本の
		// MTLBuffer 内に metal::FRAME_COUNT 個の領域を確保してリングする。GPU が前フレーム
		// を読んでいる間の上書き競合を防ぐ考え方は Vulkan 版(VulkanBuffers.h)と同じ。
		// 静的バッファ(メッシュ等)はリングしない(領域 1 本)。

		/**
		 * 頂点バッファ
		 */
		class MetalVertexBuffer : public IVertexBuffer
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice> device_;
			id<MTLBuffer> buffer_;

			/** レイアウト */
			uint32_t stride_;
			uint32_t bytesPerFrame_;

			/** リング関連(動的のみ) */
			uint32_t frameIndex_;
			bool     dynamic_;


		public:
			explicit MetalVertexBuffer(id<MTLDevice> device, bool dynamic);
			~MetalVertexBuffer() override;


		public:
			bool     Create(uint32_t vertexNum, uint32_t stride, const void* data) override;
			/** 動的版の別名。Vulkan 版(CreateDynamic)と呼び口を揃えるためのラッパ */
			bool     CreateDynamic(uint32_t vertexNum, uint32_t stride, const void* data);
			void     Release() override;
			bool     Update(const void* data, uint32_t byteSize) override;

			inline uint32_t GetStride() const override { return stride_; }


			/**
			 * バインド用 (P2 以降)
			 */
		public:
			/** 実体の MTLBuffer */
			inline id<MTLBuffer> GetBuffer() const { return buffer_; }

			/** 現在フレーム領域の先頭バイトオフセット(静的は常に 0) */
			uint32_t GetCurrentOffset() const;
		};




		/**
		 * インデックスバッファ
		 */
		class MetalIndexBuffer : public IIndexBuffer
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice> device_;
			id<MTLBuffer> buffer_;

			/** レイアウト */
			IndexFormat format_;
			uint32_t    bytesPerFrame_;

			/** リング関連(動的のみ) */
			uint32_t frameIndex_;
			bool     dynamic_;


		public:
			explicit MetalIndexBuffer(id<MTLDevice> device, bool dynamic);
			~MetalIndexBuffer() override;


		public:
			bool Create(uint32_t indexNum, const void* data) override;                          // 静的 R32
			bool Create(uint32_t indexNum, IndexFormat format, const void* data);               // フォーマット指定
			/** 動的版の別名。Vulkan 版(CreateDynamic)と呼び口を揃えるためのラッパ */
			bool CreateDynamic(uint32_t indexNum, IndexFormat format, const void* data);
			void Release() override;
			bool Update(const void* data, uint32_t byteSize) override;

			/** Create 時に受けたフォーマットを返す(既定の UInt32 固定では動的 16bit が壊れる) */
			inline IndexFormat GetFormat() const override { return format_; }


			/**
			 * バインド用 (P2 以降)
			 */
		public:
			/** 実体の MTLBuffer */
			inline id<MTLBuffer> GetBuffer() const { return buffer_; }

			/** drawIndexedPrimitives: へ渡すインデックス型 */
			inline MTLIndexType GetIndexType() const { return metal::ToMTLIndexType(format_); }

			/** 現在フレーム領域の先頭バイトオフセット(静的は常に 0) */
			uint32_t GetCurrentOffset() const;
		};




		/**
		 * 定数バッファ
		 */
		class MetalConstantBuffer : public IConstantBuffer
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice> device_;
			id<MTLBuffer> buffer_;

			/** サイズ関連 */
			uint32_t dataSize_;     // 元データのサイズ (Update の memcpy 量)
			uint32_t alignedSize_;  // metal::CONSTANT_BUFFER_ALIGNMENT へ切り上げた確保サイズ


		public:
			explicit MetalConstantBuffer(id<MTLDevice> device);
			~MetalConstantBuffer() override;


		public:
			bool Create(const void* data, uint32_t size) override;
			void Release() override;

			/** contents へ dataSize_ バイト memcpy する(MetalRenderContextImpl から呼ばれる) */
			void Update(const void* data);


			/**
			 * バインド用 (P2 以降)
			 */
		public:
			/** 実体の MTLBuffer */
			inline id<MTLBuffer> GetBuffer() const { return buffer_; }

			/** 現在の書き込み領域の先頭バイトオフセット */
			inline uint32_t GetCurrentOffset() const { return 0; }

			/** setVertexBuffer: へ渡す有効バイト数 */
			inline uint32_t GetRange() const { return dataSize_; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
