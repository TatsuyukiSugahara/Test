#pragma once
#include <memory>
#include <vector>
#include "Graphics/IGraphicsDeviceImpl.h"

// D3D12/DXGI の COM インターフェースを前方宣言する。
// d3d12.h は .cpp 実装側でのみ include する。
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;
struct ID3D12Resource;
struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;
struct IDXGISwapChain3;
struct IDXGIFactory4;


namespace aq
{
	namespace graphics
	{
		class D3D12RenderTarget;
		class D3D12DepthMap;
		class D3D12RootSignature;
		class D3D12PipelineStateCache;
		class D3D12RenderContextImpl;
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
			friend class D3D12RenderContextImpl;  // SRVヒープ直接アクセスのため
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

			// オフスクリーン RT (Phase 3)
			IRenderTarget* GetRenderTarget(uint32_t index) override;
			uint32_t CreateOffscreenRenderTarget(uint32_t width, uint32_t height) override;
			uint32_t CreateOffscreenRenderTarget(const RenderTargetDesc& desc) override;

			// ── リソースファクトリ (Phase 0: クリア画面のため未実装。Phase 1〜2 で実装) ──
			std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IVertexBuffer>       CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data) override;
			std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) override;
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) override;
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) override;
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) override;
			std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) override;

			/** D3D12RenderContextImpl が記録に使うアクセサ */
			ID3D12GraphicsCommandList* GetCommandList() const   { return commandList_; }
			ID3D12CommandQueue*        GetCommandQueue() const  { return commandQueue_; }
			ID3D12RootSignature*       GetRootSignature() const;
			ID3D12RootSignature*       GetComputeRootSignature() const;
			D3D12PipelineStateCache*   GetPipelineCache() const { return psoCache_.get(); }
			// ── SRV ディスクリプタヒープ (Phase 2) ──
			// RenderContext (friend) が描画時にテーブルを確保・コピーするためのアクセサ。
			// D3D12_*_HANDLE をヘッダで露出しないよう、ハンドルは「ヒープ + インデックス」で受け渡し、
			// 実ハンドル計算は d3d12.h を含む .cpp 側で行う。
			ID3D12DescriptorHeap* GetSRVShaderHeap() const  { return srvShaderHeap_; }
			ID3D12DescriptorHeap* GetSRVStagingHeap() const { return srvStagingHeap_; }
			uint32_t              GetSRVDescriptorSize() const { return srvDescriptorSize_; }
			uint32_t              GetNullSRVIndex() const { return nullSRVIndex_; }

			/** shader-visible ring から count 個連続の SRV スロットを確保し、先頭インデックスを返す。
			 *  フレーム内 bump アロケータ。容量超過時 false (呼び側は描画スキップ)。 */
			bool AllocateSRVTableRange(uint32_t count, uint32_t& outBaseIndex);

			/** フレーム世代。BeginFrameIfNeeded で新フレームを開くたびに +1。
			 *  RenderContext が「コマンドリスト Reset により失われた SRV テーブルバインド」を
			 *  フレーム先頭で張り直すために監視する。 */
			uint64_t GetFrameGeneration() const { return frameGeneration_; }

			/** RenderContext から呼ばれ、フレーム未オープンならコマンドリストを開いて RT を準備する */
			void BeginFrameIfNeeded();

			/** リソースクラス向け静的アクセサ (D3D11 層の GetStaticDevice と同じパターン) */
			static ID3D12Device* GetStaticDevice();

		private:
			bool CreateDeviceAndQueues();
			bool CreateSwapChain(void* hwnd, uint32_t width, uint32_t height);
			bool CreateBackBuffers();
			bool CreateMainRenderTargets(uint32_t width, uint32_t height);
			bool CreateSRVHeaps();
			bool CreateRtvDsvHeaps();
			void WaitForGPU();

		private:
			static constexpr uint32_t RENDER_TARGET_COUNT = 2;

			ID3D12Device*              device_        = nullptr;
			ID3D12CommandQueue*        commandQueue_  = nullptr;
			ID3D12CommandAllocator*    commandAlloc_  = nullptr;
			ID3D12GraphicsCommandList* commandList_   = nullptr;
			IDXGISwapChain3*           swapChain_     = nullptr;
			ID3D12Resource*            backBuffers_[RENDER_TARGET_COUNT] = {};

			// カラー RTV / 深度 DSV のディスクリプタヒープ (CPU 専用・線形アロケータ)
			ID3D12DescriptorHeap*      rtvHeap_           = nullptr;
			ID3D12DescriptorHeap*      dsvHeap_           = nullptr;
			uint32_t                   rtvDescriptorSize_ = 0;
			uint32_t                   dsvDescriptorSize_ = 0;
			uint32_t                   rtvNext_           = 0;
			uint32_t                   dsvNext_           = 0;
			uint32_t                   rtvCapacity_       = 0;
			uint32_t                   dsvCapacity_       = 0;

			ID3D12Fence*               fence_         = nullptr;
			void*                      fenceEvent_    = nullptr;  // HANDLE
			uint64_t                   fenceValue_    = 0;

			// メイン RT(深度付きオフスクリーン)。シーンはここへ描画し CopyToBackBuffer でバックバッファへ複写。
			std::unique_ptr<D3D12RenderTarget> mainRenderTargets_[RENDER_TARGET_COUNT];
			std::vector<std::unique_ptr<D3D12RenderTarget>> offscreenRTs_;
			uint32_t                   currentBackBufferIndex_ = 0;
			uint32_t                   backBufferStates_[RENDER_TARGET_COUNT] = {};  // D3D12_RESOURCE_STATES

			std::unique_ptr<D3D12RootSignature>     rootSignature_;
			std::unique_ptr<D3D12PipelineStateCache> psoCache_;
			bool                       frameOpen_ = false;  // BeginFrameIfNeeded で開かれているか

			// ── SRV ディスクリプタヒープ (Phase 2) ──
			// d3d12.h をヘッダに持ち込まないため、ハンドルは保持せずヒープ + サイズ + インデックスで管理する。
			ID3D12DescriptorHeap*      srvStagingHeap_       = nullptr;  // CPU可視/非shader-visible: テクスチャSRV恒久格納
			ID3D12DescriptorHeap*      srvShaderHeap_        = nullptr;  // shader-visible: 毎フレームringでテーブル確保
			uint32_t                   srvDescriptorSize_    = 0;
			uint32_t                   srvStagingCapacity_   = 0;
			uint32_t                   srvStagingNext_       = 0;        // 次に割り当てる staging スロット
			uint32_t                   srvShaderRingCapacity_= 0;
			uint32_t                   srvShaderRingNext_    = 0;        // フレーム内 ring 位置 (BeginFrame で reset)
			uint32_t                   nullSRVIndex_         = 0;        // staging 内の null SRV 位置
			uint64_t                   frameGeneration_      = 0;        // BeginFrameIfNeeded で開くたびに +1
		};
	}
}
