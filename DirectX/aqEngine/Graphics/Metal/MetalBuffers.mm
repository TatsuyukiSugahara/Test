#include "aq.h"
// Metal のバッファ群。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalBuffers.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include <cstdio>

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


			/**
			 * frames-in-flight のリング位置
			 *
			 * **自前カウンタを Update ごとに進めてはいけない**(設計書 §13-7)。
			 * 同一フレーム内で複数回 Update されるとスライスがずれ、直前に記録した
			 * 描画が読む領域を上書きしてしまう。デバイスのフレーム番号は Present ごとに
			 * しか進まないので、何度 Update されてもずれない。
			 */
			inline uint32_t CurrentFrameIndex()
			{
				const MetalGraphicsDeviceImpl* device = MetalGraphicsDeviceImpl::GetInstance();
				return (device != nullptr) ? device->GetFrameIndex() : 0;
			}


			/**
			 * 単調増加のフレーム通し番号
			 *
			 * 「フレームが変わったか」の判定に剰余 (CurrentFrameIndex) を使ってはいけない。
			 * FRAMES_IN_FLIGHT (レンダースレッドのスロット数) と metal::FRAME_COUNT が
			 * どちらも 2 なので、あるスロットの定数バッファは**常に同じ剰余値**を見ることになり、
			 * 「フレームが変わっていない」と誤判定してカーソルが永久にリセットされない。
			 */
			inline uint64_t CurrentFrameSerial()
			{
				const MetalGraphicsDeviceImpl* device = MetalGraphicsDeviceImpl::GetInstance();
				return (device != nullptr) ? device->GetFrameSerial() : 0;
			}


			/**
			 * このフレームが実際に始まっているか
			 *
			 * false の間は**コマンドバッファがまだ無い = 描画が 1 本も記録されていない**。
			 * drawable が取れずに捨てられたフレーム(ウィンドウが隠れている等)は
			 * Present が空振りしてフレーム番号が進まないので、これを見ないと
			 * 定数バッファのカーソルだけが延々と積み上がる。
			 */
			inline bool IsDeviceFrameOpen()
			{
				const MetalGraphicsDeviceImpl* device = MetalGraphicsDeviceImpl::GetInstance();
				return (device != nullptr) && device->IsFrameOpen();
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
			// 書き込み先は「今のフレームの領域」。1 フレームに何度 Update されても
			// 同じ領域を書き直すだけで、GPU が読んでいる前フレームの領域は触らない。
			std::memcpy(Contents(buffer_) + GetCurrentOffset(), data, byteSize);
			return true;
		}


		uint32_t MetalVertexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (bytesPerFrame_ * CurrentFrameIndex()) : 0;
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
			std::memcpy(Contents(buffer_) + GetCurrentOffset(), data, byteSize);
			return true;
		}


		uint32_t MetalIndexBuffer::GetCurrentOffset() const
		{
			return dynamic_ ? (bytesPerFrame_ * CurrentFrameIndex()) : 0;
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
			, sliceCount_(0)
			, cursor_(0)
			, lastFrameSerial_(0xffffffffffffffffull)  // 最初の Update で必ずカーソルをリセットさせる
			, currentOffset_(0)
			, exhaustedLogged_(false)
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
				if (alignedSize_ == 0) {
					return false;
				}

				// 1 フレーム領域が INITIAL_FRAME_SIZE_BYTES に収まる本数を初期スライス数にする
				// (Vulkan 版と同じ配分)。足りなくなったら Update 側で Grow する。
				sliceCount_ = INITIAL_FRAME_SIZE_BYTES / alignedSize_;
				if (sliceCount_ < MIN_SLICE_COUNT)          { sliceCount_ = MIN_SLICE_COUNT; }
				if (sliceCount_ > MAX_INITIAL_SLICE_COUNT)  { sliceCount_ = MAX_INITIAL_SLICE_COUNT; }

				cursor_          = 0;
				lastFrameSerial_ = 0xffffffffffffffffull;
				currentOffset_   = 0;
				exhaustedLogged_ = false;

				buffer_ = NewSharedBuffer(device_, alignedSize_ * sliceCount_ * metal::FRAME_COUNT);
				if (buffer_ == nil) {
					return false;
				}

				// 初期データは各フレーム領域の先頭スライスへ置く
				// (一度も Update されない静的な CB がそのまま束ねられるため)。
				if (data) {
					uint8_t* dst = Contents(buffer_);
					for (uint32_t f = 0; f < metal::FRAME_COUNT; ++f) {
						std::memcpy(dst + static_cast<size_t>(alignedSize_) * sliceCount_ * f, data, dataSize_);
					}
				}
				return true;
			}
		}


		bool MetalConstantBuffer::Grow()
		{
			@autoreleasepool
			{
				const uint32_t newSliceCount = sliceCount_ * 2;
				const uint64_t newTotalSize  = static_cast<uint64_t>(alignedSize_) * newSliceCount * metal::FRAME_COUNT;
				if (newTotalSize > MAX_TOTAL_SIZE_BYTES) {
					return false;
				}

				id<MTLBuffer> newBuffer = NewSharedBuffer(device_, static_cast<uint32_t>(newTotalSize));
				if (newBuffer == nil) {
					return false;
				}

				// 旧レイアウトの各フレーム領域を、新レイアウトの同じフレームへ移す。
				// 記録済みの描画が参照するのは旧 MTLBuffer のままだが、Metal はエンコーダが
				// 触れたリソースをコマンドバッファ完了まで retain するので解放して問題ない。
				const uint8_t* src = Contents(buffer_);
				uint8_t*       dst = Contents(newBuffer);
				if (src != nullptr && dst != nullptr) {
					const size_t oldFrameSize = static_cast<size_t>(alignedSize_) * sliceCount_;
					const size_t newFrameSize = static_cast<size_t>(alignedSize_) * newSliceCount;
					for (uint32_t f = 0; f < metal::FRAME_COUNT; ++f) {
						std::memcpy(dst + newFrameSize * f, src + oldFrameSize * f, oldFrameSize);
					}
				}

				[buffer_ release];
				buffer_     = newBuffer;  // newBufferWithLength: は +1 済みなので retain しない
				sliceCount_ = newSliceCount;

				char msg[128];
				std::snprintf(msg, sizeof(msg),
				              "[MetalConstantBuffer] スライスを拡張しました (size=%u, slices=%u, total=%uKB)",
				              dataSize_, sliceCount_,
				              static_cast<uint32_t>(newTotalSize / 1024));
				aq::StartupLog(msg);
				return true;
			}
		}


		void MetalConstantBuffer::Update(const void* data)
		{
			if (buffer_ == nil || !data || sliceCount_ == 0) {
				return;
			}

			// フレームが変わったらカーソルを戻す。同一フレーム内では戻さないので、
			// 先に記録した描画が読むスライスを書き潰すことがない。
			// 「まだフレームが開いていない」間も戻してよい(描画が 1 本も記録されていないため)。
			// これが無いと、drawable が取れずに捨てられ続けるフレームで Update だけが積み上がり、
			// リングが無駄に伸びる。
			// 判定は**単調増加の通し番号**で行う。剰余の frameIndex で比べると、
			// レンダースレッドのスロットと FRAME_COUNT の偶奇が噛み合ったときに
			// 永久に等しくなり、カーソルがリセットされないままリングを食い潰す。
			const uint32_t frameIndex  = CurrentFrameIndex();
			const uint64_t frameSerial = CurrentFrameSerial();
			if (frameSerial != lastFrameSerial_ || !IsDeviceFrameOpen()) {
				lastFrameSerial_ = frameSerial;
				cursor_          = 0;
			}

			if (cursor_ >= sliceCount_)
			{
				// 枯渇。まずリングを伸ばす。伸ばせないときは**最後のスライスを使い回す**。
				// 先頭へ巻き戻すと、同じフレームで既に記録済みの描画のデータが壊れるため、
				// 「最後の 1 本を共有して見た目が崩れる」ほうを選ぶ(原因はログで分かる)。
				if (!Grow())
				{
					if (!exhaustedLogged_)
					{
						exhaustedLogged_ = true;
						char msg[192];
						std::snprintf(msg, sizeof(msg),
						              "[MetalConstantBuffer] リングが枯渇しました。最終スライスを使い回します "
						              "(size=%u, slices=%u, 上限=%uKB)。描画が最後の値で潰れます",
						              dataSize_, sliceCount_, MAX_TOTAL_SIZE_BYTES / 1024);
						aq::StartupLog(msg);
					}
					cursor_ = sliceCount_ - 1;
				}
			}

			const uint32_t slice = cursor_;
			++cursor_;

			currentOffset_ = (frameIndex * sliceCount_ + slice) * alignedSize_;
			std::memcpy(Contents(buffer_) + currentOffset_, data, dataSize_);
		}


		void MetalConstantBuffer::Release()
		{
			[buffer_ release];
			buffer_ = nil;

			sliceCount_    = 0;
			cursor_        = 0;
			currentOffset_ = 0;
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
