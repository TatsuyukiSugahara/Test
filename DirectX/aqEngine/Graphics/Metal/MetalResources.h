#pragma once
// Metal のテクスチャ / サンプラ / SRV・UAV の共通基底 / GPU 駆動用バッファ
// (設計書/MetalBackend設計.md §5.3 / §6)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/IUnorderedAccessView.h"
#include "Graphics/IGpuBuffer.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal のシェーダリソースビュー共通基底
		 *
		 * **compute パス側との契約**。バインド側は IShaderResourceView を
		 * dynamic_cast<MetalSRVBase*> して、テクスチャとバッファのどちらとして
		 * 束ねるかを決める(テクスチャが nil ならバッファ SRV)。
		 *
		 * IShaderResourceView::GetNativeHandle() は「id<MTLTexture> を返す」規約なので
		 * **バッファ SRV では nullptr しか返せない**。構造化 / RAW バッファを
		 * [[buffer(t+n)]] へ束ねるにはこの口が要る(設計書 §5.1 / §5.3)。
		 *
		 * MetalDepthMap::DepthSRV だけはこの基底を継承していない(別担当のファイル)ため、
		 * **static_cast ではなく dynamic_cast で問い合わせること**。
		 */
		class MetalSRVBase : public IShaderResourceView
		{
		public:
			/** テクスチャ SRV でなければ nil */
			virtual id<MTLTexture> GetTexture() const = 0;

			/** バッファ SRV でなければ nil */
			virtual id<MTLBuffer> GetBuffer() const = 0;
		};




		/**
		 * Metal のアンオーダードアクセスビュー共通基底
		 *
		 * **compute パス側との契約**。Metal ではテクスチャ UAV が texture 空間、
		 * バッファ UAV が buffer 空間へ落ちる(設計書 §5.3)ので、バインド側は
		 * static_cast<MetalUAVBase*> してからどちらの空間へ束ねるかを決める。
		 *
		 * Metal バックエンドの IUnorderedAccessView は **すべて**この基底を継承している
		 * (MetalUAV / MetalRenderTarget::ColorUAV / MetalGpuBuffer::BufferUAV)ので、
		 * static_cast で安全に受けられる。
		 */
		class MetalUAVBase : public IUnorderedAccessView
		{
		public:
			/** テクスチャ UAV でなければ nil */
			virtual id<MTLTexture> GetTexture() const = 0;

			/** バッファ UAV でなければ nil */
			virtual id<MTLBuffer> GetBuffer() const = 0;
		};




		/**
		 * テクスチャ 2D + SRV
		 *
		 * ユニファイドメモリなので MTLStorageModeShared のテクスチャへ
		 * replaceRegion: で直接書き込む(ステージング不要。設計書 §6)。
		 * BC 圧縮はそのまま扱える(Apple Silicon が対応。設計書 §0.2)。
		 */
		class MetalTexture : public MetalSRVBase
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>  device_;
			id<MTLTexture> texture_;


		public:
			explicit MetalTexture(id<MTLDevice> device);
			~MetalTexture() override;


		public:
			bool Create(const Texture2DDesc& desc, const ImageData& data);
			void Release() override;

			/** 実体の MTLTexture */
			id<MTLTexture> GetTexture() const override { return texture_; }

			/** テクスチャ SRV なのでバッファは持たない */
			id<MTLBuffer> GetBuffer() const override { return nil; }

			/**
			 * ImGui の ImTextureID 用ハンドル
			 *
			 * 未ロード時は nullptr を返し、呼び出し側がスキップできるようにする
			 * (D3D12 / Vulkan と同方式)。
			 */
			void* GetNativeHandle() const override;
		};




		/**
		 * サンプラーステート
		 */
		class MetalSampler : public ISamplerState
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>       device_;
			id<MTLSamplerState> sampler_;


		public:
			explicit MetalSampler(id<MTLDevice> device);
			~MetalSampler() override;


		public:
			bool Create(const SamplerDesc& desc) override;
			void Release() override;

			/** 実体の MTLSamplerState */
			inline id<MTLSamplerState> GetSampler() const { return sampler_; }
		};




		/**
		 * アンオーダードアクセスビュー(単体所有版)
		 *
		 * Metal ではテクスチャ UAV が texture 空間、バッファ UAV が buffer 空間へ落ちる
		 * (設計書 §5.3)ので、両方を持てる入れ物にしておく。どちらか一方だけが有効。
		 */
		class MetalUAV : public MetalUAVBase
		{
		private:
			/** Metal オブジェクト(どちらか一方だけが有効) */
			id<MTLTexture> texture_;
			id<MTLBuffer>  buffer_;


		public:
			MetalUAV();
			~MetalUAV() override;


		public:
			void Release() override;

			/** 対象の差し替え(retain / release は本クラスが持つ) */
			void SetTexture(id<MTLTexture> texture);
			void SetBuffer(id<MTLBuffer> buffer);

			id<MTLTexture> GetTexture() const override { return texture_; }
			id<MTLBuffer>  GetBuffer() const override  { return buffer_; }
		};




		/**
		 * GPU 駆動処理用の汎用バッファ(構造化 SRV / RAW UAV / インデックス / 間接引数)
		 *
		 * D3D12 の UPLOAD / DEFAULT ヒープの使い分けは Metal には無い。Apple Silicon は
		 * ユニファイドメモリなので **MTLStorageModeShared 1 本**で、CPU 初期化も
		 * GPU 読み書きも同じバッファで済む(設計書 §6)。
		 *
		 * ビューは内蔵の BufferSRV / BufferUAV を指すだけで、実体の所有者は本クラス。
		 * よって内蔵ビューの Release() は何もしない(MetalRenderTarget の内蔵ビューと同じ作法)。
		 */
		class MetalGpuBuffer final : public IGpuBuffer
		{
			/**
			 * 内蔵ビュー
			 */
		public:
			/** 構造化 / RAW バッファの SRV(compute 入力) */
			class BufferSRV final : public MetalSRVBase
			{
			public:
				MetalGpuBuffer* owner = nullptr;

				/** バッファ SRV なのでテクスチャは持たない */
				id<MTLTexture> GetTexture() const override { return nil; }

				id<MTLBuffer> GetBuffer() const override { return (owner != nullptr) ? owner->GetBuffer() : nil; }

				/**
				 * Metal の GetNativeHandle() は id<MTLTexture> を返す規約なので **nullptr 固定**。
				 * バインド側は nil を見たら MetalSRVBase::GetBuffer() へ落ちること。
				 */
				void* GetNativeHandle() const override { return nullptr; }

				void Release() override {}  // owner が所有
			};


			/** RAW バッファの UAV(compute 出力 / 間接引数) */
			class BufferUAV final : public MetalUAVBase
			{
			public:
				MetalGpuBuffer* owner = nullptr;

				/** バッファ UAV なのでテクスチャは持たない */
				id<MTLTexture> GetTexture() const override { return nil; }

				id<MTLBuffer> GetBuffer() const override { return (owner != nullptr) ? owner->GetBuffer() : nil; }

				void Release() override {}  // owner が所有
			};


		private:
			/** Metal オブジェクト */
			id<MTLBuffer> buffer_;

			/** 寸法。stride > 0 なら構造化バッファ、0 なら RAW(ByteAddress) */
			uint32_t byteSize_;
			uint32_t stride_;

			/** 生成を要求されたビュー */
			bool srvValid_;
			bool uavValid_;

			/** 内蔵ビュー */
			BufferSRV srv_;
			BufferUAV uav_;


		public:
			MetalGpuBuffer();
			~MetalGpuBuffer() override;

			MetalGpuBuffer(const MetalGpuBuffer&) = delete;
			MetalGpuBuffer& operator=(const MetalGpuBuffer&) = delete;


			/**
			 * 生成 / 破棄
			 */
		public:
			/**
			 * バッファを確保する
			 * @param device       MTLDevice
			 * @param byteSize     確保するバイト数
			 * @param stride       構造化バッファの 1 要素のバイト数(RAW なら 0)
			 * @param srv          SRV として使うか
			 * @param uav          UAV として使うか
			 * @param initData     初期データ(nullptr ならゼロ初期化)
			 * @param initDataSize initData の有効バイト数。byteSize を切り上げている場合に
			 *                     **元の長さより先を読まない**ために別引数で受ける
			 * @return 生成できたら true
			 */
			bool Create(id<MTLDevice>  device,
			            const uint32_t byteSize,
			            const uint32_t stride,
			            const bool     srv,
			            const bool     uav,
			            const void*    initData,
			            const uint32_t initDataSize);

			void Release() override;


			/**
			 * IGpuBuffer
			 */
		public:
			IShaderResourceView*  AsSRV() override { return srvValid_ ? &srv_ : nullptr; }
			IUnorderedAccessView* AsUAV() override { return uavValid_ ? &uav_ : nullptr; }


			/**
			 * Metal 固有(インデックスバッファ / 間接引数として使う側が読む)
			 */
		public:
			inline id<MTLBuffer> GetBuffer()   const { return buffer_; }
			inline uint32_t      GetByteSize() const { return byteSize_; }
			inline uint32_t      GetStride()   const { return stride_; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
