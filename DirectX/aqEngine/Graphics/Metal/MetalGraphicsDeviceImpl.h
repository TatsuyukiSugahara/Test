#pragma once
// Metal バックエンドのデバイス実装(ブリッジパターンの Concrete Implementor)。
//
// **本ヘッダだけは素の C++** で書く。aqEngine/Engine.cpp(素の .cpp)が include して
// GraphicsDevice::Create<MetalGraphicsDeviceImpl>() を呼ぶため、Objective-C 型を一切出さない
// (設計書/MetalBackend設計.md §10)。Metal オブジェクトは不透明構造体 MetalDeviceObjects へ
// まとめ、定義は MetalGraphicsDeviceImpl.mm 側に置く(PlatformMac.h / PlatformMac.mm と同じ手)。
#if defined(ENGINE_GRAPHICS_METAL)
#include <cstdint>
#include <memory>
#include <vector>
#include "Graphics/IGraphicsDeviceImpl.h"


namespace aq
{
	namespace graphics
	{
		class RenderContext;
		class IRenderTarget;
		class MetalRenderTarget;
		class MetalRenderContextImpl;

		/** Metal オブジェクト群(MTLDevice / MTLCommandQueue / CAMetalLayer)。定義は .mm 側 */
		struct MetalDeviceObjects;


		/**
		 * Metal Concrete Implementor (Bridge Pattern) — P0(足場)
		 *
		 * P0 の到達目標は「ビルドが通り、黒いウィンドウで起動して終了コード 0 で終わる」こと
		 * (設計書/MetalBackend設計.md §12)。drawable 取得・クリア・Present は P1 で入れる。
		 *
		 * 設計: 設計書/MetalBackend設計.md §1(全体像) / §2(フレーム) / §7(RT・深度) / §10(ヘッダ規約)
		 * - スワップチェーンは CAMetalLayer。PlatformMac が生成済みのレイヤを受け取って設定するだけ。
		 * - MTLRenderPass / Framebuffer 相当のオブジェクトは無く、描画毎に
		 *   MTLRenderPassDescriptor を組み立てる(MetalRenderContextImpl の担当)。
		 */
		class MetalGraphicsDeviceImpl : public IGraphicsDeviceImpl
		{
			friend class MetalRenderContextImpl;

		private:
			/** メイン RT の数。RenderTargetHandle の有効範囲と ToggleMainRenderTarget が 2 枚前提 */
			static constexpr uint32_t MAIN_RT_COUNT = 2;


		private:
			/** Metal オブジェクト群(Initialize で生成し Finalize で解放) */
			MetalDeviceObjects* objects_;

			/** スワップチェーンの論理サイズ(contentsScale = 1 固定なのでドロウアブルと一致) */
			uint32_t width_;
			uint32_t height_;

			/** レンダーターゲット。先頭 MAIN_RT_COUNT 本がメイン、その後ろがオフスクリーン */
			std::unique_ptr<MetalRenderTarget>              mainRTs_[MAIN_RT_COUNT];
			std::vector<std::unique_ptr<MetalRenderTarget>> offscreenRTs_;

			/** 現在の描画コンテキスト(所有は RenderContext 側。Present でエンコーダを閉じるため保持) */
			MetalRenderContextImpl* activeContext_;


		public:
			MetalGraphicsDeviceImpl();
			~MetalGraphicsDeviceImpl() override;

			MetalGraphicsDeviceImpl(const MetalGraphicsDeviceImpl&) = delete;
			MetalGraphicsDeviceImpl& operator=(const MetalGraphicsDeviceImpl&) = delete;


			/**
			 * IGraphicsDeviceImpl
			 */
		public:
			bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) override;
			void Finalize() override;
			void WaitIdle() override;
			void SetupRenderContext(RenderContext& outContext) override;
			void SetupDefaultRenderState(RenderContext& context) override;

			uint32_t       GetMainRenderTargetCount() const override;
			IRenderTarget& GetMainRenderTarget(uint32_t index) override;
			IRenderTarget* GetRenderTarget(uint32_t index) override;
			uint32_t       CreateOffscreenRenderTarget(uint32_t width, uint32_t height) override;
			uint32_t       CreateOffscreenRenderTarget(const RenderTargetDesc& desc) override;

			void Present() override;
			void CopyToBackBuffer(IRenderTarget& src) override;


			/**
			 * リソースファクトリー
			 */
		public:
			std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IVertexBuffer>       CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) override;
			std::unique_ptr<IIndexBuffer>        CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data) override;
			std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) override;
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) override;
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) override;
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) override;
			std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) override;


			/**
			 * Metal オブジェクトへの口
			 *
			 * 素の C++ ヘッダに id<MTL*> を出せないため void* で渡す。Graphics/Metal 配下の .mm では
			 * 単純キャストで元の型へ戻して使う(MRR ビルドなので __bridge 注釈は不要)。
			 * この口は Graphics/Metal/ の中だけで使い、外へは出さないこと(設計書 §15 チェックポイント)。
			 */
		public:
			/** id<MTLDevice> */
			void* GetMTLDeviceHandle() const;

			/** id<MTLCommandQueue> */
			void* GetMTLCommandQueueHandle() const;

			/** CAMetalLayer* */
			void* GetCAMetalLayerHandle() const;

			/**
			 * 直近にコミットした MTLCommandBuffer を記録する(retain する)。
			 * WaitIdle() はこれに対して waitUntilCompleted を掛ける。P1 の Present から呼ぶ。
			 * @param commandBuffer id<MTLCommandBuffer>。nil を渡すと記録を消す
			 */
			void SetLastCommittedCommandBuffer(void* commandBuffer);


			/**
			 * 各種アクセサ
			 */
		public:
			inline uint32_t GetWidth()  const { return width_; }
			inline uint32_t GetHeight() const { return height_; }

			/** Present で描画エンコーダを閉じるための現在コンテキスト */
			inline MetalRenderContextImpl* GetActiveContext() const { return activeContext_; }


		public:
			/** リソースクラス向けの静的アクセサ(Vulkan/D3D12 層の GetInstance と同じパターン) */
			static MetalGraphicsDeviceImpl* GetInstance();

			/** メイン RT の枚数(コンパイル時定数版) */
			static constexpr uint32_t GetMainRTCount() { return MAIN_RT_COUNT; }
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
