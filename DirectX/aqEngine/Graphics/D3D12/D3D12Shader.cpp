#include "aq.h"
#ifdef ENGINE_GRAPHICS_D3D12
#include "D3D12Common.h"
#include "D3D12Shader.h"
#include <filesystem>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>


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


			// ── バイトコードキャッシュ ──────────────────────────────────────────────
			// 実行時 D3DCompile は起動時に 19 本で Release 約 0.5 秒 / Debug はさらに重い(実測)。
			// コンパイル結果を <ProjectRoot>/x64/ShaderCache/<key>.bin に保存し、次回以降は
			// ソース(本体 + #include した全ファイル)のハッシュが一致すればそれを読む。
			//   key  = FNV-1a64(解決済みパス, エントリ, プロファイル, コンパイルフラグ)
			//   検証 = 本体ハッシュ + 依存ファイル(パス, ハッシュ)の全一致。1 つでも変わればミス扱いで再コンパイル。
			// UWP はコンテンツルートが読み取り専用のためキャッシュを使わない。

			uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed = 14695981039346656037ull)
			{
				const auto* p = static_cast<const uint8_t*>(data);
				uint64_t h = seed;
				for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
				return h;
			}

			uint64_t Fnv1a64(const std::string& s, uint64_t seed) { return Fnv1a64(s.data(), s.size(), seed); }

			bool ReadWholeFile(const std::string& path, std::vector<char>& out)
			{
				FILE* fp = nullptr;
				if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) return false;
				fseek(fp, 0, SEEK_END);
				const long size = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				out.clear();
				if (size > 0)
				{
					out.resize(static_cast<size_t>(size));
					const size_t read = fread(out.data(), 1, static_cast<size_t>(size), fp);
					out.resize(read);
				}
				fclose(fp);
				return true;
			}

			// キャッシュディレクトリ(作成込み)。使えない環境では空文字を返す。
			const std::string& ShaderCacheDir()
			{
				static const std::string cached = []() -> std::string
				{
#if defined(AQ_PLATFORM_UWP)
					return std::string();
#else
					if (aq::Engine::Get().GetContentRoot()) return std::string();   // パッケージ配置 = 読み取り専用
					std::error_code ec;
					const std::filesystem::path dir = std::filesystem::path(FindProjectRoot()) / "x64" / "ShaderCache";
					std::filesystem::create_directories(dir, ec);
					if (ec) return std::string();
					return dir.generic_string() + "/";
#endif
				}();
				return cached;
			}

			struct ShaderDep
			{
				std::string path;
				uint64_t    hash = 0;
			};

			constexpr uint32_t SHADER_CACHE_MAGIC   = 0x43535141u;   // 'AQSC'
			constexpr uint32_t SHADER_CACHE_VERSION = 1u;

			// キャッシュ読み込み。形式不一致/ソース変更/依存変更なら false。
			bool TryLoadShaderCache(const std::string& cachePath, uint64_t sourceHash, ID3DBlob** outBlob)
			{
				std::vector<char> file;
				if (!ReadWholeFile(cachePath, file) || file.size() < 24) return false;

				size_t pos = 0;
				auto readU32 = [&](uint32_t& v) { if (pos + 4 > file.size()) return false; std::memcpy(&v, file.data() + pos, 4); pos += 4; return true; };
				auto readU64 = [&](uint64_t& v) { if (pos + 8 > file.size()) return false; std::memcpy(&v, file.data() + pos, 8); pos += 8; return true; };

				uint32_t magic = 0, version = 0, depCount = 0, blobSize = 0;
				uint64_t storedSourceHash = 0;
				if (!readU32(magic) || magic != SHADER_CACHE_MAGIC) return false;
				if (!readU32(version) || version != SHADER_CACHE_VERSION) return false;
				if (!readU64(storedSourceHash) || storedSourceHash != sourceHash) return false;
				if (!readU32(depCount) || depCount > 256) return false;

				std::vector<char> depData;
				for (uint32_t i = 0; i < depCount; ++i)
				{
					uint32_t len = 0; uint64_t hash = 0;
					if (!readU32(len) || pos + len > file.size()) return false;
					const std::string depPath(file.data() + pos, len);
					pos += len;
					if (!readU64(hash)) return false;
					if (!ReadWholeFile(depPath, depData)) return false;
					if (Fnv1a64(depData.data(), depData.size()) != hash) return false;
				}

				if (!readU32(blobSize) || blobSize == 0 || pos + blobSize > file.size()) return false;

				ID3DBlob* blob = nullptr;
				if (FAILED(D3DCreateBlob(blobSize, &blob)) || !blob) return false;
				std::memcpy(blob->GetBufferPointer(), file.data() + pos, blobSize);
				*outBlob = blob;
				return true;
			}

			// キャッシュ書き込み。一時ファイルに書いてから rename し、並列コンパイル時の書きかけ読みを防ぐ。
			void SaveShaderCache(const std::string& cachePath, uint64_t sourceHash,
			                     const std::vector<ShaderDep>& deps, ID3DBlob* blob)
			{
				std::vector<char> buf;
				auto putU32 = [&](uint32_t v) { const char* p = reinterpret_cast<const char*>(&v); buf.insert(buf.end(), p, p + 4); };
				auto putU64 = [&](uint64_t v) { const char* p = reinterpret_cast<const char*>(&v); buf.insert(buf.end(), p, p + 8); };

				putU32(SHADER_CACHE_MAGIC);
				putU32(SHADER_CACHE_VERSION);
				putU64(sourceHash);
				putU32(static_cast<uint32_t>(deps.size()));
				for (const auto& d : deps)
				{
					putU32(static_cast<uint32_t>(d.path.size()));
					buf.insert(buf.end(), d.path.begin(), d.path.end());
					putU64(d.hash);
				}
				const uint32_t blobSize = static_cast<uint32_t>(blob->GetBufferSize());
				putU32(blobSize);
				const char* bp = static_cast<const char*>(blob->GetBufferPointer());
				buf.insert(buf.end(), bp, bp + blobSize);

				std::ostringstream tmpName;
				tmpName << cachePath << ".tmp" << std::this_thread::get_id();
				const std::string tmpPath = tmpName.str();

				FILE* fp = nullptr;
				if (fopen_s(&fp, tmpPath.c_str(), "wb") != 0 || !fp) return;
				fwrite(buf.data(), 1, buf.size(), fp);
				fclose(fp);

				std::error_code ec;
				std::filesystem::rename(tmpPath, cachePath, ec);   // MSVC 実装は既存ファイルを置換する
				if (ec) std::filesystem::remove(tmpPath, ec);
			}


			// #include をシェーダのディレクトリ基準で解決する ID3DInclude 実装。
			// SetCurrentDirectory(プロセス全体の CWD 変更) を使わずに済むため、コンパイルを
			// ワーカースレッドで安全に実行できる(他スレッドのファイル I/O を壊さない)。
			// 開いたファイルの (パス, ハッシュ) を記録し、バイトコードキャッシュの依存情報にする。
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

					// 依存として記録(同じファイルの多重 include は 1 回だけ)
					const std::string key = full.generic_string();
					bool known = false;
					for (const auto& d : deps_) { if (d.path == key) { known = true; break; } }
					if (!known) deps_.push_back({ key, Fnv1a64(buffer, read) });
					return S_OK;
				}

				HRESULT __stdcall Close(LPCVOID data) override
				{
					delete[] static_cast<const char*>(data);
					return S_OK;
				}

				const std::vector<ShaderDep>& GetDependencies() const { return deps_; }

			private:
				std::string            baseDir_;
				std::vector<ShaderDep> deps_;
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
			const char* model = models[static_cast<uint32_t>(shaderType)];

			// ── バイトコードキャッシュ照合 ──
			const std::string& cacheDir = ShaderCacheDir();
			std::string cachePath;
			const uint64_t sourceHash = Fnv1a64(shaderBuffer.data(), shaderBuffer.size());
			if (!cacheDir.empty())
			{
				uint64_t key = Fnv1a64(resolved, 14695981039346656037ull);
				key = Fnv1a64(std::string(entryFuncName ? entryFuncName : ""), key);
				key = Fnv1a64(std::string(model), key);
				key = Fnv1a64(&flags, sizeof(flags), key);
				char name[32];
				snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(key));
				cachePath = cacheDir + name;

				if (TryLoadShaderCache(cachePath, sourceHash, &blob_))
				{
					if (type_ == ShaderType::VS) BuildInputLayout();
					return true;
				}
			}

			// ── 実コンパイル(キャッシュミス) ──
			const auto compileStart = std::chrono::steady_clock::now();

			// #include はシェーダのディレクトリ基準で解決する。専用ハンドラを使うことで
			// SetCurrentDirectory(プロセス全体の CWD 変更) を避け、ワーカースレッドから安全にコンパイルできる。
			ShaderIncludeHandler includeHandler(GetDirectoryPath(resolved));

			ID3DBlob* errorBlob = nullptr;
			HRESULT hr = D3DCompile(
				shaderBuffer.data(), shaderBuffer.size(), resolved.c_str(), nullptr,
				&includeHandler, entryFuncName, model,
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

			const double compileMs = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - compileStart).count();
			aq::StartupMarkf("      [shader] compiled (cache miss) %7.2f ms  %s:%s", compileMs, filePath, entryFuncName ? entryFuncName : "");

			if (!cachePath.empty())
			{
				SaveShaderCache(cachePath, sourceHash, includeHandler.GetDependencies(), blob_);
			}

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
