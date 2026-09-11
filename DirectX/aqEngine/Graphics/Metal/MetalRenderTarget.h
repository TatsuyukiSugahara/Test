#pragma once
// Metal のレンダーターゲット。
//
// 本ヘッダは Objective-C 型を使うため **Graphics/Metal 配下の .mm からのみ** include できる
// (MetalCommon.h が __OBJC__ を要求する)。エンジンから見える口は IRenderTarget だけ。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IRenderTarget.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/IUnorderedAccessView.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * Metal レンダーターゲット(設計書/MetalBackend設計.md §7)
		 *
		 * Vulkan 版と同じく 2 モードを持つ。
		 *  (a) オフスクリーン       … 自前の MTLTexture を所有する。GBuffer / ポスプロ / メイン RT。
		 *  (b) スワップチェーンプロキシ … CAMetalLayer の nextDrawable のテクスチャを指す(P1 で使う)。
		 *
		 * Metal はエンコーダ内のリソース依存を自動で追跡するので、Vulkan 版のような
		 * image layout の保持と遷移は要らない(設計書 §8)。
		 *
		 * 参照カウントは MRR。new... で得たオブジェクトは Release() で対に release する。
		 */
		class MetalRenderTarget final : public IRenderTarget
		{
			/**
			 * 内蔵ビュー
			 *
			 * RT のカラーを後続パスでサンプル / compute から書き込むための軽いビュー。
			 * テクスチャの所有者はあくまで MetalRenderTarget なので Release() は何もしない。
			 */
		public:
			/** カラーをサンプルするための SRV */
			class ColorSRV final : public IShaderResourceView
			{
			public:
				MetalRenderTarget* owner = nullptr;

				/** 実体のテクスチャ(プロキシならその時点の drawable) */
				inline id<MTLTexture> GetTexture() const { return (owner != nullptr) ? owner->GetTexture() : nil; }

				/** Metal では id<MTLTexture> をネイティブハンドルとして返す(ImGui / 描画側が直接使える) */
				void* GetNativeHandle() const override { return GetTexture(); }

				void Release() override {}  // owner が所有
			};


			/** compute が RT へ書き込むための UAV(MTLTexture の ShaderWrite 使用) */
			class ColorUAV final : public IUnorderedAccessView
			{
			public:
				MetalRenderTarget* owner = nullptr;

				inline id<MTLTexture> GetTexture() const { return (owner != nullptr) ? owner->GetTexture() : nil; }

				void Release() override {}  // owner が所有
			};


		private:
			/** カラー(オフスクリーンは所有、プロキシは drawable を借りるだけ) */
			id<MTLTexture> colorTexture_;
			id<MTLTexture> drawableTexture_;
			MTLPixelFormat colorFormat_;

			/** 深度(hasDepth のオフスクリーンのみ。Apple Silicon の都合で Depth32Float 固定) */
			id<MTLTexture> depthTexture_;

			/** 解像度とモード */
			uint32_t width_;
			uint32_t height_;
			bool     proxy_;

			/** 内蔵ビュー */
			ColorSRV colorSRV_;
			ColorUAV colorUAV_;


		public:
			MetalRenderTarget();
			~MetalRenderTarget() override;

			MetalRenderTarget(const MetalRenderTarget&) = delete;
			MetalRenderTarget& operator=(const MetalRenderTarget&) = delete;


			/**
			 * 生成 / 破棄
			 */
		public:
			/**
			 * オフスクリーン RT を作る
			 * @param device      MTLDevice
			 * @param width       幅
			 * @param height      高さ
			 * @param colorFormat カラーのフォーマット
			 * @param hasDepth    深度テクスチャも作るか
			 * @return 生成できたら true
			 */
			bool CreateOffscreen(id<MTLDevice>  device,
			                     const uint32_t width,
			                     const uint32_t height,
			                     MTLPixelFormat colorFormat,
			                     const bool     hasDepth);

			/**
			 * スワップチェーンプロキシとして初期化する(P1 で使う)。
			 * 実体は毎フレーム SetDrawableTexture() で差し替える。
			 */
			void InitAsSwapchainProxy(const uint32_t width, const uint32_t height, MTLPixelFormat colorFormat);

			/**
			 * プロキシモードの実体テクスチャを差し替える(P1: nextDrawable のテクスチャ)。
			 * drawable のテクスチャはフレーム毎に変わるため、retain して次の差し替えで release する。
			 * @param texture nil を渡すと現在の参照を落とす
			 */
			void SetDrawableTexture(id<MTLTexture> texture);

			void Release();


			/**
			 * IRenderTarget
			 */
		public:
			IShaderResourceView&  GetRenderTargetSRV() override { return colorSRV_; }
			IUnorderedAccessView& GetRenderTargetUAV() override { return colorUAV_; }


			/**
			 * Metal 固有(P1〜P4 の描画側が使う)
			 */
		public:
			/** カラーのテクスチャ。プロキシなら現在の drawable のテクスチャ */
			inline id<MTLTexture> GetTexture() const { return proxy_ ? drawableTexture_ : colorTexture_; }

			/** カラーのフォーマット。PSO の colorAttachments[n].pixelFormat に要る */
			inline MTLPixelFormat GetPixelFormat() const { return colorFormat_; }

			inline bool           HasDepth()            const { return depthTexture_ != nil; }
			inline id<MTLTexture> GetDepthTexture()     const { return depthTexture_; }
			inline MTLPixelFormat GetDepthPixelFormat() const { return (depthTexture_ != nil) ? metal::DEPTH_PIXEL_FORMAT : MTLPixelFormatInvalid; }

			inline bool     IsProxy()   const { return proxy_; }
			inline uint32_t GetWidth()  const { return width_; }
			inline uint32_t GetHeight() const { return height_; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
