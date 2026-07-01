#pragma once
#include "Graphics/IGraphicsDeviceImpl.h"
#include "Graphics/RenderContext.h"
#include "D3D11RenderResources.h"
#include "RenderConfig.h"


namespace aq
{
	namespace graphics
	{
		/**
		 * DirectX 11 Concrete Implementor (Bridge Pattern)
		 *
		 * D3D11 固有の型 (ID3D11Device* 等) はこのファイルにのみ登場する。
		 * Engine や他のコードはこのクラスを直接 include しない。
		 */
		class D3D11GraphicsDeviceImpl : public IGraphicsDeviceImpl
		{
		public:
			D3D11GraphicsDeviceImpl();
			~D3D11GraphicsDeviceImpl() override;

			bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) override;
			void Finalize() override;
			void SetupRenderContext(RenderContext& outContext) override;
			uint32_t GetMainRenderTargetCount() const override { return MAIN_RT_COUNT; }
			IRenderTarget& GetMainRenderTarget(uint32_t index) override;
			IRenderTarget* GetRenderTarget(uint32_t index) override;
			uint32_t CreateOffscreenRenderTarget(uint32_t width, uint32_t height) override;
			uint32_t CreateOffscreenRenderTarget(const RenderTargetDesc& desc) override;
			void Present() override;
			void CopyToBackBuffer(IRenderTarget& src) override;
			void SetupDefaultRenderState(RenderContext& context) override;

			/** D3D11 固有: デバイスが必要な箇所（バッファ生成など）向け */
			ID3D11Device* GetDevice() const { return device_; }
			/** D3D11 固有: imgui など context が必要な箇所向け */
			ID3D11DeviceContext* GetDeviceContext() const { return deviceContext_; }

			/** SamplerState / RenderTarget / Texture 等 D3D11 リソースクラス向け静的アクセサ */
			static ID3D11Device*        GetStaticDevice();
			static ID3D11DeviceContext* GetStaticDeviceContext();

			std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IVertexBuffer>       CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data) override;
			std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) override;
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) override;
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) override;
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) override;
			std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) override;

		private:
			bool CreateDeviceAndSwapChain(HWND hwnd, uint32_t width, uint32_t height); // HWND は D3D11 内部のみ
#if defined(AQ_PLATFORM_UWP)
			bool CreateDeviceAndSwapChainUWP(::IUnknown* coreWindow, uint32_t width, uint32_t height);
#endif
			bool CreateMainRenderTargets(uint32_t width, uint32_t height);

		private:
			ID3D11Device*           device_;
			ID3D11DeviceContext*    deviceContext_;
			IDXGISwapChain*         swapChain_;
			ID3D11RasterizerState*  rasterizerState_;
			D3D_DRIVER_TYPE         driverType_;
			D3D_FEATURE_LEVEL       featureLevel_;

			// オフスクリーン RT ハンドルの基点（スワップチェーンとは独立。常に 2 で固定）。
			static constexpr uint32_t RENDER_TARGET_COUNT = 2;
			// メイン HDR RT の実枚数。非同期モードのみダブルバッファ（2 枚）にする。
#ifdef AQ_RENDER_PIPELINED
			static constexpr uint32_t MAIN_RT_COUNT = 2;
#else
			static constexpr uint32_t MAIN_RT_COUNT = 1;
#endif
			RenderTarget mainRenderTargets_[MAIN_RT_COUNT];
			uint32_t     currentRenderTargetIndex_;

			std::vector<std::unique_ptr<RenderTarget>> offscreenRenderTargets_;
		};
	}
}
