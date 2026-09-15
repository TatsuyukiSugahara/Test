#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "D3D11Shader.h"
#include "D3D11GraphicsDeviceImpl.h"
#include "Resource/AssetPath.h"

namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** シェーダの #include 解決に渡すディレクトリ */
			std::string GetDirectoryPath(const std::string& path)
			{
				const size_t slash = path.find_last_of("/\\");
				if (slash == std::string::npos) {
					return ".";
				}
				return path.substr(0, slash);
			}


			/**
			 * シェーダの #include をシェーダのあるディレクトリ基準で解決するハンドラ
			 *
			 * D3DCompile 標準の解決(D3D_COMPILE_STANDARD_FILE_INCLUDE)は #include を
			 * CWD 相対で探すため、以前はコンパイルを挟んでプロセス全体の CWD を
			 * 差し替えていた。シェーダのロードはワーカースレッドから並列に走るので、
			 * その窓の間に別スレッドが相対パスを解決すると外れる。自前で解決すれば
			 * CWD に触らずに済む(D3D12 側は同じ理由で先に自前実装へ移してある)。
			 */
			class ShaderIncludeHandler : public ID3DInclude
			{
			private:
				std::string baseDir_;


			public:
				explicit ShaderIncludeHandler(std::string baseDir) : baseDir_(std::move(baseDir)) {}


			public:
				HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*includeType*/, LPCSTR fileName,
				                       LPCVOID /*parentData*/, LPCVOID* outData, UINT* outBytes) override
				{
					if (!fileName || !outData || !outBytes) return E_FAIL;

					const std::string full = baseDir_ + "/" + fileName;
					FILE* fp = nullptr;
					if (fopen_s(&fp, full.c_str(), "rb") != 0 || !fp) return E_FAIL;

					fseek(fp, 0, SEEK_END);
					const long size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					if (size <= 0) {
						fclose(fp);
						return E_FAIL;
					}
					char* buffer = new char[static_cast<size_t>(size)];
					const size_t readSize = fread(buffer, 1, static_cast<size_t>(size), fp);
					fclose(fp);

					*outData  = buffer;
					*outBytes = static_cast<UINT>(readSize);
					return S_OK;
				}

				HRESULT __stdcall Close(LPCVOID data) override
				{
					delete[] static_cast<const char*>(data);
					return S_OK;
				}
			};

			/**
			 * ファイル読み込み。戻り値 false = ファイルが開けなかった
			 *
			 * 読み込み先は呼び出しごとのバッファ。シェーダのロードはワーカースレッドから
			 * 並列に走るため、ここを共有バッファにするとソースが互いに混ざる。
			 */
			bool ReadFile(const char* filePath, std::vector<char>& readBuffer, uint32_t& fileSize, std::string& openedPath)
			{
				const std::string requested = filePath ? filePath : "";

				std::vector<std::string> candidates;
				aq::res::BuildAssetPathCandidates(requested, candidates);

				FILE* fp = nullptr;
				for (const std::string& path : candidates) {
					if (fopen_s(&fp, path.c_str(), "rb") == 0 && fp) {
						openedPath = path;
						break;
					}
				}
				if (!fp) {
					aq::res::LogUnresolvedAssetPath(requested);
					return false;
				}
				fseek(fp, 0, SEEK_END);
				fpos_t fPos;
				fgetpos(fp, &fPos);
				fseek(fp, 0, SEEK_SET);
				fileSize = static_cast<uint32_t>(fPos);
				readBuffer.resize(fileSize);
				if (fileSize > 0) fread(readBuffer.data(), fileSize, 1, fp);
				fclose(fp);
				return true;
			}

			/** 頂点シェーダーから頂点レイアウトを生成 */
			HRESULT CreateInputLayoutDescFromVertexShaderSignature(ID3DBlob* shaderBlob, ID3D11Device* d3dDevice, ID3D11InputLayout** inputLayout)
			{
				ID3D11ShaderReflection* vertexShaderReflection = NULL;
				if (FAILED(D3DReflect(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&vertexShaderReflection))) {
					return S_FALSE;
				}

				D3D11_SHADER_DESC shaderDesc;
				vertexShaderReflection->GetDesc(&shaderDesc);

				std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayoutDescs;
				for (uint32_t i = 0; i < shaderDesc.InputParameters; ++i) {
					D3D11_SIGNATURE_PARAMETER_DESC parameterDesc;
					vertexShaderReflection->GetInputParameterDesc(i, &parameterDesc);

					D3D11_INPUT_ELEMENT_DESC elementDesc;
					elementDesc.SemanticName         = parameterDesc.SemanticName;
					elementDesc.SemanticIndex        = parameterDesc.SemanticIndex;
					elementDesc.InputSlot            = 0;
					elementDesc.AlignedByteOffset    = D3D11_APPEND_ALIGNED_ELEMENT;
					elementDesc.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
					elementDesc.InstanceDataStepRate = 0;

					if (parameterDesc.Mask == 1) {
						if      (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32_FLOAT;
					} else if (parameterDesc.Mask <= 3) {
						if      (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
					} else if (parameterDesc.Mask <= 7) {
						if      (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
					} else if (parameterDesc.Mask <= 15) {
						if      (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  elementDesc.Format = DXGI_FORMAT_R32G32B32A32_SINT;
						else if (parameterDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) elementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					}

					inputLayoutDescs.push_back(elementDesc);
				}

				HRESULT hr = d3dDevice->CreateInputLayout(
					&inputLayoutDescs[0], static_cast<UINT>(inputLayoutDescs.size()),
					shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), inputLayout);

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
					case ShaderType::VS: static_cast<ID3D11VertexShader*>(shader_)->Release();  break;
					case ShaderType::PS: static_cast<ID3D11PixelShader*>(shader_)->Release();   break;
					case ShaderType::CS: static_cast<ID3D11ComputeShader*>(shader_)->Release(); break;
				}
				shader_ = nullptr;
			}
			if (inputLayout_) { inputLayout_->Release(); inputLayout_ = nullptr; }
			if (blob_)        { blob_->Release();        blob_        = nullptr; }
		}


		bool Shader::Load(const char* filePath, const char* entryFuncName, ShaderType shaderType)
		{
			Release();
			shaderType_ = shaderType;
			DWORD dwordShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
			dwordShaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
			std::vector<char> shaderSource;
			uint32_t fileSize = 0;
			std::string openedPath;
			if (!ReadFile(filePath, shaderSource, fileSize, openedPath)) return false;

			// シェーダモデルをデバイスの機能レベルに合わせる。SM5.0 は FL11_0 必須のため、
			// FL10_1(Xbox One UWP 等)では SM4.1、FL10_0 では SM4.0 でコンパイルする。
			// FL10 では SM5.0 専用機能を使うシェーダはコンパイル失敗する(非致命・nullptr 返し)。
			ID3D11Device* d3dDevice = D3D11GraphicsDeviceImpl::GetStaticDevice();
			const D3D_FEATURE_LEVEL fl = d3dDevice ? d3dDevice->GetFeatureLevel() : D3D_FEATURE_LEVEL_11_0;
			const char* verSuffix = (fl >= D3D_FEATURE_LEVEL_11_0) ? "5_0"
			                      : (fl >= D3D_FEATURE_LEVEL_10_1) ? "4_1"
			                      : "4_0";
			static const char* shaderPrefix[] = { "vs_", "ps_", "cs_" };
			char shaderModel[16] = {};
			sprintf_s(shaderModel, "%s%s", shaderPrefix[static_cast<uint32_t>(shaderType_)], verSuffix);

			ID3DBlob* errorBlob = nullptr;
			ShaderIncludeHandler includeHandler(GetDirectoryPath(openedPath));
			HRESULT hr = D3DCompile(
				shaderSource.data(), fileSize, openedPath.c_str(), nullptr,
				&includeHandler, entryFuncName,
				shaderModel,
				dwordShaderFlags, 0, &blob_, &errorBlob);

			if (FAILED(hr)) {
				if (errorBlob) {
					char text[5 * 1024];
					snprintf(text, ArraySize(text), "[Shader Error] %s\n%s",
					         filePath, (char*)errorBlob->GetBufferPointer());
					OutputDebugStringA(text);
					// デバッガを繋いでいないと OutputDebugString は誰にも見えないので、起動ログにも残す
					aq::StartupLog(text);
					errorBlob->Release();
				}
				{
					char b[220]; sprintf_s(b, "[shader] COMPILE FAIL %s (%s @%s)", filePath ? filePath : "", entryFuncName ? entryFuncName : "", shaderModel);
					aq::StartupLog(b);
				}
				EngineAssertMsg(false, "シェーダーコンパイルエラー");
				return false;
			}

			switch (shaderType_)
			{
				case ShaderType::VS:
				{
					hr = d3dDevice->CreateVertexShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11VertexShader**)&shader_);
					if (FAILED(hr)) { char b[220]; sprintf_s(b, "[shader] CreateVS FAIL %s hr=0x%08X", filePath?filePath:"", static_cast<unsigned>(hr)); aq::StartupLog(b); return false; }
					hr = CreateInputLayoutDescFromVertexShaderSignature(blob_, d3dDevice, &inputLayout_);
					if (FAILED(hr)) { char b[220]; sprintf_s(b, "[shader] InputLayout FAIL %s hr=0x%08X", filePath?filePath:"", static_cast<unsigned>(hr)); aq::StartupLog(b); return false; }
					break;
				}
				case ShaderType::PS:
				{
					hr = d3dDevice->CreatePixelShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11PixelShader**)&shader_);
					if (FAILED(hr)) { char b[220]; sprintf_s(b, "[shader] CreatePS FAIL %s hr=0x%08X", filePath?filePath:"", static_cast<unsigned>(hr)); aq::StartupLog(b); return false; }
					break;
				}
				case ShaderType::CS:
				{
					hr = d3dDevice->CreateComputeShader(blob_->GetBufferPointer(), blob_->GetBufferSize(), nullptr, (ID3D11ComputeShader**)&shader_);
					if (FAILED(hr)) { char b[220]; sprintf_s(b, "[shader] CreateCS FAIL %s hr=0x%08X", filePath?filePath:"", static_cast<unsigned>(hr)); aq::StartupLog(b); return false; }
					break;
				}
			}
			return true;
		}
	}
}

#endif // ENGINE_GRAPHICS_D3D11
