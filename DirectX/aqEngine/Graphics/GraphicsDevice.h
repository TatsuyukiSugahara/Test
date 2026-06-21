#pragma once
#include <memory>
#include <cstdint>
#include "IGraphicsDeviceImpl.h"
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace graphics
	{
		class RenderContext;
		class IRenderTarget;

		/**
		 * Graphics Device Abstraction (Bridge Pattern)
		 *
		 * Engine やその他のコードはこのクラスを通じてグラフィクス API を使う。
		 * 内部に IGraphicsDeviceImpl を持ち、API の差異をここで吸収する。
		 *
		 * 使用例（起動時）:
		 *   GraphicsDevice::Create(std::make_unique<D3D11GraphicsDeviceImpl>());
		 *   GraphicsDevice::Get().Initialize(hwnd, width, height);
		 *
		 * 将来 D3D12 に切り替える場合:
		 *   GraphicsDevice::Create(std::make_unique<D3D12GraphicsDeviceImpl>());
		 *   // 呼び出し側のコードは一切変えなくてよい
		 */
		class GraphicsDevice
		{
		public:
			~GraphicsDevice();

			GraphicsDevice(const GraphicsDevice&) = delete;
			GraphicsDevice& operator=(const GraphicsDevice&) = delete;


		public:
			bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height);
			void Finalize();

			/** RenderContext に API 依存コンテキストをセット */
			void SetupRenderContext(RenderContext& outContext);

			/** メインRTの数を返す。RenderTargetHandle の有効インデックス範囲は [0, count) */
			uint32_t GetMainRenderTargetCount() const;

			/** メインのレンダリングターゲットを返す */
			IRenderTarget& GetMainRenderTarget(uint32_t index);

			/** ハンドルが示す RT を返す（メイン・オフスクリーン両対応）。無効なら nullptr */
			IRenderTarget* GetRenderTarget(rendering::RenderTargetHandle handle);

			/** オフスクリーン RT を生成してハンドルを返す。失敗時は INVALID ハンドル */
			rendering::RenderTargetHandle CreateOffscreenRenderTarget(uint32_t width, uint32_t height);
			rendering::RenderTargetHandle CreateOffscreenRenderTarget(const graphics::RenderTargetDesc& desc);

			/** 描画結果を画面に出す */
			void Present();

			/** メインRTをバックバッファへコピー */
			void CopyToBackBuffer(IRenderTarget& src);

			/** デフォルトのレンダーステートを設定する */
			void SetupDefaultRenderState(RenderContext& context);

			/** Impl への生ポインタ (D3D11 専用クラスが static getter 経由で使う) */
			IGraphicsDeviceImpl* GetImplRaw() const { return impl_.get(); }

			/**
			 * リソースファクトリー
			 * 呼び出し元は API 実装型を知らなくてよい。
			 */
			std::unique_ptr<IVertexBuffer>   CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data);
			std::unique_ptr<IVertexBuffer>   CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data);
			std::unique_ptr<IIndexBuffer>    CreateIndexBuffer(uint32_t indexNum, const void* data);
			std::unique_ptr<IConstantBuffer> CreateConstantBuffer(const void* data, uint32_t size);
			std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type);
			std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc);
			std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data);
			std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height);


		private:
			std::unique_ptr<IGraphicsDeviceImpl> impl_;


			/**
			 * Singleton
			 * aq::Create() のタイミングで GraphicsDevice::Create() を呼ぶ。
			 */
		private:
			static GraphicsDevice* instance_;
			explicit GraphicsDevice(std::unique_ptr<IGraphicsDeviceImpl> impl);

		public:
			template<typename TImpl, typename... TArgs>
			static void Create(TArgs&&... args)
			{
				static_assert(std::is_base_of_v<IGraphicsDeviceImpl, TImpl>,
				              "TImpl must implement IGraphicsDeviceImpl");
				if (instance_ == nullptr) {
					instance_ = new GraphicsDevice(std::make_unique<TImpl>(std::forward<TArgs>(args)...));
				}
			}
			static GraphicsDevice& Get() { return *instance_; }
			static void Release()
			{
				if (instance_) {
					delete instance_;
					instance_ = nullptr;
				}
			}
		};
	}
}
