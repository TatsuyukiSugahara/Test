#pragma once
#include <cstdint>
#include <memory>
#include <windows.h>
#include "IBuffer.h"
#include "IShader.h"
#include "ISamplerState.h"


namespace engine
{
	namespace graphics
	{
		class RenderContext;
		class RenderTarget;

		/**
		 * Graphics API Implementor interface (Bridge Pattern)
		 *
		 * D3D11 / D3D12 / Vulkan など各 API の実装クラスが継承する。
		 * Engine や GraphicsDevice はこのインターフェース越しにのみ API と話す。
		 */
		class IGraphicsDeviceImpl
		{
		public:
			virtual ~IGraphicsDeviceImpl() = default;

			/** デバイス・スワップチェーン等の初期化 */
			virtual bool Initialize(HWND hwnd, uint32_t width, uint32_t height) = 0;

			/** 解放 */
			virtual void Finalize() = 0;

			/**
			 * RenderContext に API 依存のコンテキストを渡す。
			 * D3D11 なら ID3D11DeviceContext*、D3D12 なら CommandList など。
			 */
			virtual void SetupRenderContext(RenderContext& outContext) = 0;

			/** メインのレンダリングターゲットを返す */
			virtual RenderTarget& GetMainRenderTarget(uint32_t index) = 0;

			/** 描画結果を画面に出す */
			virtual void Present() = 0;

			/** メインRTを現在のバックバッファへコピー（不要な API では空実装） */
			virtual void CopyToBackBuffer(RenderTarget& src) = 0;

			/** デフォルトのラスタライザーなど API 固有の初期レンダーステートを設定する */
			virtual void SetupDefaultRenderState(RenderContext& context) = 0;

			/**
			 * リソースファクトリー
			 * API 非依存のインターフェースを返す。呼び出し元は実装型を知る必要がない。
			 */
			virtual std::unique_ptr<IVertexBuffer>   CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) = 0;
			virtual std::unique_ptr<IIndexBuffer>    CreateIndexBuffer(uint32_t indexNum, const void* data) = 0;
			virtual std::unique_ptr<IConstantBuffer> CreateConstantBuffer(const void* data, uint32_t size) = 0;
			virtual std::unique_ptr<IShader>         CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) = 0;
			virtual std::unique_ptr<ISamplerState>   CreateSamplerState(const SamplerDesc& desc) = 0;
		};
	}
}
