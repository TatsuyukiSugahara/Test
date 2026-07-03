#include "aq.h"
#include "GraphicsDevice.h"
#include "RenderContext.h"
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace graphics
	{
		GraphicsDevice* GraphicsDevice::instance_ = nullptr;


		// コンピュートシェーダ/UAV/GPU 駆動機能が使えるか。既定は対応(PC/D3D12/FL11 の D3D11)。
		// D3D11 バックエンドがフィーチャーレベル 11 未満(Xbox One UWP の FL10_1 等)を検出したとき
		// false に設定され、Bloom/HiZ/GPU カリング/海など compute 前提のパスを無効化する。
		namespace { bool g_computeSupported = true; }
		void SetComputeSupported(bool supported) { g_computeSupported = supported; }
		bool IsComputeSupported()               { return g_computeSupported; }


		GraphicsDevice::GraphicsDevice(std::unique_ptr<IGraphicsDeviceImpl> impl)
			: impl_(std::move(impl))
		{
		}


		GraphicsDevice::~GraphicsDevice()
		{
		}


		bool GraphicsDevice::Initialize(NativeWindowHandle window, uint32_t width, uint32_t height)
		{
			return impl_->Initialize(window, width, height);
		}


		void GraphicsDevice::Finalize()
		{
			impl_->Finalize();
		}


		void GraphicsDevice::OnSuspend() { if (impl_) impl_->OnSuspend(); }
		void GraphicsDevice::OnResume()  { if (impl_) impl_->OnResume();  }


		void GraphicsDevice::SetupRenderContext(RenderContext& outContext)
		{
			impl_->SetupRenderContext(outContext);
		}


		uint32_t GraphicsDevice::GetMainRenderTargetCount() const
		{
			return impl_->GetMainRenderTargetCount();
		}


		IRenderTarget& GraphicsDevice::GetMainRenderTarget(uint32_t index)
		{
			return impl_->GetMainRenderTarget(index);
		}


		IRenderTarget* GraphicsDevice::GetRenderTarget(rendering::RenderTargetHandle handle)
		{
			if (!handle.IsValid()) return nullptr;
			return impl_->GetRenderTarget(handle.index);
		}


		bool GraphicsDevice::ReadbackOffscreenR32(rendering::RenderTargetHandle handle,
		                                          uint32_t width, uint32_t height, std::vector<float>& outData)
		{
			if (!handle.IsValid()) return false;
			return impl_->ReadbackOffscreenR32(handle.index, width, height, outData);
		}


		rendering::RenderTargetHandle GraphicsDevice::CreateOffscreenRenderTarget(uint32_t width, uint32_t height)
		{
			return rendering::RenderTargetHandle{ impl_->CreateOffscreenRenderTarget(width, height) };
		}


		rendering::RenderTargetHandle GraphicsDevice::CreateOffscreenRenderTarget(const graphics::RenderTargetDesc& desc)
		{
			return rendering::RenderTargetHandle{ impl_->CreateOffscreenRenderTarget(desc) };
		}


		void GraphicsDevice::Present()
		{
			impl_->Present();
		}


		void GraphicsDevice::CopyToBackBuffer(IRenderTarget& src)
		{
			impl_->CopyToBackBuffer(src);
		}


		void GraphicsDevice::SetupDefaultRenderState(RenderContext& context)
		{
			impl_->SetupDefaultRenderState(context);
		}


		std::unique_ptr<IVertexBuffer> GraphicsDevice::CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return impl_->CreateVertexBuffer(vertexNum, stride, data);
		}

		std::unique_ptr<IVertexBuffer> GraphicsDevice::CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			return impl_->CreateDynamicVertexBuffer(vertexNum, stride, data);
		}

		std::unique_ptr<IIndexBuffer> GraphicsDevice::CreateIndexBuffer(uint32_t indexNum, const void* data)
		{
			return impl_->CreateIndexBuffer(indexNum, data);
		}

		std::unique_ptr<IIndexBuffer> GraphicsDevice::CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data)
		{
			return impl_->CreateDynamicIndexBuffer(indexNum, format, data);
		}

		std::unique_ptr<IConstantBuffer> GraphicsDevice::CreateConstantBuffer(const void* data, uint32_t size)
		{
			return impl_->CreateConstantBuffer(data, size);
		}

		std::unique_ptr<IShader> GraphicsDevice::CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type)
		{
			return impl_->CreateShader(filePath, entryFunc, type);
		}

		std::unique_ptr<ISamplerState> GraphicsDevice::CreateSamplerState(const SamplerDesc& desc)
		{
			return impl_->CreateSamplerState(desc);
		}

		std::unique_ptr<IShaderResourceView> GraphicsDevice::CreateTexture2D(const Texture2DDesc& desc, const ImageData& data)
		{
			return impl_->CreateTexture2D(desc, data);
		}

		std::unique_ptr<IDepthMap> GraphicsDevice::CreateDepthMap(uint32_t width, uint32_t height)
		{
			return impl_->CreateDepthMap(width, height);
		}
	}
}
