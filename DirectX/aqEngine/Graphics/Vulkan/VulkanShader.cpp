#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <dxc/dxcapi.h>
#include <spirv_reflect/spirv_reflect.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace aq
{
	namespace graphics
	{
		namespace
		{
			// ── パス解決 (D3D12Shader と同じ規則) ──
			std::string FindProjectRoot()
			{
				static std::string cached;
				if (!cached.empty()) return cached;
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

			std::string ResolveShaderPath(const char* filePath)
			{
				std::string path = filePath ? filePath : "";
				std::replace(path.begin(), path.end(), '\\', '/');
				if (std::filesystem::path(path).is_absolute()) return path;
				const std::filesystem::path root(FindProjectRoot());
				std::filesystem::path candidate;
				if (path.rfind("Assets/", 0) == 0)            candidate = root / "Game" / path;
				else if (path.rfind("Game/Assets/", 0) == 0)  candidate = root / path;
				else                                          candidate = root / path;
				std::error_code ec;
				if (std::filesystem::exists(candidate, ec)) return candidate.generic_string();
				return path;
			}

			std::wstring ToWide(const std::string& s)
			{
				if (s.empty()) return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
				std::wstring w(n, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
				return w;
			}

			const wchar_t* ProfileFor(IShader::ShaderType t)
			{
				switch (t)
				{
				case IShader::ShaderType::VS: return L"vs_6_0";
				case IShader::ShaderType::PS: return L"ps_6_0";
				case IShader::ShaderType::CS: return L"cs_6_0";
				}
				return L"vs_6_0";
			}

			// SPIRV-Reflect / Vulkan 共通フォーマットのバイトサイズ (頂点入力で使う範囲)。
			uint32_t FormatByteSize(VkFormat f)
			{
				switch (f)
				{
				case VK_FORMAT_R32_SFLOAT:          case VK_FORMAT_R32_UINT:  case VK_FORMAT_R32_SINT:          return 4;
				case VK_FORMAT_R32G32_SFLOAT:       case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT:     return 8;
				case VK_FORMAT_R32G32B32_SFLOAT:    case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32_SINT: return 12;
				case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: return 16;
				default: return 0;
				}
			}
		}


		bool VulkanShader::Load(const char* filePath, const char* entryFuncName, ShaderType shaderType)
		{
			Release();
			type_ = shaderType;

			const std::string resolved = ResolveShaderPath(filePath);
			if (!CompileToSpirv(resolved.c_str(), entryFuncName, shaderType)) return false;

			VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			ci.codeSize = spirv_.size() * sizeof(uint32_t);
			ci.pCode    = spirv_.data();
			if (!VK_VERIFY(vkCreateShaderModule(VulkanGraphicsDeviceImpl::GetStaticDevice(), &ci, nullptr, &module_)))
				return false;

			if (type_ == ShaderType::VS) BuildInputLayout();
			return true;
		}


		bool VulkanShader::CompileToSpirv(const char* resolvedPath, const char* entry, ShaderType type)
		{
			// ファイル読み込み
			std::string source;
			{
				FILE* fp = nullptr;
				if (fopen_s(&fp, resolvedPath, "rb") != 0 || !fp)
				{
					EngineAssertMsg(false, "Vulkan シェーダファイルを開けません");
					return false;
				}
				fseek(fp, 0, SEEK_END);
				long sz = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				source.resize((size_t)sz);
				fread(source.data(), 1, (size_t)sz, fp);
				fclose(fp);
			}

			ComPtr<IDxcUtils>          utils;
			ComPtr<IDxcCompiler3>      compiler;
			ComPtr<IDxcIncludeHandler> includeHandler;
			if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))    return false;
			if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) return false;
			utils->CreateDefaultIncludeHandler(&includeHandler);

			// #include 解決のため -I にシェーダディレクトリを渡し、CWD も一時的に合わせる。
			const std::filesystem::path shaderPath(resolvedPath);
			const std::wstring includeDir = shaderPath.parent_path().wstring();
			const std::wstring entryW     = ToWide(entry ? entry : "main");

			std::vector<LPCWSTR> args = {
				L"-spirv",
				L"-fspv-entrypoint-name=main",   // SPIR-V エントリ名を main に固定
				L"-fvk-use-dx-layout",           // cbuffer を D3D パッキングに合わせる (CPU 構造体と一致)
				L"-E", entryW.c_str(),
				L"-T", ProfileFor(type),
				// register→binding 写像 (設計 §5.1)
				L"-fvk-b-shift", L"0",  L"all",
				L"-fvk-t-shift", L"16", L"all",
				L"-fvk-s-shift", L"32", L"all",
				L"-fvk-u-shift", L"48", L"all",
				L"-I", includeDir.c_str(),
			};
#ifdef _DEBUG
			args.push_back(L"-Zi");
			args.push_back(L"-Qembed_debug");
#endif

			DxcBuffer srcBuf{};
			srcBuf.Ptr      = source.data();
			srcBuf.Size     = source.size();
			srcBuf.Encoding = DXC_CP_UTF8;

			wchar_t prevDir[MAX_PATH] = {};
			GetCurrentDirectoryW(MAX_PATH, prevDir);
			SetCurrentDirectoryW(includeDir.c_str());

			ComPtr<IDxcResult> result;
			HRESULT hr = compiler->Compile(&srcBuf, args.data(), (UINT32)args.size(),
			                               includeHandler.Get(), IID_PPV_ARGS(&result));

			SetCurrentDirectoryW(prevDir);

			if (SUCCEEDED(hr) && result)
			{
				ComPtr<IDxcBlobUtf8> errors;
				result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
				if (errors && errors->GetStringLength() > 0)
					OutputDebugStringA(errors->GetStringPointer());
				result->GetStatus(&hr);
			}
			if (FAILED(hr))
			{
				EngineAssertMsg(false, "Vulkan シェーダ DXC コンパイルエラー");
				return false;
			}

			ComPtr<IDxcBlob> object;
			result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
			if (!object || object->GetBufferSize() == 0) return false;

			const size_t words = object->GetBufferSize() / sizeof(uint32_t);
			spirv_.resize(words);
			std::memcpy(spirv_.data(), object->GetBufferPointer(), words * sizeof(uint32_t));
			return true;
		}


		void VulkanShader::BuildInputLayout()
		{
			attributes_.clear();
			vertexStride_ = 0;

			SpvReflectShaderModule mod{};
			if (spvReflectCreateShaderModule(spirv_.size() * sizeof(uint32_t), spirv_.data(), &mod) != SPV_REFLECT_RESULT_SUCCESS)
				return;

			uint32_t count = 0;
			spvReflectEnumerateInputVariables(&mod, &count, nullptr);
			std::vector<SpvReflectInterfaceVariable*> inputs(count);
			spvReflectEnumerateInputVariables(&mod, &count, inputs.data());

			// 組み込み変数 (SV_*) を除外し location 昇順に並べる。
			std::vector<SpvReflectInterfaceVariable*> userInputs;
			for (auto* v : inputs)
				if (v && v->built_in == (SpvBuiltIn)-1 && v->location != 0xFFFFFFFF)
					userInputs.push_back(v);
			std::sort(userInputs.begin(), userInputs.end(),
			          [](auto* a, auto* b) { return a->location < b->location; });

			// パック済みレイアウト (CPU の VertexData / SkinnedVertexData と一致) を仮定し
			// location 順にオフセットを積む (D3D12 の APPEND_ALIGNED と同じ思想)。
			uint32_t offset = 0;
			for (auto* v : userInputs)
			{
				VkVertexInputAttributeDescription a{};
				a.location = v->location;
				a.binding  = 0;
				a.format   = (VkFormat)v->format;
				a.offset   = offset;
				attributes_.push_back(a);
				offset += FormatByteSize((VkFormat)v->format);
			}
			vertexStride_ = offset;

			spvReflectDestroyShaderModule(&mod);
		}


		void VulkanShader::Release()
		{
			if (module_)
			{
				VkDevice dev = VulkanGraphicsDeviceImpl::GetStaticDevice();
				if (dev) vkDestroyShaderModule(dev, module_, nullptr);
				module_ = VK_NULL_HANDLE;
			}
			spirv_.clear();
			attributes_.clear();
			vertexStride_ = 0;
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
