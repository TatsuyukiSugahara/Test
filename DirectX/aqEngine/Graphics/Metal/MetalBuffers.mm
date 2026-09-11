#include "aq.h"
// Metal のバッファ群。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalBuffers.h"

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalBuffers.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** a を切り上げ単位として v を整列する */
			inline uint32_t AlignUp(const uint32_t v, const uint32_t a)
			{
				return (v + a - 1) & ~(a - 1);
			}


			/**
			 * CPU からも GPU からも見える MTLBuffer を確保する
			 *
			 * ユニファイドメモリなのでステージングは要らない(設計書 §6)。
			 * @return 失敗時は nil
			 */
			id<MTLBuffer> NewSharedBuffer(id<MTLDevice> device, const uint32_t byteSize)
			{
				if (device == nil || byteSize == 0) {
					return nil;
				}
				return [device newBufferWithLength:byteSize options:MTLResourceStorageModeShared];
			}


			/** MTLBuffer の contents へのバイト先頭 */
			inline uint8_t* Contents(id<MTLBuffer> buffer)
			{
				return (buffer != nil) ? static_cast<uint8_t*>([buffer contents]) : nullptr;
			}


			/** IndexFormat 1 要素のバイト数 */
			inline uint32_t IndexElementSize(const IndexFormat format)
			{
				return (format == IndexFormat::UInt16) ? 2u : 4u;
			}
		}


		/**
		 * 頂点バッファ
		 */
		MetalVertexBuffer::MetalVertexBuffer(id<MTLDevice> device, const bool dynamic)
			: device_([device retain])
			, buffer_(nil)
			, stride_(0)
			, bytesPerFrame_(0)
			, frameIndex_(0)
			, dynamic_(dynamic)
		{
		}


		MetalVertexBuffer::~MetalVertexBuffer()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalVertexBuffer::Create(const uint32_t vertexNum, const uint32_t stride, const void* data)
		{
			@autoreleasepool
			{
				Release();

				stride_        = stride;
				bytesPerFrame_ = vertexNum * stride;
				frameIndex_    = 0;

				const uint32_t frames = dynamic_ ? metal::FRAME_COUNT : 1;
				buffer_ = NewSharedBuffer(device_, bytesPerFrame_ * frames);
				if (buffer_ == nil) {
					return false;
				}

				// 全フレーム領域へ初期データを複製しておく(動的の未更新フレーム対策)。
				if (data) {
					uint8_t* dst = Contents(buffer_);
					for (uint32_t f = 0; f < frames; ++f) {
						std::memcpy(dst + static_cast<size_t>(bytesPerFrame_) * f, data, bytesPerFrame_);
					}
				}
				return true;
			}
		}


		bool MetalVertexBuffer::CreateDynamic(const uint32_t vertexNum, const uint32_t stride, const void* data)
		{
			return Create(vertexNum, stride, data);
		}


		void MetalVertexBuffer::Release()
		{
			[buffer_ release];
			buffer_ = nil;
		}


		bool MetalVertexBuffer::Update(const void* data, const uint32_t byteSize)
		{
			if (!dynamic_ || buffer_ == nil || !data || byteSize > bytesPerFrame_) {
				return false;
			}
			// 書き込み先を次のフレーム領域へ進めてから書く(GetCurrentOffset が
			// 直近に書いた領域を指すようにするため)。
			frameIndex_ = (frameIndex_ + 1) % metal::FRAME_COUNT;
			std::memcpy(Contents(buffer_) + GetCurrentOffset(), data, byteSize);
			return true;
		}


		uint32_t MetalVertexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (bytesPerFrame_ * frameIndex_) : 0;
		}


		/************************************/




		/**
		 * インデックスバッファ
		 */
		MetalIndexBuffer::MetalIndexBuffer(id<MTLDevice> device, const bool dynamic)
			: device_([device retain])
			, buffer_(nil)
			, format_(IndexFormat::UInt32)
			, bytesPerFrame_(0)
			, frameIndex_(0)
			, dynamic_(dynamic)
		{
		}


		MetalIndexBuffer::~MetalIndexBuffer()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalIndexBuffer::Create(const uint32_t indexNum, const void* data)
		{
			return Create(indexNum, IndexFormat::UInt32, data);
		}


		bool MetalIndexBuffer::Create(const uint32_t indexNum, const IndexFormat format, const void* data)
		{
			@autoreleasepool
			{
				Release();

				format_        = format;
				bytesPerFrame_ = indexNum * IndexElementSize(format);
				frameIndex_    = 0;

				const uint32_t frames = dynamic_ ? metal::FRAME_COUNT : 1;
				buffer_ = NewSharedBuffer(device_, bytesPerFrame_ * frames);
				if (buffer_ == nil) {
					return false;
				}

				if (data) {
					uint8_t* dst = Contents(buffer_);
					for (uint32_t f = 0; f < frames; ++f) {
						std::memcpy(dst + static_cast<size_t>(bytesPerFrame_) * f, data, bytesPerFrame_);
					}
				}
				return true;
			}
		}


		bool MetalIndexBuffer::CreateDynamic(const uint32_t indexNum, const IndexFormat format, const void* data)
		{
			return Create(indexNum, format, data);
		}


		void MetalIndexBuffer::Release()
		{
			[buffer_ release];
			buffer_ = nil;
		}


		bool MetalIndexBuffer::Update(const void* data, const uint32_t byteSize)
		{
			if (!dynamic_ || buffer_ == nil || !data || byteSize > bytesPerFrame_) {
				return false;
			}
			frameIndex_ = (frameIndex_ + 1) % metal::FRAME_COUNT;
			std::memcpy(Contents(buffer_) + GetCurrentOffset(), data, byteSize);
			return true;
		}


		uint32_t MetalIndexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (bytesPerFrame_ * frameIndex_) : 0;
		}


		/************************************/




		/**
		 * 定数バッファ
		 */
		MetalConstantBuffer::MetalConstantBuffer(id<MTLDevice> device)
			: device_([device retain])
			, buffer_(nil)
			, dataSize_(0)
			, alignedSize_(0)
		{
		}


		MetalConstantBuffer::~MetalConstantBuffer()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalConstantBuffer::Create(const void* data, const uint32_t size)
		{
			@autoreleasepool
			{
				Release();

				dataSize_    = size;
				alignedSize_ = AlignUp(size, metal::CONSTANT_BUFFER_ALIGNMENT);

				buffer_ = NewSharedBuffer(device_, alignedSize_);
				if (buffer_ == nil) {
					return false;
				}

				if (data) {
					std::memcpy(Contents(buffer_), data, dataSize_);
				}
				return true;
			}
		}


		void MetalConstantBuffer::Update(const void* data)
		{
			if (buffer_ == nil || !data) {
				return;
			}
			// TODO(P2): エンジンは同一フレーム内で同じ CB を何度も Update する
			//           (オブジェクト毎の world 行列など)。実描画を入れる段で
			//           Vulkan 版(VulkanConstantBuffer)と同じ「Update 毎に別スライスへ
			//           bump 確保 + frames-in-flight 分のリング」へ広げること。
			//           P0 は描画しないので単一スライスで足りる。
			std::memcpy(Contents(buffer_), data, dataSize_);
		}


		void MetalConstantBuffer::Release()
		{
			[buffer_ release];
			buffer_ = nil;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
