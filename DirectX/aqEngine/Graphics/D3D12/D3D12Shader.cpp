#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12Shader.h"
#include "Engine.h"   // aq::Engine::GetContentRoot()
#include <d3dcompiler.h>
#include <filesystem>

#pragma comment(lib, "d3dcompiler.lib")


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// プロジェクトルート (Game/Assets を含むディレクトリ) を探す
			std::string FindProjectRoot()
			{
				static std::string cached;
				if (!cached.empty()) return cached;

				// UWP 等でプラットフォームがコンテンツ基点(パッケージ install フォルダ)を
				// 返す場合はそれを採用し、ソースツリーの上方探索は行わない。
				if (const char* contentRoot = aq::Engine::Get().GetContentRoot()) {
					cached = contentRoot;
					return cached;
				}

				std::error_code ec;
				std::filesystem::path dir = std::filesystem::current_path(ec);
				if (ec) return std::string();

				while (!dir.empty())
				{
					if (std::filesystem::exists(dir / "Game" / "Assets", ec) && !ec)
					{
						cached = dir.generic_string();
						return cached;
					}
					if (dir == dir.root_path()) break;
					dir = dir.parent_path();
				}
				cached = std::filesystem::current_path(ec).generic_string();
				return cached;
			}

			// 与えられたパスを解決して fopen 可能なパスを返す (D3D11 層と同じ探索規則)
			std::string ResolveShaderPath(const char* filePath)
			{
				std::string path = filePath ? filePath : "";
				std::replace(path.begin(), path.end(), '\\', '/');

				if (std::filesystem::path(path).is_absolute()) return path;

				// UWP でもパッケージ内にソースツリー相対構造(Game/Assets/... と aqEngine/Graphics/...)
				// を再現して同梱するため、デスクトップと同じ "Game/" プレフィクス規則で解決する。
				const std::filesystem::path root(FindProjectRoot());
				std::filesystem::path candidate;
				if (path.rfind("Assets/", 0) == 0)        candidate = root / "Game" / path;
				else if (path.rfind("Game/Assets/", 0) == 0) candidate = root / path;
				else                                       candidate = root / path;

				std::error_code ec;
				if (std::filesystem::exists(candidate, ec)) return candidate.generic_string();
				return path;
			}

			std::string GetDirectoryPath(const std::string& path)
			{
				const size_t slash = path.find_last_of('/');
				return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
			}

			DXGI_FORMAT ComponentFormat(BYTE mask, D3D_REGISTER_COMPONENT_TYPE type)
			{
				// mask: 1=x, 3=xy, 7=xyz, 15=xyzw
				if (mask == 1)
				{
					if (type == D3D_REGISTER_COMPONENT_UINT32)  return DXGI_FORMAT_R32_UINT;
					if (type == D3D_REGISTER_COMPONENT_SINT32)  return DXGI_FORMAT_R32_SINT;
					return DXGI_FORMAT_R32_FLOAT;
				}
				if (mask <= 3)
				{
					if (type == D3D_REGISTER_COMPONENT_UINT32)  return DXGI_FORMAT_R32G32_UINT;
					if (type == D3D_REGISTER_COMPONENT_SINT32)  return DXGI_FORMAT_R32G32_SINT;
					return DXGI_FORMAT_R32G32_FLOAT;
				}
				if (mask <= 7)
				{
					if (type == D3D_REGISTER_COMPONENT_UINT32)  return DXGI_FORMAT_R32G32B32_UINT;
					if (type == D3D_REGISTER_COMPONENT_SINT32)  return DXGI_FORMAT_R32G32B32_SINT;
					return DXGI_FORMAT_R32G32B32_FLOAT;
				}
				if (type == D3D_REGISTER_COMPONENT_UINT32)  return DXGI_FORMAT_R32G32B32A32_UINT;
				if (type == D3D_REGISTER_COMPONENT_SINT32)  return DXGI_FORMAT_R32G32B32A32_SINT;
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
		}


		bool D3D12Shader::Load(const char* filePath, const char* entryFuncName, ShaderType shaderType)
		{
			Release();
			type_ = shaderType;

			const std::string resolved = ResolveShaderPath(filePath);

			// ファイル読み込み
			static char shaderBuffer[5 * 1024 * 1024];
			uint32_t fileSize = 0;
			{
				FILE* fp = nullptr;
				if (fopen_s(&fp, resolved.c_str(), "rb") != 0 || !fp)
				{
					EngineAssertMsg(false, "D3D12 シェーダファイルを開けません");
					return false;
				}
				fseek(fp, 0, SEEK_END);
				fileSize = static_cast<uint32_t>(ftell(fp));
				fseek(fp, 0, SEEK_SET);
				fread(shaderBuffer, fileSize, 1, fp);
				fclose(fp);
			}

			uint32_t flags = 0;
#ifdef _DEBUG
			flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
			static const char* models[] = { "vs_5_0", "ps_5_0", "cs_5_0" };

			// #include 解決のためカレントディレクトリを一時的に切り替える
			char prevDir[MAX_PATH] = {};
			GetCurrentDirectoryA(MAX_PATH, prevDir);
			SetCurrentDirectoryA(GetDirectoryPath(resolved).c_str());

			ID3DBlob* errorBlob = nullptr;
			HRESULT hr = D3DCompile(
				shaderBuffer, fileSize, nullptr, nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE, entryFuncName,
				models[static_cast<uint32_t>(shaderType)],
				flags, 0, &blob_, &errorBlob);

			SetCurrentDirectoryA(prevDir);

			if (FAILED(hr))
			{
				if (errorBlob)
				{
					OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
					errorBlob->Release();
				}
				EngineAssertMsg(false, "D3D12 シェーダコンパイルエラー");
				return false;
			}
			if (errorBlob) errorBlob->Release();

			if (type_ == ShaderType::VS) BuildInputLayout();
			return true;
		}


		void D3D12Shader::BuildInputLayout()
		{
			elements_.clear();
			semanticNames_.clear();

			ID3D12ShaderReflection* reflection = nullptr;
			if (FAILED(D3DReflect(blob_->GetBufferPointer(), blob_->GetBufferSize(),
			                      IID_PPV_ARGS(&reflection))))
			{
				return;
			}

			D3D12_SHADER_DESC shaderDesc = {};
			reflection->GetDesc(&shaderDesc);

			// SemanticName のポインタ寿命を確保するため先に文字列を確定させる
			semanticNames_.reserve(shaderDesc.InputParameters);
			for (uint32_t i = 0; i < shaderDesc.InputParameters; ++i)
			{
				D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
				reflection->GetInputParameterDesc(i, &paramDesc);
				semanticNames_.push_back(paramDesc.SemanticName ? paramDesc.SemanticName : "");
			}
			for (uint32_t i = 0; i < shaderDesc.InputParameters; ++i)
			{
				D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
				reflection->GetInputParameterDesc(i, &paramDesc);

				D3D12_INPUT_ELEMENT_DESC elem = {};
				elem.SemanticName         = semanticNames_[i].c_str();
				elem.SemanticIndex        = paramDesc.SemanticIndex;
				elem.Format               = ComponentFormat(paramDesc.Mask, paramDesc.ComponentType);
				elem.InputSlot            = 0;
				elem.AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
				elem.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				elem.InstanceDataStepRate = 0;
				elements_.push_back(elem);
			}

			reflection->Release();
		}


		void D3D12Shader::Release()
		{
			SafeReleaseD3D12(blob_);
			elements_.clear();
			semanticNames_.clear();
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D12
