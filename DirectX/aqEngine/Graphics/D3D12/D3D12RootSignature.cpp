#include "aq.h"
#include "D3D12Common.h"
#include "D3D12RootSignature.h"
#include <d3dcompiler.h>


namespace aq
{
	namespace graphics
	{
		bool D3D12RootSignature::Create(ID3D12Device* device)
		{
			Release();
			if (!device) return false;

			// ルートパラメータ
			D3D12_ROOT_PARAMETER params[MAX_ROOT_CBV + 1] = {};

			// [0..MAX_ROOT_CBV-1] ルート CBV b0..b(N-1)
			for (uint32_t i = 0; i < MAX_ROOT_CBV; ++i)
			{
				params[i].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
				params[i].Descriptor.ShaderRegister = i;     // bi
				params[i].Descriptor.RegisterSpace  = 0;
				params[i].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
			}

			// [PARAM_SRV_TABLE] SRV ディスクリプタテーブル t0..t7
			D3D12_DESCRIPTOR_RANGE srvRange = {};
			srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			srvRange.NumDescriptors                    = SRV_TABLE_SIZE;
			srvRange.BaseShaderRegister                = 0;  // t0
			srvRange.RegisterSpace                     = 0;
			srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

			params[PARAM_SRV_TABLE].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			params[PARAM_SRV_TABLE].DescriptorTable.NumDescriptorRanges = 1;
			params[PARAM_SRV_TABLE].DescriptorTable.pDescriptorRanges   = &srvRange;
			params[PARAM_SRV_TABLE].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

			// 静的サンプラー s0 = Linear/Clamp, s1 = Linear/Wrap
			D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
			for (uint32_t i = 0; i < 2; ++i)
			{
				samplers[i].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
				samplers[i].MipLODBias       = 0.0f;
				samplers[i].MaxAnisotropy    = 1;
				samplers[i].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
				samplers[i].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
				samplers[i].MinLOD           = 0.0f;
				samplers[i].MaxLOD           = D3D12_FLOAT32_MAX;
				samplers[i].ShaderRegister   = i;   // s0 / s1
				samplers[i].RegisterSpace    = 0;
				samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			}
			const D3D12_TEXTURE_ADDRESS_MODE clamp = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			const D3D12_TEXTURE_ADDRESS_MODE wrap  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = clamp;
			samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = wrap;

			D3D12_ROOT_SIGNATURE_DESC desc = {};
			desc.NumParameters     = MAX_ROOT_CBV + 1;
			desc.pParameters       = params;
			desc.NumStaticSamplers = 2;
			desc.pStaticSamplers   = samplers;
			desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

			ID3DBlob* blob  = nullptr;
			ID3DBlob* error = nullptr;
			HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
			if (FAILED(hr))
			{
				if (error)
				{
					OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
					error->Release();
				}
				EngineAssertMsg(false, "D3D12 ルートシグネチャのシリアライズ失敗");
				return false;
			}
			if (error) error->Release();

			hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
			                                 IID_PPV_ARGS(&rootSignature_));
			blob->Release();
			if (FAILED(hr))
			{
				EngineAssertMsg(false, "D3D12 ルートシグネチャ作成失敗");
				return false;
			}
			return true;
		}


		void D3D12RootSignature::Release()
		{
			SafeReleaseD3D12(rootSignature_);
		}
	}
}
