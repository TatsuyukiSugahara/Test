#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include <spirv_reflect/spirv_reflect.h>
#include <cstdio>
#include <filesystem>

#if defined(AQ_PLATFORM_WIN32)
// DXC の実行時コンパイル経路は Windows 専用。Mac は事前ビルドした .spv だけを使う。
#include <dxc/dxcapi.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace aq
{
	namespace graphics
	{
		namespace
		{
			// SPIR-V バイナリ先頭のマジックナンバー (リトルエンディアンで読んだ値)。
			static constexpr uint32_t SPIRV_MAGIC = 0x07230203u;

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

			// ステージ名 (.spv のファイル名と DXC プロファイルの接頭辞で共通)。
			const char* StageSuffix(IShader::ShaderType t)
			{
				switch (t)
				{
				case IShader::ShaderType::VS: return "vs";
				case IShader::ShaderType::PS: return "ps";
				case IShader::ShaderType::CS: return "cs";
				}
				return "vs";
			}

			// 事前ビルドした SPIR-V の探索パス: <shaderDir>/spv/<stem>.<entry>.<stage>.spv
			// Tools/ShaderCompile/compile_spv.cmake の出力名と一対一で対応させること。
			// Mac 実機で確認済み(P2): dxc CLI 出力の .spv だけで全シェーダが生成でき、
			// BuildInputLayout()(SPIRV-Reflect)も入力レイアウトを正しく返す。
			// Mac は実行時 DXC を持たないため、失敗すれば起動できない = この経路が
			// 使われている証明になる。Windows Vulkan 構成での「.spv あり/なし双方で
			// 見た目が一致する」比較は Mac移植設計.md §9 P2 に残っている。
			std::string BuildSpirvPath(const char* resolvedPath, const char* entry, IShader::ShaderType type)
			{
				const std::filesystem::path src(resolvedPath ? resolvedPath : "");
				if (src.empty()) return std::string();
				const std::string name = src.stem().string() + "."
				                       + (entry && entry[0] ? entry : "main") + "."
				                       + StageSuffix(type) + ".spv";
				return (src.parent_path() / "spv" / name).generic_string();
			}

			// ファイル全体をバイト列として読む。
			bool ReadWholeFile(const char* path, std::vector<char>& out)
			{
				out.clear();
				std::FILE* fp = std::fopen(path, "rb");
				if (!fp) return false;
				std::fseek(fp, 0, SEEK_END);
				const long sz = std::ftell(fp);
				std::fseek(fp, 0, SEEK_SET);
				if (sz < 0)
				{
					std::fclose(fp);
					return false;
				}
				out.resize((size_t)sz);
				const size_t read = (sz > 0) ? std::fread(out.data(), 1, (size_t)sz, fp) : 0;
				std::fclose(fp);
				return read == (size_t)sz;
			}

#if defined(AQ_PLATFORM_WIN32)
			std::wstring ToWide(const std::string& s)
			{
				if (s.empty()) return std::wstring();
				int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
				std::wstring w(n, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
				return w;
			}

			// プロファイルのシェーダモデルは Tools/ShaderCompile/dxc_args.txt の
			// AQ_DXC_SHADER_MODEL と必ず一致させること (ビルド時生成と同じ SPIR-V にするため)。
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
#endif // AQ_PLATFORM_WIN32

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

			// ビルド時に生成した .spv を最優先で使う (Mac はこの経路しか無い)。
			if (!LoadSpirvBinary(resolved.c_str(), entryFuncName, shaderType))
			{
#if defined(AQ_PLATFORM_WIN32)
				// Windows は従来どおり DXC の実行時コンパイルへフォールバックする。
				if (!CompileToSpirv(resolved.c_str(), entryFuncName, shaderType)) return false;
#else
				// 実行時 DXC が無いプラットフォームでは .spv 不在は致命的。
				// 原因が分かるようにパスと生成方法をログへ出す。
				char msg[512];
				std::snprintf(msg, sizeof(msg),
					"[VulkanShader] .spv がありません: %s"
					" (このプラットフォームは実行時 DXC 非対応。"
					"cmake -P Tools/ShaderCompile/compile_spv.cmake でビルド時に生成してください)",
					BuildSpirvPath(resolved.c_str(), entryFuncName, shaderType).c_str());
				aq::StartupLog(msg);
				EngineAssertMsg(false, "Vulkan シェーダの .spv が見つかりません");
				return false;
#endif
			}

			VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			ci.codeSize = spirv_.size() * sizeof(uint32_t);
			ci.pCode    = spirv_.data();
			if (!VK_VERIFY(vkCreateShaderModule(VulkanGraphicsDeviceImpl::GetStaticDevice(), &ci, nullptr, &module_)))
				return false;

			if (type_ == ShaderType::VS) BuildInputLayout();
			return true;
		}


		bool VulkanShader::LoadSpirvBinary(const char* resolvedPath, const char* entry, ShaderType type)
		{
			const std::string spvPath = BuildSpirvPath(resolvedPath, entry, type);
			if (spvPath.empty()) return false;

			// 未生成なら黙って失敗させる (Windows は DXC へフォールバックする正常系)。
			std::error_code ec;
			if (!std::filesystem::exists(spvPath, ec) || ec) return false;

			std::vector<char> bytes;
			if (!ReadWholeFile(spvPath.c_str(), bytes)
			    || bytes.size() < sizeof(uint32_t)
			    || (bytes.size() % sizeof(uint32_t)) != 0)
			{
				char msg[512];
				std::snprintf(msg, sizeof(msg), "[VulkanShader] .spv の読み込みに失敗: %s", spvPath.c_str());
				aq::StartupLog(msg);
				return false;
			}

			spirv_.resize(bytes.size() / sizeof(uint32_t));
			std::memcpy(spirv_.data(), bytes.data(), bytes.size());

			// 壊れた/別物のファイルを掴んだまま vkCreateShaderModule へ流さない。
			if (spirv_[0] != SPIRV_MAGIC)
			{
				char msg[512];
				std::snprintf(msg, sizeof(msg), "[VulkanShader] .spv のマジックが不正: %s", spvPath.c_str());
				aq::StartupLog(msg);
				spirv_.clear();
				return false;
			}
			return true;
		}


#if defined(AQ_PLATFORM_WIN32)
		bool VulkanShader::CompileToSpirv(const char* resolvedPath, const char* entry, ShaderType type)
		{
			// ファイル読み込み
			std::vector<char> source;
			if (!ReadWholeFile(resolvedPath, source))
			{
				EngineAssertMsg(false, "Vulkan シェーダファイルを開けません");
				return false;
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

			// 固定引数は Tools/ShaderCompile/dxc_args.txt を単一ソースにする。
			// 同じファイルを compile_spv.cmake (ビルド時 .spv 生成) も読むので、
			// 実行時コンパイルとビルド時コンパイルで引数が食い違わない (Mac移植設計.md §4)。
			// 実行時のファイル依存を増やさないよう、読み込みではなく #include で埋め込む。
#define AQ_DXC_WIDEN_(x)    L ## x
#define AQ_DXC_ARG(x)       AQ_DXC_WIDEN_(x),
#if defined(_DEBUG)
#define AQ_DXC_ARG_DEBUG(x) AQ_DXC_WIDEN_(x),
#else
#define AQ_DXC_ARG_DEBUG(x)
#endif
			std::vector<LPCWSTR> args = {
#include "../../../Tools/ShaderCompile/dxc_args.txt"
				L"-E", entryW.c_str(),
				L"-T", ProfileFor(type),
				L"-I", includeDir.c_str(),
			};
#undef AQ_DXC_ARG_DEBUG
#undef AQ_DXC_ARG
#undef AQ_DXC_WIDEN_

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
					aq::debug::OutputString(errors->GetStringPointer());
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
#endif // AQ_PLATFORM_WIN32


		namespace
		{
			/**
			 * per-instance 入力か。
			 *
			 * DXC は入力変数を **`in.var.<セマンティクス>`**(ドット区切り)と名付ける。
			 * `spirv-dis` は表示のときにドットをアンダースコアへ直して
			 * `%in_var_POSITION` と見せるので、逆アセンブル出力を見て
			 * `in_var_` を期待すると一致しない(実際に一度そこで嵌まった)。
			 * 念のため両方の綴りを受ける。
			 *
			 * 前置きを剥がしたセマンティクスが `I_` で始まれば per-instance
			 * (D3D12Shader.cpp の perInstance 判定と同じ規約)。
			 */
			bool IsPerInstanceInput(const char* name)
			{
				if (name == nullptr) { return false; }

				static constexpr char PREFIX_DOT[]   = "in.var.";
				static constexpr char PREFIX_UNDER[] = "in_var_";

				const char* semantic = nullptr;
				if (std::strncmp(name, PREFIX_DOT, sizeof(PREFIX_DOT) - 1) == 0)
				{
					semantic = name + (sizeof(PREFIX_DOT) - 1);
				}
				else if (std::strncmp(name, PREFIX_UNDER, sizeof(PREFIX_UNDER) - 1) == 0)
				{
					semantic = name + (sizeof(PREFIX_UNDER) - 1);
				}
				else
				{
					semantic = name;   // 前置きが無い綴りにも一応対応する
				}

				return semantic[0] == 'I' && semantic[1] == '_';
			}
		}


		void VulkanShader::BuildInputLayout()
		{
			attributes_.clear();
			vertexStride_   = 0;
			instanceStride_ = 0;

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
			//
			// binding は 2 本に分ける。**セマンティクスが `I_` で始まる入力は per-instance
			// ストリーム(binding 1)** という規約は D3D12 と共通(D3D12Shader.cpp の
			// perInstance 判定と対になっている)。DXC は HLSL のセマンティクスを
			// SPIR-V の変数名 `in_var_<セマンティクス>` として残すので、そこから判定する
			// (`-fspv-reflect` を付けていないため UserSemantic 装飾は無い)。
			//
			// これを分けないと per-instance のワールド行列が binding 0 の頂点データとして
			// 読まれ、インスタンス描画(路面リボン / 草 / コインリング)が姿勢を失って消える。
			uint32_t vertexOffset   = 0;
			uint32_t instanceOffset = 0;
			for (auto* v : userInputs)
			{
				const bool perInstance = IsPerInstanceInput(v->name);

				VkVertexInputAttributeDescription a{};
				a.location = v->location;
				a.binding  = perInstance ? 1u : 0u;
				a.format   = (VkFormat)v->format;
				a.offset   = perInstance ? instanceOffset : vertexOffset;
				attributes_.push_back(a);

				const uint32_t size = FormatByteSize((VkFormat)v->format);
				if (perInstance) { instanceOffset += size; }
				else             { vertexOffset   += size; }
			}
			vertexStride_   = vertexOffset;
			instanceStride_ = instanceOffset;

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
