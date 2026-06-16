#pragma once
#include <cstdint>
#include <memory>
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IDepthMap.h"
#include "GraphicsTypes.h"


namespace engine
{
	namespace graphics
	{
		class RenderContext;
		class IRenderTarget;

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
			virtual bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) = 0;

			/** 解放 */
			virtual void Finalize() = 0;

			/**
			 * RenderContext に API 依存のコンテキストを渡す。
			 * D3D11 なら ID3D11DeviceContext*、D3D12 なら CommandList など。
			 */
			virtual void SetupRenderContext(RenderContext& outContext) = 0;

			/** メインRTの数を返す。RenderTargetHandle の有効インデックス範囲は [0, count) */
			virtual uint32_t GetMainRenderTargetCount() const = 0;

			/** メインのレンダリングターゲットを返す */
			virtual IRenderTarget& GetMainRenderTarget(uint32_t index) = 0;

			/**
			 * ハンドルが示す RT を返す。メイン・オフスクリーン両方に対応。
			 * 無効なインデックスの場合は nullptr を返す。
			 */
			virtual IRenderTarget* GetRenderTarget(uint32_t index) = 0;

			/**
			 * オフスクリーン RT を生成してそのインデックスを返す。
			 * 失敗時は ~0u (INVALID) を返す。
			 */
			virtual uint32_t CreateOffscreenRenderTarget(uint32_t width, uint32_t height) = 0;

			/** 描画結果を画面に出す */
			virtual void Present() = 0;

			/** メインRTを現在のバックバッファへコピー（不要な API では空実装） */
			virtual void CopyToBackBuffer(IRenderTarget& src) = 0;

			/** デフォルトのラスタライザーなど API 固有の初期レンダーステートを設定する */
			virtual void SetupDefaultRenderState(RenderContext& context) = 0;

			/**
			 * リソースファクトリー
			 * API 非依存のインターフェースを返す。呼び出し元は実装型を知る必要がない。
			 */
			virtual std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) = 0;
			virtual std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) = 0;
			virtual std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) = 0;
			virtual std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) = 0;
			virtual std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) = 0;
			virtual std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) = 0;
			virtual std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) = 0;
		};
	}
}
