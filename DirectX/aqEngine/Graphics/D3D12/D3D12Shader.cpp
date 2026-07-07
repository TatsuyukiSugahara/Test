#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12Shader.h"
#include <filesystem>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")


namespace aq
{
	namespace graphics
	{
		namespace
		{
			// プロジェクトルート (Game/Assets を含むディレクトリ) を探す。
			// ワーカースレッドから並列に呼ばれるため、C++11 のスレッドセーフな static 初期化で一度だけ算出する。
			std::string FindProjectRoot()
			{
				static const std::string cached = []() -> std::string
				{
					// UWP 等でプラットフォームがコンテンツ基点(パッケージ install フォルダ)を
					// 返す場合はそれを採用し、ソースツリーの上方探索は行わない。
					if (const char* contentRoot = aq::Engine::Get().GetContentRoot()) {
						return contentRoot;
					}

					std::error_code ec;
					std::filesystem::path dir = std::filesystem::current_path(ec);
					if (ec) return std::string();

					while (!dir.empty())
					{
						if (std::filesystem::exists(dir / "Game" / "Assets", ec) && !ec)
						{
							return dir.generic_string();
						}
						if (dir == dir.root_path()) break;
						dir = dir.parent_path();
					}
					return std::filesystem::current_path(ec).generic_string();
				}();
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


			// #include をシェーダのディレクトリ基準で解決する ID3DInclude 実装。
			// SetCurrentDirectory(プロセス全体の CWD 変更) を使わずに済むため、コンパイルを
			// ワーカースレッドで安全に実行できる(他スレッドのファイル I/O を壊さない)。
			class ShaderIncludeHandler : public ID3DInclude
			{
			public:
				explicit ShaderIncludeHandler(std::string baseDir) : baseDir_(std::move(baseDir)) {}

				HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR fileName,
				                       LPCVOID /*parentData*/, LPCVOID* outData, UINT* outBytes) override
				{
					const std::filesystem::path full = std::filesystem::path(baseDir_) / fileName;
					FILE* fp = nullptr;
					if (fopen_s(&fp, full.string().c_str(), "rb") != 0 || !fp) return E_FAIL;
					fseek(fp, 0, SEEK_END);
					const long size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					if (size <= 0) { fclose(fp); return E_FAIL; }
					char* buffer = new char[static_cast<size_t>(size)];
					const size_t read = fread(buffer, 1, static_cast<size_t>(size), fp);
					fclose(fp);
					*outData  = buffer;
					*outBytes = static_cast<UINT>(read);
					return S_OK;
				}

				HRESULT __stdcall Close(LPCVOID data) override
				{
					delete[] static_cast<const char*>(data);
					return S_OK;
				}

			private:
				std::string baseDir_;
			};

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

			// ファイル読み込み (スレッド安全: ワーカースレッドからも呼べるようローカルバッファを使う。
			// 旧実装は 5MB の static バッファを共有していたため並列コンパイルで壊れる)
			std::vector<char> shaderBuffer;
			{
				FILE* fp = nullptr;
				if (fopen_s(&fp, resolved.c_str(), "rb") != 0 || !fp)
				{
					EngineAssertMsg(false, "D3D12 シェーダファイルを開けません");
					return false;
				}
				fseek(fp, 0, SEEK_END);
				const long fileSize = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				if (fileSize > 0)
				{
					shaderBuffer.resize(static_cast<size_t>(fileSize));
					fread(shaderBuffer.data(), 1, static_cast<size_t>(fileSize), fp);
				}
				fclose(fp);
			}

			uint32_t flags = 0;
#ifdef _DEBUG
			flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
			static const char* models[] = { "vs_5_0", "ps_5_0", "cs_5_0" };

			// #include はシェーダのディレクトリ基準で解決する。専用ハンドラを使うことで
			// SetCurrentDirectory(プロセス全体の CWD 変更) を避け、ワーカースレッドから安全にコンパイルできる。
			ShaderIncludeHandler includeHandler(GetDirectoryPath(resolved));

			ID3DBlob* errorBlob = nullptr;
			HRESULT hr = D3DCompile(
				shaderBuffer.data(), shaderBuffer.size(), resolved.c_str(), nullptr,
				&includeHandler, entryFuncName,
				models[static_cast<uint32_t>(shaderType)],
				flags, 0, &blob_, &errorBlob);

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

				// セマンティクスが "I_" 始まりの要素は per-instance ストリーム(slot1)として扱う。
				// 例: HLSL の I_WORLD0..3 は reflection 上 SemanticName="I_WORLD"/Index=0..3 に分解される
				// ため、完全一致でなく前方一致で判定する。AlignedByteOffset は APPEND なのでスロット別に
				// オフセットが自動計算される。
				const bool perInstance = semanticNames_[i].rfind("I_", 0) == 0;

				D3D12_INPUT_ELEMENT_DESC elem = {};
				elem.SemanticName         = semanticNames_[i].c_str();
				elem.SemanticIndex        = paramDesc.SemanticIndex;
				elem.Format               = ComponentFormat(paramDesc.Mask, paramDesc.ComponentType);
				elem.InputSlot            = perInstance ? 1u : 0u;
				elem.AlignedByteOffset    = D3D12_APPEND_ALIGNED_ELEMENT;
				elem.InputSlotClass       = perInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
				                                        : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				elem.InstanceDataStepRate = perInstance ? 1u : 0u;
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
