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

struct ImDrawData;


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
		 * Metal Concrete Implementor (Bridge Pattern) — P4(HDR メイン RT・compute 有効化)
		 *
		 * メイン RT は Vulkan / D3D12 と同じ HDR(R16G16B16A16_Float)+ 深度付きで、
		 * トーンマップ後の LDR を CopyToBackBuffer でバックバッファへ出す(設計書 §13-9)。
		 *
		 * 設計: 設計書/MetalBackend設計.md §1(全体像) / §2(フレーム) / §7(RT・深度) / §10(ヘッダ規約)
		 * - スワップチェーンは CAMetalLayer。PlatformMac が生成済みのレイヤを受け取って設定するだけ。
		 * - MTLRenderPass / Framebuffer 相当のオブジェクトは無く、描画毎に
		 *   MTLRenderPassDescriptor を組み立てる(MetalRenderContextImpl の担当)。
		 * - 抽象 IF に BeginFrame/EndFrame が無いため、フレームの開始は Vulkan 版と同じく
		 *   BeginFrameIfNeeded() の**遅延発火**で吸収する(設計書 §2.3)。
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

			/**
			 * スワップチェーンプロキシ。実体は毎フレームの nextDrawable のテクスチャ(設計書 §7)。
			 * GetRenderTarget() の並びには入れない(エンジンからは見えず、CopyToBackBuffer の宛先専用)。
			 */
			std::unique_ptr<MetalRenderTarget> swapchainRT_;

			/** 現在の描画コンテキスト(所有は RenderContext 側。Present でエンコーダを閉じるため保持) */
			MetalRenderContextImpl* activeContext_;

			/** フレーム状態。BeginFrameIfNeeded で立て、Present で倒す(設計書 §2.2) */
			/**
			 * Present するたびに増える通し番号。frames-in-flight のリング位置に使う。
			 * 動的バッファ(CB / 動的 VB・IB)は自前カウンタではなくこれを見ること
			 * (1 フレーム内で何度 Update されてもリングがずれないため)。
			 *
			 * **宣言順に注意**: コンストラクタの初期化順はヘッダの宣言順で決まるので、
			 * frameOpen_ より前に置く(初期化子の順序と合わせないと -Wreorder-ctor が出る)。
			 */
			uint64_t frameCounter_;

			bool frameOpen_;

			/** このフレームは nextDrawable が nil で捨てた。Present までの再取得を 1 回に抑える */
			bool frameAcquireFailed_;

			/**
			 * このフレームの imgui 描画データ(AQ_IMGUI 時のみ立つ)。
			 * CopyToBackBuffer の末尾で drawable へ描いたら nullptr へ戻す(1 フレーム限りのため)。
			 */
			ImDrawData* imguiDrawData_;


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
			 * フレーム
			 */
		public:
			/**
			 * このフレームがまだ始まっていなければ開始する(設計書 §2.2 / §2.3)。
			 *
			 * セマフォで frames-in-flight を待ってから nextDrawable とコマンドバッファを取る。
			 * 抽象 IF に BeginFrame が無いため、最初に必要になった時点
			 * (クリアの flush / 描画 / CopyToBackBuffer / Present)で呼ぶ。
			 * nextDrawable は nil を返しうる(ウィンドウが隠れている等)。その場合は
			 * セマフォを戻してフレームを捨て、フレーム未開始のまま戻る。
			 */
			void BeginFrameIfNeeded();

			/** フレームが開始済みか(捨てたフレームでは false のまま) */
			inline bool IsFrameOpen() const { return frameOpen_; }

			/**
			 * frames-in-flight のリング位置(0 .. FRAME_COUNT-1)。
			 *
			 * 動的バッファはこの値でスライスを選ぶ。**自前カウンタを Update ごとに
			 * 進めてはいけない**(同一フレーム内で複数回 Update されるとずれる)。
			 */
			uint32_t GetFrameIndex() const;


			/**
			 * ImGui (設計書 §12 P6)
			 */
		public:
			/**
			 * imgui の描画データを受け取り、CopyToBackBuffer 後に drawable へ描く (AQ_IMGUI 時)。
			 *
			 * Vulkan 版と同じ経路。ImGuiRenderCommand::Execute から毎フレーム渡され、
			 * 描き終えた時点で nullptr へ戻す(描画データはそのフレーム限り有効)。
			 */
			inline void SetImGuiDrawData(ImDrawData* data) { imguiDrawData_ = data; }


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
			 * GPU 駆動クラスタカリング用のバッファ(設計書 §6)。
			 *
			 * compute を有効にすると GpuClusterBuffers::Create() がこれらを要求する。
			 * Metal では単なる MTLBuffer(MTLStorageModeShared)で、D3D12 のような
			 * ディスクリプタヒープの確保は要らない。**生成に失敗したら nullptr を返す**
			 * (呼び出し元の Meshlet.cpp が 4 本すべて揃ったかを見て clusterCount を立てるため、
			 * 他のファクトリーのように空オブジェクトを返してはいけない)。
			 */
			std::unique_ptr<IGpuBuffer>          CreateStructuredBuffer(uint32_t stride, uint32_t count, const void* data) override;
			std::unique_ptr<IGpuBuffer>          CreateRawBuffer(uint32_t byteSize, bool srv, bool uav, const void* initData) override;


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
			 * このフレームの id<MTLCommandBuffer>。フレーム未開始なら nullptr。
			 * MetalRenderContextImpl がエンコーダを開くのに使う。
			 */
			void* GetCurrentCommandBufferHandle() const;

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
