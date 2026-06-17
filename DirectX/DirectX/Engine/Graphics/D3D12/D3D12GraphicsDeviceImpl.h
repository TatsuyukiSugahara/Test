#pragma once
#include "Graphics/IGraphicsDeviceImpl.h"

// D3D12/DXGI の COM インターフェースを前方宣言する。
// d3d12.h は .cpp 実装側でのみ include する。
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;
struct IDXGISwapChain3;
struct IDXGIFactory4;


namespace aq
{
	namespace graphics
	{
		/**
		 * DirectX 12 Concrete Implementor (Bridge Pattern)
		 *
		 * D3D11GraphicsDeviceImpl と同じ役割を D3D12 オブジェクトで担う。
		 *
		 * DX11 との主な違い
		 * -----------------
		 * - スワップチェーンは IDXGISwapChain3、バックバッファは D3D12 リソース。
		 * - 描画は D3D12RenderContextImpl が記録したコマンドリストを
		 *   D3D12CommandQueue に Submit する形になる。
		 * - 定数バッファはアップロードヒープのサブアロケーション（UploadBufferAllocator）。
		 * - GPU 同期は ID3D12Fence + WaitForSingleObject。
		 *
		 * 移行チェックリスト
		 * ------------------
		 * 1. Engine.cpp に #ifdef ENGINE_GRAPHICS_D3D12 ガードを追加する（D3D11 と同様）。
		 * 2. D3D12 SDK ヘッダ・ライブラリ（d3d12.lib, dxgi.lib）をプロジェクトに追加する。
		 * 3. 下記の各オーバーライドを D3D12GraphicsDeviceImpl.cpp に実装する。
		 * 4. CreateConstantBuffer() でアップロードヒープスライスを返すように変更する。
		 */
		class D3D12GraphicsDeviceImpl : public IGraphicsDeviceImpl
		{
		public:
			D3D12GraphicsDeviceImpl();
			~D3D12GraphicsDeviceImpl() override;

			bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) override;
			void Finalize() override;
			void SetupRenderContext(RenderContext& outContext) override;
			uint32_t GetMainRenderTargetCount() const override;
			IRenderTarget& GetMainRenderTarget(uint32_t index) override;
			void Present() override;
			void CopyToBackBuffer(IRenderTarget& src) override;
			void SetupDefaultRenderState(RenderContext& context) override;

			// TODO: D3D12 オフスクリーン RT 未実装。D3D12 バックエンド移行時に実装する。
			IRenderTarget* GetRenderTarget(uint32_t index) override { return nullptr; }
			uint32_t CreateOffscreenRenderTarget(uint32_t /*width*/, uint32_t /*height*/) override { return ~0u; }

			std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) override;
			std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) override;
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) override;
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) override;
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) override;

			/** D3D12RenderContextImpl がコマンドリストを取得するためのアクセサ */
			ID3D12GraphicsCommandList* GetCommandList() const { return commandList_; }
			ID3D12CommandQueue*        GetCommandQueue() const { return commandQueue_; }

		private:
			bool CreateDeviceAndQueues(void* hwnd, uint32_t width, uint32_t height);
			bool CreateSwapChain(void* hwnd, uint32_t width, uint32_t height);
			bool CreateRenderTargets(uint32_t width, uint32_t height);
			void WaitForGPU();

		private:
			ID3D12Device*              device_        = nullptr;
			ID3D12CommandQueue*        commandQueue_  = nullptr;
			ID3D12CommandAllocator*    commandAlloc_  = nullptr;
			ID3D12GraphicsCommandList* commandList_   = nullptr;
			IDXGISwapChain3*           swapChain_     = nullptr;
			ID3D12Fence*               fence_         = nullptr;
			uint64_t                   fenceValue_    = 0;

			static constexpr uint32_t RENDER_TARGET_COUNT = 2;
			// TODO: D3D12RenderTarget mainRenderTargets_[RENDER_TARGET_COUNT];
			uint32_t currentRTIndex_ = 0;
		};
	}
}
