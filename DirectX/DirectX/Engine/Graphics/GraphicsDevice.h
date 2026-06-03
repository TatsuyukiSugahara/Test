#pragma once
#include <memory>
#include <cstdint>
#include <windows.h>
#include "IGraphicsDeviceImpl.h"


namespace engine
{
	namespace graphics
	{
		class RenderContext;
		class RenderTarget;

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
			/** Implementor を外部から注入する (Dependency Injection) */
			explicit GraphicsDevice(std::unique_ptr<IGraphicsDeviceImpl> impl);
			~GraphicsDevice();

			GraphicsDevice(const GraphicsDevice&) = delete;
			GraphicsDevice& operator=(const GraphicsDevice&) = delete;


		public:
			bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
			void Finalize();

			/** RenderContext に API 依存コンテキストをセット */
			void SetupRenderContext(RenderContext& outContext);

			/** メインのレンダリングターゲットを返す */
			RenderTarget& GetMainRenderTarget(uint32_t index);

			/** 描画結果を画面に出す */
			void Present();

			/** メインRTをバックバッファへコピー */
			void CopyToBackBuffer(RenderTarget& src);

			/** デフォルトのレンダーステートを設定する */
			void SetupDefaultRenderState(RenderContext& context);

			/** Impl への生ポインタ (D3D11 専用クラスが static getter 経由で使う) */
			IGraphicsDeviceImpl* GetImplRaw() const { return impl_.get(); }

			/**
			 * リソースファクトリー
			 * 呼び出し元は API 実装型を知らなくてよい。
			 */
			std::unique_ptr<IVertexBuffer>   CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data);
			std::unique_ptr<IIndexBuffer>    CreateIndexBuffer(uint32_t indexNum, const void* data);
			std::unique_ptr<IConstantBuffer> CreateConstantBuffer(const void* data, uint32_t size);
			std::unique_ptr<IShader>         CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type);
			std::unique_ptr<ISamplerState>   CreateSamplerState(const SamplerDesc& desc);


		private:
			std::unique_ptr<IGraphicsDeviceImpl> impl_;


			/**
			 * Singleton
			 * Engine::Create() のタイミングで GraphicsDevice::Create() を呼ぶ。
			 */
		private:
			static GraphicsDevice* instance_;

		public:
			static void Create(std::unique_ptr<IGraphicsDeviceImpl> impl)
			{
				if (instance_ == nullptr) {
					instance_ = new GraphicsDevice(std::move(impl));
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
