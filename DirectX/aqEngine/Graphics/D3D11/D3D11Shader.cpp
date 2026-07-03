#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D11
#include "D3D11Shader.h"
#include "D3D11GraphicsDeviceImpl.h"
#include <filesystem>

namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** ファイル読み込み。戻り値 false = ファイルが開けなかった */
			void PushUniquePath(std::vector<std::string>& paths, const std::string& path)
			{
				if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end()) {
					paths.push_back(path);
				}
			}

			std::string FindProjectRoot()
			{
				static std::string cachedRoot;
				if (!cachedRoot.empty()) {
					return cachedRoot;
				}

				std::error_code ec;
				std::filesystem::path dir = std::filesystem::current_path(ec);
				if (ec) {
					return std::string();
				}

				while (!dir.empty()) {
					if (std::filesystem::exists(dir / "Game" / "Assets", ec) && !ec) {
						cachedRoot = dir.generic_string();
						return cachedRoot;
					}
					if (dir == dir.root_path()) {
						break;
					}
					dir = dir.parent_path();
				}

				cachedRoot = std::filesystem::current_path(ec).generic_string();
				return cachedRoot;
			}

			std::vector<std::string> BuildPathCandidates(std::string path)
			{
				std::replace(path.begin(), path.end(), '\\', '/');

				std::vector<std::string> paths;
				PushUniquePath(paths, path);

				std::filesystem::path fsPath(path);
				if (fsPath.is_absolute()) {
					return paths;
				}

				const std::filesystem::path root(FindProjectRoot());
				if (path.rfind("Assets/", 0) == 0) {
					PushUniquePath(paths, (root / "Game" / path).generic_string());
				}
				else if (path.rfind("Game/Assets/", 0) == 0) {
					PushUniquePath(paths, (root / path).generic_string());
				}
				else {
					PushUniquePath(paths, (root / path).generic_string());
				}
				return paths;
			}

			std::string GetDirectoryPath(const std::string& path)
			{
				const size_t slash = path.find_last_of('/');
				if (slash == std::string::npos) {
					return ".";
				}
				return path.substr(0, slash);
			}

			bool ReadFile(const char* filePath, char* readBuffer, uint32_t& fileSize, std::string& openedPath)
			{
				FILE* fp = nullptr;
				for (const std::string& path : BuildPathCandidates(filePath ? filePath : "")) {
					if (fopen_s(&fp, path.c_str(), "rb") == 0 && fp) {
						openedPath = path;
						break;
					}
				}
				if (!fp) return false;
				fseek(fp, 0, SEEK_END);
				fpos_t fPos;
				fgetpos(fp, &fPos);
				fseek(fp, 0, SEEK_SET);
				fileSize = static_cast<uint32_t>(fPos);
				fread(readBuffer, fileSize, 1, fp);
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
			static char shaderBuffer[5 * 1024 * 1024];
			uint32_t fileSize = 0;
			std::string openedPath;
			if (!ReadFile(filePath, shaderBuffer, fileSize, openedPath)) return false;

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
			char currentDirectory[MAX_PATH] = {};
			GetCurrentDirectoryA(MAX_PATH, currentDirectory);
			SetCurrentDirectoryA(GetDirectoryPath(openedPath).c_str());
			HRESULT hr = D3DCompile(
				shaderBuffer, fileSize, nullptr, nullptr,
				((ID3DInclude*)(UINT_PTR)1), entryFuncName,
				shaderModel,
				dwordShaderFlags, 0, &blob_, &errorBlob);
			SetCurrentDirectoryA(currentDirectory);

			if (FAILED(hr)) {
				if (errorBlob) {
					static char text[5 * 1024];
					snprintf(text, ArraySize(text), "[Shader Error] %s\n%s",
					         filePath, (char*)errorBlob->GetBufferPointer());
					OutputDebugStringA(text);
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
