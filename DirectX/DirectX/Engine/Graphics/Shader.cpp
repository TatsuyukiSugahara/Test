#include "../EnginePreCompile.h"
#include "Shader.h"
#include "../Engine.h"

namespace engine
{
	namespace graphics
	{
		namespace
		{
			/** ファイル読み込み */
			void ReadFile(const char* filePath, char* readBuffer, uint32_t& fileSize)
			{
				FILE* fp = nullptr;
				fopen_s(&fp, filePath, "r");
				fseek(fp, 0, SEEK_END);
				fpos_t fPos;
				fgetpos(fp, &fPos);
				fseek(fp, 0, SEEK_SET);
				fileSize = static_cast<uint32_t>(fPos);
				fread(readBuffer, fileSize, 1, fp);
				fclose(fp);
			}

			/** 頂点シェーダーから頂点レイアウト生成 */
			HRESULT CreateInputLayoutDescFromVertexShaderSignature(ID3DBlob* shaderBlob, ID3D11Device* d3dDevice, ID3D11InputLayout** inputLayout)
			{
				// シェーダー情報からリフレクションを行う
				ID3D11ShaderReflection* vertexShaderReflection = NULL;
				if (FAILED(D3DReflect(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&vertexShaderReflection))) {
					return S_FALSE;
				}

				// シェーダー情報を取得
				D3D11_SHADER_DESC shaderDesc;
				vertexShaderReflection->GetDesc(&shaderDesc);

				// 入力情報定義を読み込み
				std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDescs;
				for (uint32_t i = 0; i < shaderDesc.InputParameters; ++i) {
					D3D11_SIGNATURE_PARAMETER_DESC parameterDesc;
					vertexShaderReflection->GetInputParameterDesc(i, &parameterDesc);

					// エレメント定義設定
					D3D11_INPUT_ELEMENT_DESC elementDesc;
					elementDesc.SemanticName = parameterDesc.SemanticName;
					elementDesc.SemanticIndex = parameterDesc.SemanticIndex;
					elementDesc.InputSlot = 0;
					elementDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
					elementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
					elementDesc.InstanceDataStepRate = 0;

					// DXGI format
					if (parameterDesc.Mask == 1) {
						if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)		elementDesc.Format = DXGI_FORMAT_R32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)	elementDesc.Format = DXGI_FORMAT_R32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
					}
					else if (parameterDesc.Mask <= 3) {
						if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)		elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)	elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
					}
					else if (parameterDesc.Mask <= 7) {
						if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)		elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)	elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
					}
					else if (parameterDesc.Mask <= 15) {
						if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)		elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)	elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					}

					// エレメント定義を保存
					inputLayoutDescs.push_back(elementDesc);
				}

				// 入力レイアウト生成
				HRESULT hr = d3dDevice->CreateInputLayout(&inputLayoutDescs[0], static_cast<UINT>(inputLayoutDescs.size()), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), inputLayout);

				// リフレクション用に確保したメモリを解放
				vertexShaderReflection->Release();

				return hr;
			}
		}


		Shader::Shader()
			: shaderType_(ShaderType::VS)
			, shader_(nullptr)
			, inputLayout_(nullptr)
			, blob_(nullptr)
		{
		}


		Shader::~Shader()
		{
			Release();
		}


		void Shader::Release()
		{
			if (shader_) {
				switch (shaderType_)
				{
					case ShaderType::VS:
					{
						static_cast<ID3D11VertexShader*>(shader_)->Release();
						break;
					}
					case ShaderType::PS:
					{
						static_cast<ID3D11PixelShader*>(shader_)->Release();
						break;
					}
					case ShaderType::CS:
					{
						static_cast<ID3D11ComputeShader*>(shader_)->Release();
						break;
					}
					shader_ = nullptr;
				}
			}
			if (inputLayout_) {
				inputLayout_->Release();
				inputLayout_ = nullptr;
			}
			if (blob_) {
				blob_->Release();
				blob_ = nullptr;
			}
		}


		bool Shader::Load(const char* filePath, const char* entryFuncName, ShaderType shaderType)
		{
			Release();
			shaderType_ = shaderType;
			HRESULT hr = S_OK;
			DWORD dwordShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
			dwordShaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

			ID3DBlob* errorBlob;
			// シェーダーを読み込み
			static char shaderBuffer[5 * 1024 * 1024];	// NOTE: 5MBほど。これくらいあれば足りるだろう
			uint32_t fileSize = 0;
			ReadFile(filePath, shaderBuffer, fileSize);
			static const char* shaderModelNames[] = {
				"vs_5_0",
				"ps_5_0",
				"cs_5_0",
			};
			SetCurrentDirectory(_T("Assets/Shader"));
			hr = D3DCompile(shaderBuffer, fileSize, nullptr, nullptr, ((ID3DInclude*)(UINT_PTR)1), entryFuncName,
							shaderModelNames[static_cast<uint32_t>(shaderType_)], dwordShaderFlags, 0, &blob_, &errorBlob);
			SetCurrentDirectory(_T("../../"));
			if (FAILED(hr)) {
				if (errorBlob) {
					static char text[5 * 1024];
					snprintf(text, ArraySize(text), "%s", (char*)errorBlob->GetBufferPointer());
					EngineAssertMsg(false, "シェーダーコンパイルエラー");
				}
				return false;
			}
			ID3D11Device* d3dDevice = Engine::Get().GetD3DDevice();
			switch (shaderType_)
			{
				case ShaderType::VS:
				{
					// 頂点シェーダー
					hr = d3dDevice->CreateVertexShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11VertexShader**)&shader_);
					if (FAILED(hr)) {
						return false;
					}
					// 入力レイアウト生成
					hr = CreateInputLayoutDescFromVertexShaderSignature(blob_, d3dDevice, &inputLayout_);
					if (FAILED(hr)) {
						return false;
					}
					break;
				}
				case ShaderType::PS:
				{
					// ピクセルシェーダー
					hr = d3dDevice->CreatePixelShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11PixelShader**)&shader_);
					if (FAILED(hr)) {
						return false;
					}
					break;
				}
				case ShaderType::CS:
				{
					hr = d3dDevice->CreateComputeShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11ComputeShader**)&shader_);
					if (FAILED(hr)) {
						return false;
					}
					break;
				}
			}

			return true;
		}
	}
}