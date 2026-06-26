#include "aq.h"
#include "D3D12Common.h"
#include "D3D12PipelineStateCache.h"
#include "D3D12Shader.h"


namespace aq
{
	namespace graphics
	{
		bool D3D12PipelineStateCache::Key::operator==(const Key& o) const
		{
			if (!(vs == o.vs && ps == o.ps && blend == o.blend && depth == o.depth
			      && topoType == o.topoType && rtCount == o.rtCount && dsFormat == o.dsFormat))
				return false;
			for (uint32_t i = 0; i < rtCount && i < MAX_RT; ++i)
				if (rtFormats[i] != o.rtFormats[i]) return false;
			return true;
		}


		size_t D3D12PipelineStateCache::KeyHash::operator()(const Key& k) const
		{
			// FNV-1a でメンバのバイト列をハッシュ
			size_t h = 1469598103934665603ull;
			auto mix = [&h](const void* p, size_t n)
			{
				const uint8_t* b = static_cast<const uint8_t*>(p);
				for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
			};
			mix(&k.vs, sizeof(k.vs));
			mix(&k.ps, sizeof(k.ps));
			mix(&k.blend, sizeof(k.blend));
			mix(&k.depth, sizeof(k.depth));
			mix(&k.topoType, sizeof(k.topoType));
			mix(&k.rtCount, sizeof(k.rtCount));
			for (uint32_t i = 0; i < k.rtCount && i < MAX_RT; ++i) mix(&k.rtFormats[i], sizeof(k.rtFormats[i]));
			mix(&k.dsFormat, sizeof(k.dsFormat));
			return h;
		}


		namespace
		{
			D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode)
			{
				D3D12_BLEND_DESC desc = {};
				desc.AlphaToCoverageEnable  = FALSE;
				desc.IndependentBlendEnable = FALSE;
				D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
				rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
				rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
				rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
				rt.DestBlendAlpha        = D3D12_BLEND_ZERO;
				rt.BlendOp               = D3D12_BLEND_OP_ADD;

				switch (mode)
				{
					case BlendMode::Opaque:
						rt.BlendEnable = FALSE;
						rt.SrcBlend    = D3D12_BLEND_ONE;
						rt.DestBlend   = D3D12_BLEND_ZERO;
						break;
					case BlendMode::AlphaBlend:
						rt.BlendEnable = TRUE;
						rt.SrcBlend    = D3D12_BLEND_SRC_ALPHA;
						rt.DestBlend   = D3D12_BLEND_INV_SRC_ALPHA;
						break;
					case BlendMode::Additive:
						rt.BlendEnable = TRUE;
						rt.SrcBlend    = D3D12_BLEND_ONE;
						rt.DestBlend   = D3D12_BLEND_ONE;
						break;
					case BlendMode::Premultiplied:
						rt.BlendEnable = TRUE;
						rt.SrcBlend    = D3D12_BLEND_ONE;
						rt.DestBlend   = D3D12_BLEND_INV_SRC_ALPHA;
						break;
				}
				return desc;
			}

			D3D12_DEPTH_STENCIL_DESC MakeDepthDesc(DepthMode mode)
			{
				D3D12_DEPTH_STENCIL_DESC desc = {};
				desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
				switch (mode)
				{
					case DepthMode::ReadWrite:
						desc.DepthEnable    = TRUE;
						desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
						break;
					case DepthMode::ReadOnly:
						desc.DepthEnable    = TRUE;
						desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
						break;
					case DepthMode::Disabled:
						desc.DepthEnable    = FALSE;
						desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
						break;
				}
				desc.StencilEnable = FALSE;
				return desc;
			}

			D3D12_RASTERIZER_DESC MakeRasterizerDesc()
			{
				D3D12_RASTERIZER_DESC desc = {};
				desc.FillMode              = D3D12_FILL_MODE_SOLID;
				desc.CullMode              = D3D12_CULL_MODE_BACK;
				desc.FrontCounterClockwise = FALSE;
				desc.DepthClipEnable       = FALSE;  // D3D11 既定設定に合わせる
				return desc;
			}
		}


		ID3D12PipelineState* D3D12PipelineStateCache::GetOrCreate(ID3D12Device* device, ID3D12RootSignature* rootSig,
		                                                          D3D12Shader* vs, D3D12Shader* ps, const Key& key)
		{
			auto it = cache_.find(key);
			if (it != cache_.end()) return it->second;

			if (!device || !rootSig || !vs) return nullptr;

			const D3D12_INPUT_ELEMENT_DESC* elements = nullptr;
			uint32_t elementCount = 0;
			vs->GetInputLayout(elements, elementCount);

			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
			desc.pRootSignature                = rootSig;
			desc.VS.pShaderBytecode            = vs->GetByteCode();
			desc.VS.BytecodeLength             = vs->GetByteCodeSize();
			if (ps)
			{
				desc.PS.pShaderBytecode = ps->GetByteCode();
				desc.PS.BytecodeLength  = ps->GetByteCodeSize();
			}
			desc.BlendState                    = MakeBlendDesc(key.blend);
			desc.SampleMask                    = UINT_MAX;
			desc.RasterizerState               = MakeRasterizerDesc();
			desc.DepthStencilState             = MakeDepthDesc(key.depth);
			desc.InputLayout.pInputElementDescs = elements;
			desc.InputLayout.NumElements        = elementCount;
			desc.PrimitiveTopologyType         = key.topoType;
			desc.NumRenderTargets              = key.rtCount;
			for (uint32_t i = 0; i < key.rtCount && i < MAX_RT; ++i)
				desc.RTVFormats[i] = key.rtFormats[i];
			desc.DSVFormat                     = key.dsFormat;
			desc.SampleDesc.Count              = 1;

			ID3D12PipelineState* pso = nullptr;
			HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
			if (FAILED(hr))
			{
				EngineAssertMsg(false, "D3D12 PSO 生成失敗");
				return nullptr;
			}
			cache_[key] = pso;
			return pso;
		}


		ID3D12PipelineState* D3D12PipelineStateCache::GetOrCreateCompute(
			ID3D12Device* device, ID3D12RootSignature* rootSig, D3D12Shader* cs)
		{
			if (!device || !rootSig || !cs) return nullptr;
			const void* key = cs->GetByteCode();
			auto it = computeCache_.find(key);
			if (it != computeCache_.end()) return it->second;

			D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
			desc.pRootSignature     = rootSig;
			desc.CS.pShaderBytecode = cs->GetByteCode();
			desc.CS.BytecodeLength  = cs->GetByteCodeSize();

			ID3D12PipelineState* pso = nullptr;
			HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
			if (FAILED(hr)) { EngineAssertMsg(false, "D3D12 コンピュート PSO 生成失敗"); return nullptr; }
			computeCache_[key] = pso;
			return pso;
		}


		void D3D12PipelineStateCache::Release()
		{
			for (auto& kv : cache_)        { if (kv.second) kv.second->Release(); }
			for (auto& kv : computeCache_) { if (kv.second) kv.second->Release(); }
			cache_.clear();
			computeCache_.clear();
		}
	}
}
