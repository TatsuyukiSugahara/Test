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
		// ── Metal バッファ群 ──
		//
		// ユニファイドメモリなので **ステージングバッファを作らない**(設計書 §6)。
		// すべて MTLResourceStorageModeShared で確保し、contents へ直接 memcpy する。
		//
		// frames-in-flight 対応: 動的バッファ(毎フレーム Update する VB/IB / 全 CB)は 1 本の
		// MTLBuffer 内に metal::FRAME_COUNT 個の領域を確保してリングする。GPU が前フレーム
		// を読んでいる間の上書き競合を防ぐ考え方は Vulkan 版(VulkanBuffers.h)と同じ。
		// 静的バッファ(メッシュ等)はリングしない(領域 1 本)。
		//
		// **リング位置はデバイスのフレーム番号で決める**(P2 で修正。設計書 §13-7)。
		// MetalGraphicsDeviceImpl::GetFrameIndex() は Present ごとにしか進まないので、
		// 同一フレーム内で何度 Update されてもスライスがずれない。自前カウンタを
		// Update ごとに進めると 1 フレーム 2 回更新でずれる。

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

			/** リング関連(動的のみ。位置はデバイスのフレーム番号で決めるので自前カウンタは持たない) */
			bool dynamic_;


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
			 * バインド用
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

			/** リング関連(動的のみ。位置はデバイスのフレーム番号で決めるので自前カウンタは持たない) */
			bool dynamic_;


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
			 * バインド用
			 */
		public:
			/** 実体の MTLBuffer */
			inline id<MTLBuffer> GetBuffer() const { return buffer_; }

			/** drawIndexedPrimitives: へ渡すインデックス型 */
			inline MTLIndexType GetIndexType() const { return metal::ToMTLIndexType(format_); }

			/**
			 * インデックス 1 要素のバイト数。
			 *
			 * drawIndexedPrimitives: の indexBufferOffset は**バイト単位**なので、
			 * startIndexLocation をこれ倍してリングの offset へ足す必要がある。
			 */
			inline uint32_t GetIndexStride() const { return (format_ == IndexFormat::UInt16) ? 2u : 4u; }

			/** 現在フレーム領域の先頭バイトオフセット(静的は常に 0) */
			uint32_t GetCurrentOffset() const;
		};




		/**
		 * 定数バッファ
		 *
		 * エンジンは**同一フレーム内で同じ CB を何度も Update する**(オブジェクト毎の world 行列、
		 * UI の DrawRange 毎の定数など)。1 領域へ上書きすると **全オブジェクトが最後の値で
		 * 描かれる**ので、Update ごとに別スライスへ bump 確保する(Vulkan 版と同型。設計書 §13-7)。
		 *
		 * レイアウト:
		 * ```
		 *   [ frame0 : slice0 slice1 ... sliceN-1 ][ frame1 : slice0 ... sliceN-1 ]
		 * ```
		 * 1 スライスは metal::CONSTANT_BUFFER_ALIGNMENT(256)へ切り上げ。カーソルは
		 * **デバイスのフレーム番号が変わったとき**(と、まだフレームが開いておらず描画が
		 * 1 本も記録されていないとき)だけ 0 へ戻すので、前フレームの領域を書き潰さない。
		 *
		 * スライスを使い切ったら**リングを 2 倍に伸ばす**(Grow)。先頭へ巻き戻すと、
		 * 同じフレームで既に記録済みの描画が読むデータを壊すため、それだけは行わない。
		 */
		class MetalConstantBuffer : public IConstantBuffer
		{
		private:
			/** 1 フレーム領域の初期バイト数。ここから初期スライス数を決める(Vulkan 版と同値) */
			static constexpr uint32_t INITIAL_FRAME_SIZE_BYTES = 256u * 1024u;

			/** 初期スライス数の下限 / 上限 */
			static constexpr uint32_t MIN_SLICE_COUNT = 4;
			static constexpr uint32_t MAX_INITIAL_SLICE_COUNT = 1024;

			/** 1 本の CB が使ってよい総バイト数の上限。これを超える Grow は行わない */
			static constexpr uint32_t MAX_TOTAL_SIZE_BYTES = 8u * 1024u * 1024u;


		private:
			/** Metal オブジェクト */
			id<MTLDevice> device_;
			id<MTLBuffer> buffer_;

			/** サイズ関連 */
			uint32_t dataSize_;     // 元データのサイズ (Update の memcpy 量)
			uint32_t alignedSize_;  // metal::CONSTANT_BUFFER_ALIGNMENT へ切り上げた 1 スライス分

			/** リング関連 */
			uint32_t sliceCount_;      // 1 フレーム領域あたりのスライス数
			uint32_t cursor_;          // 現フレームで使い終えたスライス数
			uint32_t lastFrameIndex_;  // 直近に Update したときのデバイスのフレーム番号
			uint32_t currentOffset_;   // 直近に書いたスライスの先頭バイトオフセット
			bool     exhaustedLogged_; // 枯渇ログを 1 回だけ出すためのフラグ


		private:
			/**
			 * スライス数を 2 倍に伸ばして確保し直す。
			 *
			 * 既に記録済みの描画は**古い MTLBuffer** を参照しているが、Metal はエンコーダが
			 * 触れたリソースをコマンドバッファ完了まで retain するので、ここで release してよい。
			 * @return 伸ばせたら true(上限超え / 確保失敗なら false)
			 */
			bool Grow();


		public:
			explicit MetalConstantBuffer(id<MTLDevice> device);
			~MetalConstantBuffer() override;


		public:
			bool Create(const void* data, uint32_t size) override;
			void Release() override;

			/** 次のスライスへ dataSize_ バイト memcpy する(MetalRenderContextImpl から呼ばれる) */
			void Update(const void* data);


			/**
			 * バインド用
			 */
		public:
			/** 実体の MTLBuffer */
			inline id<MTLBuffer> GetBuffer() const { return buffer_; }

			/** 直近に Update したスライスの先頭バイトオフセット */
			inline uint32_t GetCurrentOffset() const { return currentOffset_; }

			/** 有効バイト数。Metal の setVertexBuffer: は長さを取らないので診断用 */
			inline uint32_t GetRange() const { return dataSize_; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
