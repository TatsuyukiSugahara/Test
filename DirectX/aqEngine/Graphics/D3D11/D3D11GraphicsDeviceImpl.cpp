#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "D3D11GraphicsDeviceImpl.h"
#include "D3D11RenderContextImpl.h"
#include "D3D11Buffers.h"
#include "D3D11Shader.h"
#include "D3D11DepthMap.h"
#include "Graphics/GraphicsDevice.h"


namespace aq
{
	namespace graphics
	{
		D3D11GraphicsDeviceImpl::D3D11GraphicsDeviceImpl()
			: device_(nullptr)
			, deviceContext_(nullptr)
			, swapChain_(nullptr)
			, rasterizerState_(nullptr)
			, driverType_(D3D_DRIVER_TYPE_NULL)
			, featureLevel_(D3D_FEATURE_LEVEL_11_0)
			, mainRenderTargets_{}
			, currentRenderTargetIndex_(0)
		{
		}


		D3D11GraphicsDeviceImpl::~D3D11GraphicsDeviceImpl()
		{
		}


		bool D3D11GraphicsDeviceImpl::Initialize(NativeWindowHandle window, uint32_t width, uint32_t height)
		{
			HWND hwnd = static_cast<HWND>(window.handle);
			if (!CreateDeviceAndSwapChain(hwnd, width, height)) {
				return false;
			}
			if (!CreateMainRenderTargets(width, height)) {
				return false;
			}
			return true;
		}


		void D3D11GraphicsDeviceImpl::Finalize()
		{
			for (auto& rt : offscreenRenderTargets_) rt->Release();
			offscreenRenderTargets_.clear();
			for (auto& rt : mainRenderTargets_) rt.Release();
			if (rasterizerState_) {
				rasterizerState_->Release();
				rasterizerState_ = nullptr;
			}
			if (swapChain_) {
				swapChain_->Release();
				swapChain_ = nullptr;
			}
			if (deviceContext_) {
				deviceContext_->ClearState();
				deviceContext_->Release();
				deviceContext_ = nullptr;
			}
			if (device_) {
				device_->Release();
				device_ = nullptr;
			}
		}


		void D3D11GraphicsDeviceImpl::SetupRenderContext(RenderContext& outContext)
		{
			outContext.SetImpl(std::make_unique<D3D11RenderContextImpl>(deviceContext_));
		}


		IRenderTarget& D3D11GraphicsDeviceImpl::GetMainRenderTarget(uint32_t index)
		{
			EngineAssert(index < MAIN_RT_COUNT);
			if (index >= MAIN_RT_COUNT) index = 0;
			return mainRenderTargets_[index];
		}


		IRenderTarget* D3D11GraphicsDeviceImpl::GetRenderTarget(uint32_t index)
		{
			// [0, MAIN_RT_COUNT) = メイン RT。[MAIN_RT_COUNT, RENDER_TARGET_COUNT) は
			// 直列モードで未使用の予約領域（メイン RT 1 枚時の index 1）。
			// オフスクリーンはハンドル基点を RENDER_TARGET_COUNT 固定にして安定させる。
			if (index < MAIN_RT_COUNT)
			{
				return &mainRenderTargets_[index];
			}
			if (index < RENDER_TARGET_COUNT)
			{
				return nullptr;
			}
			const uint32_t offIdx = index - RENDER_TARGET_COUNT;
			if (offIdx < static_cast<uint32_t>(offscreenRenderTargets_.size()))
			{
				return offscreenRenderTargets_[offIdx].get();
			}
			return nullptr;
		}


		uint32_t D3D11GraphicsDeviceImpl::CreateOffscreenRenderTarget(uint32_t width, uint32_t height)
		{
			RenderTargetDesc desc;
			desc.width    = width;
			desc.height   = height;
			desc.hasDepth = true;
			return CreateOffscreenRenderTarget(desc);
		}


		uint32_t D3D11GraphicsDeviceImpl::CreateOffscreenRenderTarget(const RenderTargetDesc& desc)
		{
			auto rt = std::make_unique<RenderTarget>();
			SampleDesc sampleDesc;
			PixelFormat depthFmt = desc.hasDepth ? PixelFormat::D24_Unorm_S8_Uint : PixelFormat::Unknown;
			if (!rt->Create(static_cast<int32_t>(desc.width), static_cast<int32_t>(desc.height), 1,
				desc.colorFormat, depthFmt, sampleDesc))
			{
				return ~0u;
			}
			offscreenRenderTargets_.push_back(std::move(rt));
			return RENDER_TARGET_COUNT + static_cast<uint32_t>(offscreenRenderTargets_.size() - 1);
		}


		void D3D11GraphicsDeviceImpl::Present()
		{
			swapChain_->Present(0, 0);
		}


		void D3D11GraphicsDeviceImpl::CopyToBackBuffer(IRenderTarget& src)
		{
			RenderTarget& rt = static_cast<RenderTarget&>(src);
			ID3D11Texture2D* backBuffer = nullptr;
			if (FAILED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer)) || backBuffer == nullptr) {
				return;
			}
			deviceContext_->CopyResource(backBuffer, rt.GetRenderTarget());
			backBuffer->Release();
		}


		bool D3D11GraphicsDeviceImpl::CreateDeviceAndSwapChain(HWND hwnd, uint32_t width, uint32_t height)
		{
			uint32_t createDeviceFlags = 0;
#ifdef _DEBUG
			createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
			D3D_DRIVER_TYPE driverTypes[] = {
				D3D_DRIVER_TYPE_HARDWARE,
				D3D_DRIVER_TYPE_WARP,
				D3D_DRIVER_TYPE_REFERENCE,
			};
			D3D_FEATURE_LEVEL featureLevels[] = {
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1,
				D3D_FEATURE_LEVEL_10_0,
			};

			DXGI_SWAP_CHAIN_DESC desc = {};
			desc.BufferCount = 1;
			desc.BufferDesc.Width = width;
			desc.BufferDesc.Height = height;
			desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BufferDesc.RefreshRate.Numerator = 60;
			desc.BufferDesc.RefreshRate.Denominator = 1;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow = hwnd;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Windowed = TRUE;
			desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

			HRESULT hr = E_FAIL;
			for (const auto driverType : driverTypes) {
				driverType_ = driverType;
				hr = D3D11CreateDeviceAndSwapChain(
					nullptr, driverType_, nullptr, createDeviceFlags,
					featureLevels, ARRAYSIZE(featureLevels),
					D3D11_SDK_VERSION, &desc,
					&swapChain_, &device_, &featureLevel_, &deviceContext_
				);
				if (SUCCEEDED(hr)) {
					break;
				}
			}
			return SUCCEEDED(hr);
		}


		void D3D11GraphicsDeviceImpl::SetupDefaultRenderState(RenderContext& context)
		{
			D3D11_RASTERIZER_DESC desc = {};
			desc.FillMode = D3D11_FILL_SOLID;
			desc.CullMode = D3D11_CULL_BACK;
			desc.DepthClipEnable = false;
			HRESULT hr = device_->CreateRasterizerState(&desc, &rasterizerState_);
			EngineAssert(SUCCEEDED(hr));
			context.GetImplAs<D3D11RenderContextImpl>()->RSSetState(rasterizerState_);
		}


		ID3D11Device* D3D11GraphicsDeviceImpl::GetStaticDevice()
		{
			return static_cast<D3D11GraphicsDeviceImpl*>(
				GraphicsDevice::Get().GetImplRaw()
			)->device_;
		}

		ID3D11DeviceContext* D3D11GraphicsDeviceImpl::GetStaticDeviceContext()
		{
			return static_cast<D3D11GraphicsDeviceImpl*>(
				GraphicsDevice::Get().GetImplRaw()
			)->deviceContext_;
		}


		std::unique_ptr<IVertexBuffer> D3D11GraphicsDeviceImpl::CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vb = std::make_unique<VertexBuffer>();
			if (!vb->Create(vertexNum, stride, data)) return nullptr;
			return vb;
		}

		std::unique_ptr<IVertexBuffer> D3D11GraphicsDeviceImpl::CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data)
		{
			auto vb = std::make_unique<DynamicVertexBuffer>();
			if (!vb->Create(vertexNum, stride, data)) return nullptr;
			return vb;
		}

		std::unique_ptr<IIndexBuffer> D3D11GraphicsDeviceImpl::CreateIndexBuffer(uint32_t indexNum, const void* data)
		{
			auto ib = std::make_unique<IndexBuffer>();
			if (!ib->Create(indexNum, data)) return nullptr;
			return ib;
		}

		std::unique_ptr<IIndexBuffer> D3D11GraphicsDeviceImpl::CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data)
		{
			auto ib = std::make_unique<IndexBuffer>();
			if (!ib->CreateDynamic(indexNum, format, data)) return nullptr;
			return ib;
		}

		std::unique_ptr<IConstantBuffer> D3D11GraphicsDeviceImpl::CreateConstantBuffer(const void* data, uint32_t size)
		{
			auto cb = std::make_unique<ConstantBuffer>();
			if (!cb->Create(data, size)) return nullptr;
			return cb;
		}

		std::unique_ptr<IShader> D3D11GraphicsDeviceImpl::CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type)
		{
			auto shader = std::make_unique<Shader>();
			if (!shader->Load(filePath, entryFunc, type)) return nullptr;
			return shader;
		}

		std::unique_ptr<ISamplerState> D3D11GraphicsDeviceImpl::CreateSamplerState(const SamplerDesc& desc)
		{
			auto ss = std::make_unique<SamplerState>();
			if (!ss->Create(desc)) return nullptr;
			return ss;
		}

		std::unique_ptr<IDepthMap> D3D11GraphicsDeviceImpl::CreateDepthMap(uint32_t width, uint32_t height)
		{
			auto dm = std::make_unique<D3D11DepthMap>();
			if (!dm->Create(width)) return nullptr;
			return dm;
		}


		bool D3D11GraphicsDeviceImpl::CreateMainRenderTargets(uint32_t width, uint32_t height)
		{
			SampleDesc sampleDesc;
			for (uint32_t i = 0; i < MAIN_RT_COUNT; ++i) {
				// メイン RT は HDR (R16G16B16A16_FLOAT)。PBR ライティングの 1.0 超の値をクランプせず保持し、
				// ポストプロセス(Bloom 合成)でトーンマップして LDR バックバッファへ出力する。
				bool ret = mainRenderTargets_[i].Create(
					width, height, 1,
					PixelFormat::R16G16B16A16_Float,
					PixelFormat::D24_Unorm_S8_Uint,
					sampleDesc
				);
				if (!ret) {
					return false;
				}
			}
			return true;
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D11
