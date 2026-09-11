#include "aq.h"
// Metal のシェーダ。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalShader.h"
#include <spirv_reflect/spirv_reflect.h>
#include <cstdio>
#include <filesystem>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalShader.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** spirv-cross が付けるエントリ関数名(後述の BuildMslPath のコメント参照) */
			static constexpr char MSL_ENTRY_NAME[] = "main0";

			/** SPIR-V バイナリ先頭のマジックナンバー(リトルエンディアンで読んだ値) */
			static constexpr uint32_t SPIRV_MAGIC = 0x07230203u;

			/** この時間を超えた 1 本だけログに出す。全 59 本を出すと読めないため */
			static constexpr double SLOW_COMPILE_MS = 50.0;

			/** StartupLog 1 行の上限(aq::StartupMark 側のバッファに合わせて短めに切る) */
			static constexpr size_t LOG_LINE_MAX = 320;


			// ── パス解決 (VulkanShader.cpp / D3D12Shader.cpp と同じ規則) ──
			std::string FindProjectRoot()
			{
				static std::string cached;
				if (!cached.empty()) { return cached; }

				std::error_code ec;
				std::filesystem::path dir = std::filesystem::current_path(ec);
				if (ec) { return std::string(); }

				while (!dir.empty()) {
					if (std::filesystem::exists(dir / "Game" / "Assets", ec) && !ec) {
						cached = dir.generic_string();
						return cached;
					}
					if (dir == dir.root_path()) { break; }
					dir = dir.parent_path();
				}

				cached = std::filesystem::current_path(ec).generic_string();
				return cached;
			}


			std::string ResolveShaderPath(const char* filePath)
			{
				std::string path = filePath ? filePath : "";
				std::replace(path.begin(), path.end(), '\\', '/');
				if (std::filesystem::path(path).is_absolute()) { return path; }

				const std::filesystem::path root(FindProjectRoot());
				std::filesystem::path candidate;
				if (path.rfind("Assets/", 0) == 0)           { candidate = root / "Game" / path; }
				else if (path.rfind("Game/Assets/", 0) == 0) { candidate = root / path; }
				else                                         { candidate = root / path; }

				std::error_code ec;
				if (std::filesystem::exists(candidate, ec)) { return candidate.generic_string(); }
				return path;
			}


			/** ステージ名。.metal / .spv のファイル名に入る */
			const char* StageSuffix(const IShader::ShaderType type)
			{
				switch (type)
				{
				case IShader::ShaderType::VS: return "vs";
				case IShader::ShaderType::PS: return "ps";
				case IShader::ShaderType::CS: return "cs";
				}
				return "vs";
			}


			/**
			 * ビルド時に生成した MSL の探索パス: <shaderDir>/msl/<stem>.<entry>.<stage>.metal
			 *
			 * Tools/ShaderCompile/compile_msl.cmake の出力名と**一対一で対応**させること
			 * (どちらか一方だけを変えない。compile_spv.cmake と VulkanShader.cpp の
			 * BuildSpirvPath() が同じ約束をしているのと同じ)。
			 *
			 * 出力先は Vulkan の spv/ とは**別ディレクトリ**にしてある。
			 * register -> binding のシフトが違う(設計書 §5.1)ので、同名でも中身が別物になる。
			 *
			 * ここで渡す <entry> は HLSL 側のエントリ名(VSMain など)で、
			 * ファイル名の一部にしか使わない。MSL 側の関数名は spirv-cross が
			 * "main0" に固定するため(dxc へ -fspv-entrypoint-name=main を渡し、
			 * spirv-cross が予約語衝突を避けて改名する)、照合には使わない。
			 */
			std::string BuildMslPath(const char* resolvedPath, const char* entry, const IShader::ShaderType type)
			{
				const std::filesystem::path src(resolvedPath ? resolvedPath : "");
				if (src.empty()) { return std::string(); }

				const std::string name = src.stem().string() + "."
				                       + ((entry && entry[0]) ? entry : "main") + "."
				                       + StageSuffix(type) + ".metal";
				return (src.parent_path() / "msl" / name).generic_string();
			}


			/**
			 * 頂点入力のリフレクション用 SPIR-V の探索パス: <shaderDir>/msl/<stem>.<entry>.<stage>.spv
			 *
			 * compile_msl.cmake は dxc の中間生成物である .spv を **.metal と同じディレクトリに
			 * 同名で残している**ので、BuildMslPath() と拡張子だけが違う。
			 * Vulkan 用の spv/ とは register -> binding のシフトが違う別物なので混ぜないこと
			 * (ただし頂点入力の location はシフトの影響を受けないため、どちらで読んでも同じ)。
			 */
			std::string BuildSpirvPath(const char* resolvedPath, const char* entry, const IShader::ShaderType type)
			{
				const std::filesystem::path src(resolvedPath ? resolvedPath : "");
				if (src.empty()) { return std::string(); }

				const std::string name = src.stem().string() + "."
				                       + ((entry && entry[0]) ? entry : "main") + "."
				                       + StageSuffix(type) + ".spv";
				return (src.parent_path() / "msl" / name).generic_string();
			}


			/** ファイル全体をテキストとして読む */
			bool ReadWholeFile(const char* path, std::string& out)
			{
				out.clear();

				std::FILE* fp = std::fopen(path, "rb");
				if (!fp) { return false; }

				std::fseek(fp, 0, SEEK_END);
				const long size = std::ftell(fp);
				std::fseek(fp, 0, SEEK_SET);
				if (size < 0) {
					std::fclose(fp);
					return false;
				}

				out.resize(static_cast<size_t>(size));
				const size_t read = (size > 0) ? std::fread(&out[0], 1, static_cast<size_t>(size), fp) : 0;
				std::fclose(fp);
				return read == static_cast<size_t>(size);
			}


			/**
			 * ファイル全体を 32bit ワード列として読む(SPIR-V 用)。
			 *
			 * spirv_reflect は 4 バイト境界に載った語列を期待するので、std::string で受けずに
			 * vector<uint32_t> へ入れ直す。サイズが 4 の倍数でなければ SPIR-V ではない。
			 */
			bool ReadWholeFileWords(const char* path, std::vector<uint32_t>& out)
			{
				out.clear();

				std::string bytes;
				if (!ReadWholeFile(path, bytes))                         { return false; }
				if (bytes.size() < sizeof(uint32_t))                     { return false; }
				if ((bytes.size() % sizeof(uint32_t)) != 0)              { return false; }

				out.resize(bytes.size() / sizeof(uint32_t));
				std::memcpy(out.data(), bytes.data(), bytes.size());
				return true;
			}


			/**
			 * 複数行のメッセージを 1 行ずつログへ流す。
			 *
			 * Metal のコンパイルエラーは「program_source:12:5: error: ...」が何行も並ぶ。
			 * aq::StartupMark は 1 行 512 文字で切るので、まとめて渡すと先頭しか残らず
			 * 肝心のエラー本文が消える(P2 以降のデバッグで効いてくる)。
			 */
			void LogMultiLine(const char* prefix, const std::string& text)
			{
				size_t begin = 0;
				while (begin <= text.size()) {
					size_t end = text.find('\n', begin);
					if (end == std::string::npos) { end = text.size(); }

					const size_t length = (end - begin < LOG_LINE_MAX) ? (end - begin) : LOG_LINE_MAX;
					if (length > 0) {
						char line[512];
						std::snprintf(line, sizeof(line), "%s%.*s", prefix, static_cast<int>(length), text.c_str() + begin);
						aq::StartupLog(line);
					}

					if (end == text.size()) { break; }
					begin = end + 1;
				}
			}


			/**
			 * Load の所要時間の集計(設計書 §12 P0.5「起動時間の増分を実測して記録」)。
			 *
			 * 59 本を 1 本 1 行で出すと読めないので、しきい値超えだけその場で出し、
			 * 合計はここへ貯めて MetalShader::LogLoadSummary() で 1 行にまとめる。
			 *
			 * リソースの仕上げは複数スレッドから来うるので mutex で守る。
			 */
			struct MslLoadProfile
			{
				std::mutex mutex;
				uint32_t   count     = 0;   // Load を試みた本数
				uint32_t   failed    = 0;   // うち失敗した本数
				uint32_t   slowCount = 0;   // うち SLOW_COMPILE_MS を超えた本数
				double     totalMs   = 0.0;

				MslLoadProfile()
				{
					// 計測の起点を startup_timing.log に残す。合計行との差が「MSL コンパイルの増分」。
					// 同時に aq::StartupMark 側の関数内 static を先に構築させ、
					// 本オブジェクト(プロセス終了時に破棄)より後に破棄されるようにしている。
					aq::StartupLog("[MetalShader] MSL の実行時コンパイル 計測開始");
				}

				~MslLoadProfile()
				{
					// 誰も LogLoadSummary() を呼ばなかった場合の保険。
					Flush();
				}

				void Add(const double ms, const bool ok)
				{
					std::lock_guard<std::mutex> lock(mutex);
					++count;
					totalMs += ms;
					if (!ok)                    { ++failed; }
					if (ms > SLOW_COMPILE_MS)   { ++slowCount; }
				}

				void Flush()
				{
					std::lock_guard<std::mutex> lock(mutex);
					if (count == 0) { return; }

					EnginePrintf("[LoadProf] msl compile: %u shaders (%u failed, %u slow), %.2f ms\n",
						count, failed, slowCount, totalMs);
					aq::StartupMarkf("[MetalShader] MSL コンパイル合計 %u 本 (失敗 %u / %.1fms 超 %u), %.1f ms",
						count, failed, SLOW_COMPILE_MS, slowCount, totalMs);

					count     = 0;
					failed    = 0;
					slowCount = 0;
					totalMs   = 0.0;
				}
			};


			/** 集計の実体。初回の Load で構築する(静的破棄順を StartupMark より後にするため) */
			MslLoadProfile& LoadProfile()
			{
				static MslLoadProfile profile;
				return profile;
			}
		}


		/**
		 * Metal シェーダ
		 */
		MetalShader::MetalShader(id<MTLDevice> device)
			: device_([device retain])
			, library_(nil)
			, function_(nil)
			, vertexDescriptor_(nil)
			, vertexStride_(0)
			, instanceStride_(0)
			, filePath_()
			, entryFuncName_()
			, type_(ShaderType::VS)
		{
		}


		MetalShader::~MetalShader()
		{
			Release();
			[device_ release];
			device_ = nil;
		}


		bool MetalShader::Load(const char* filePath, const char* entryFuncName, const ShaderType shaderType)
		{
			Release();

			filePath_      = filePath      ? filePath      : "";
			entryFuncName_ = entryFuncName ? entryFuncName : "";
			type_          = shaderType;

			const auto profStart = std::chrono::steady_clock::now();
			const bool ok        = LoadMsl();
			const double ms      = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - profStart).count();

			// 重い 1 本だけ名前を出す(犯人特定用)。全部出すと 59 行になる。
			if (ms > SLOW_COMPILE_MS) {
				char msg[512];
				std::snprintf(msg, sizeof(msg), "[MetalShader] slow compile %7.2f ms  %s (%s)",
					ms, filePath_.c_str(), entryFuncName_.c_str());
				aq::StartupLog(msg);
			}
			LoadProfile().Add(ms, ok);

			return ok;
		}


		bool MetalShader::LoadMsl()
		{
			// ── 1. ビルド時に生成した .metal を読む ──
			const std::string resolved = ResolveShaderPath(filePath_.c_str());
			const std::string mslPath  = BuildMslPath(resolved.c_str(), entryFuncName_.c_str(), type_);

			std::string source;
			if (mslPath.empty() || !ReadWholeFile(mslPath.c_str(), source) || source.empty()) {
				// Metal には実行時 HLSL コンパイル経路が無いので .metal 不在は致命的。
				// 原因が分かるようにパスと生成方法をログへ出す(VulkanShader の .spv 欠落時と同じ作法)。
				char msg[512];
				std::snprintf(msg, sizeof(msg),
					"[MetalShader] .metal がありません: %s"
					" (ビルド時生成物です。ビルドターゲット aqCompileMsl を実行するか、"
					"cmake -P DirectX/Tools/ShaderCompile/compile_msl.cmake で生成してください)",
					mslPath.empty() ? filePath_.c_str() : mslPath.c_str());
				aq::StartupLog(msg);
				EngineAssertMsg(false, "Metal シェーダの .metal が見つかりません");
				return false;
			}

			// ── 2. MSL を実行時コンパイルする(設計書 §9.2)──
			// xcrun metal(Metal Toolchain)が入っていないため .metallib の事前ビルドは採れない。
			// 将来 Toolchain を入れたらこの関数の中だけを差し替えられるようにしてある。
			@autoreleasepool {
				NSString* sourceString = [[NSString alloc] initWithBytes:source.data()
				                                                  length:source.size()
				                                                encoding:NSUTF8StringEncoding];
				if (sourceString == nil) {
					char msg[512];
					std::snprintf(msg, sizeof(msg), "[MetalShader] .metal を UTF-8 として読めません: %s", mslPath.c_str());
					aq::StartupLog(msg);
					return false;
				}

				// MTLCompileOptions は既定のまま(言語バージョンは spirv-cross の
				// --msl-version 20000 に合わせて Metal 側が推論する)。
				MTLCompileOptions* options = [[MTLCompileOptions alloc] init];

				NSError* error = nil;
				library_ = [device_ newLibraryWithSource:sourceString options:options error:&error];

				[options release];
				[sourceString release];

				if (library_ == nil) {
					// **ここを出さないと P2 以降のデバッグが成立しない**。
					// どのファイルで落ちたかと、Metal が返した本文を必ず残す。
					char msg[512];
					std::snprintf(msg, sizeof(msg), "[MetalShader] MSL のコンパイルに失敗: %s", mslPath.c_str());
					aq::StartupLog(msg);
					if (error != nil) {
						const char* detail = [[error localizedDescription] UTF8String];
						LogMultiLine("[MetalShader]   ", detail ? detail : "(詳細なし)");
					}
					EngineAssertMsg(false, "Metal シェーダのコンパイルに失敗しました");
					return false;
				}

				// 成功しても警告が載ってくることがあるので拾っておく。
				if (error != nil) {
					const char* detail = [[error localizedDescription] UTF8String];
					if (detail && detail[0]) {
						char msg[512];
						std::snprintf(msg, sizeof(msg), "[MetalShader] MSL の警告: %s", mslPath.c_str());
						aq::StartupLog(msg);
						LogMultiLine("[MetalShader]   ", detail);
					}
				}

				// ── 3. 関数を取り出す ──
				// **"main0" は spirv-cross が付ける固定名**。dxc へ -fspv-entrypoint-name=main を
				// 渡して SPIR-V のエントリ名を "main" にしているが、MSL では main が予約語なので
				// spirv-cross が "main0" へ改名する。よって HLSL 側のエントリ名(VSMain 等)は
				// ファイル名の識別にしか使わない。
				function_ = [library_ newFunctionWithName:[NSString stringWithUTF8String:MSL_ENTRY_NAME]];
				if (function_ == nil) {
					char msg[512];
					std::snprintf(msg, sizeof(msg),
						"[MetalShader] MSL に関数 \"%s\" がありません: %s"
						" (spirv-cross の出力形式が変わった可能性があります)",
						MSL_ENTRY_NAME, mslPath.c_str());
					aq::StartupLog(msg);
					EngineAssertMsg(false, "Metal シェーダのエントリ関数が見つかりません");
					Release();
					return false;
				}
			}

			// ── 4. 頂点入力レイアウト(設計書 §9.3)──
			// 隣の .spv を spirv_reflect で読んで MTLVertexDescriptor を組む。
			// 失敗しても致命にしない(下のコメント参照)ので戻り値は見ない。
			BuildVertexDescriptor();
			return true;
		}


		namespace
		{
			/**
			 * per-instance 入力か。
			 *
			 * DXC は入力変数を **`in.var.<セマンティクス>`**(ドット区切り)と名付ける。
			 * `spirv-dis` は表示のときにドットをアンダースコアへ直して `%in_var_POSITION` と
			 * 見せるので、逆アセンブル出力を見て `in_var_` を期待すると一致しない
			 * (Vulkan 側で一度そこで嵌まっている)。念のため両方の綴りを受ける。
			 *
			 * 前置きを剥がしたセマンティクスが `I_` で始まれば per-instance
			 * (D3D12Shader.cpp / VulkanShader.cpp の perInstance 判定と同じ規約)。
			 */
			bool IsPerInstanceInput(const char* name)
			{
				if (name == nullptr) { return false; }

				static constexpr char PREFIX_DOT[]   = "in.var.";
				static constexpr char PREFIX_UNDER[] = "in_var_";

				const char* semantic = nullptr;
				if (std::strncmp(name, PREFIX_DOT, sizeof(PREFIX_DOT) - 1) == 0) {
					semantic = name + (sizeof(PREFIX_DOT) - 1);
				} else if (std::strncmp(name, PREFIX_UNDER, sizeof(PREFIX_UNDER) - 1) == 0) {
					semantic = name + (sizeof(PREFIX_UNDER) - 1);
				} else {
					semantic = name;   // 前置きが無い綴りにも一応対応する
				}

				return semantic[0] == 'I' && semantic[1] == '_';
			}


			/**
			 * SpvReflectFormat -> MTLVertexFormat。
			 *
			 * SpvReflectFormat の値は VkFormat と同じ数値なので、VulkanShader.cpp が
			 * そのまま VkFormat へキャストしているところを Metal では写像し直す。
			 * 16bit 系は現状のシェーダには出てこないが、出たときに黙って
			 * stride が狂わないよう(FormatByteSize が 0 を返さないよう)拾ってある。
			 */
			MTLVertexFormat ToMTLVertexFormat(const SpvReflectFormat format)
			{
				switch (format)
				{
				case SPV_REFLECT_FORMAT_R32_SFLOAT:          return MTLVertexFormatFloat;
				case SPV_REFLECT_FORMAT_R32G32_SFLOAT:       return MTLVertexFormatFloat2;
				case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:    return MTLVertexFormatFloat3;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT: return MTLVertexFormatFloat4;

				case SPV_REFLECT_FORMAT_R32_UINT:            return MTLVertexFormatUInt;
				case SPV_REFLECT_FORMAT_R32G32_UINT:         return MTLVertexFormatUInt2;
				case SPV_REFLECT_FORMAT_R32G32B32_UINT:      return MTLVertexFormatUInt3;
				case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:   return MTLVertexFormatUInt4;

				case SPV_REFLECT_FORMAT_R32_SINT:            return MTLVertexFormatInt;
				case SPV_REFLECT_FORMAT_R32G32_SINT:         return MTLVertexFormatInt2;
				case SPV_REFLECT_FORMAT_R32G32B32_SINT:      return MTLVertexFormatInt3;
				case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:   return MTLVertexFormatInt4;

				case SPV_REFLECT_FORMAT_R16_SFLOAT:          return MTLVertexFormatHalf;
				case SPV_REFLECT_FORMAT_R16G16_SFLOAT:       return MTLVertexFormatHalf2;
				case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:    return MTLVertexFormatHalf3;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT: return MTLVertexFormatHalf4;

				case SPV_REFLECT_FORMAT_R16_UINT:            return MTLVertexFormatUShort;
				case SPV_REFLECT_FORMAT_R16G16_UINT:         return MTLVertexFormatUShort2;
				case SPV_REFLECT_FORMAT_R16G16B16_UINT:      return MTLVertexFormatUShort3;
				case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:   return MTLVertexFormatUShort4;

				case SPV_REFLECT_FORMAT_R16_SINT:            return MTLVertexFormatShort;
				case SPV_REFLECT_FORMAT_R16G16_SINT:         return MTLVertexFormatShort2;
				case SPV_REFLECT_FORMAT_R16G16B16_SINT:      return MTLVertexFormatShort3;
				case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:   return MTLVertexFormatShort4;

				default:                                     return MTLVertexFormatInvalid;
				}
			}


			/** 頂点入力で使う範囲のフォーマットのバイトサイズ(未知は 0) */
			uint32_t FormatByteSize(const SpvReflectFormat format)
			{
				switch (format)
				{
				case SPV_REFLECT_FORMAT_R16_SFLOAT:
				case SPV_REFLECT_FORMAT_R16_UINT:
				case SPV_REFLECT_FORMAT_R16_SINT:            return 2;

				case SPV_REFLECT_FORMAT_R16G16_SFLOAT:
				case SPV_REFLECT_FORMAT_R16G16_UINT:
				case SPV_REFLECT_FORMAT_R16G16_SINT:
				case SPV_REFLECT_FORMAT_R32_SFLOAT:
				case SPV_REFLECT_FORMAT_R32_UINT:
				case SPV_REFLECT_FORMAT_R32_SINT:            return 4;

				case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT:
				case SPV_REFLECT_FORMAT_R16G16B16_UINT:
				case SPV_REFLECT_FORMAT_R16G16B16_SINT:      return 6;

				case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT:
				case SPV_REFLECT_FORMAT_R16G16B16A16_UINT:
				case SPV_REFLECT_FORMAT_R16G16B16A16_SINT:
				case SPV_REFLECT_FORMAT_R32G32_SFLOAT:
				case SPV_REFLECT_FORMAT_R32G32_UINT:
				case SPV_REFLECT_FORMAT_R32G32_SINT:         return 8;

				case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:
				case SPV_REFLECT_FORMAT_R32G32B32_UINT:
				case SPV_REFLECT_FORMAT_R32G32B32_SINT:      return 12;

				case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:
				case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:
				case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:   return 16;

				default:                                     return 0;
				}
			}
		}


		void MetalShader::BuildVertexDescriptor()
		{
			// VS 以外は頂点入力を持たない。
			if (type_ != ShaderType::VS) { return; }

			// ── 1. 隣の .spv を読む ──
			// **読めなくても致命エラーにはしない**。頂点入力を持たないフルスクリーンパスの VS も
			// あり、その場合 MTLVertexDescriptor は nil のままが正しい姿だから
			// (Vulkan 側も BuildInputLayout の失敗は黙って属性 0 本として扱っている)。
			// ただし原因が追えるようログだけは残す。
			const std::string resolved = ResolveShaderPath(filePath_.c_str());
			const std::string spvPath  = BuildSpirvPath(resolved.c_str(), entryFuncName_.c_str(), type_);

			std::vector<uint32_t> spirv;
			if (spvPath.empty() || !ReadWholeFileWords(spvPath.c_str(), spirv) || spirv[0] != SPIRV_MAGIC) {
				char msg[512];
				std::snprintf(msg, sizeof(msg),
					"[MetalShader] 頂点入力の .spv を読めません(頂点レイアウト無しで続行): %s",
					spvPath.empty() ? filePath_.c_str() : spvPath.c_str());
				aq::StartupLog(msg);
				return;
			}

			// ── 2. 入力変数を列挙する(VulkanShader::BuildInputLayout の移植)──
			SpvReflectShaderModule reflectModule{};
			if (spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &reflectModule)
			    != SPV_REFLECT_RESULT_SUCCESS) {
				char msg[512];
				std::snprintf(msg, sizeof(msg),
					"[MetalShader] spirv_reflect が .spv を解釈できません(頂点レイアウト無しで続行): %s",
					spvPath.c_str());
				aq::StartupLog(msg);
				return;
			}

			uint32_t count = 0;
			spvReflectEnumerateInputVariables(&reflectModule, &count, nullptr);
			std::vector<SpvReflectInterfaceVariable*> inputs(count);
			spvReflectEnumerateInputVariables(&reflectModule, &count, inputs.data());

			// 組み込み変数(SV_*)を除外し location 昇順に並べる。
			std::vector<SpvReflectInterfaceVariable*> userInputs;
			for (SpvReflectInterfaceVariable* variable : inputs) {
				if (variable && variable->built_in == static_cast<SpvBuiltIn>(-1) && variable->location != 0xFFFFFFFF) {
					userInputs.push_back(variable);
				}
			}
			std::sort(userInputs.begin(), userInputs.end(),
				[](const SpvReflectInterfaceVariable* a, const SpvReflectInterfaceVariable* b)
				{
					return a->location < b->location;
				});

			if (userInputs.empty()) {
				// 頂点入力を持たない VS(フルスクリーンパス)。記述子は nil のままが正しい。
				spvReflectDestroyShaderModule(&reflectModule);
				return;
			}

			// ── 3. MTLVertexDescriptor を組む ──
			// パック済みレイアウト(CPU の VertexData / SkinnedVertexData と一致)を仮定し、
			// location 順にオフセットを積む(D3D12 の APPEND_ALIGNED と同じ思想)。
			//
			// **セマンティクスが `I_` で始まる入力は per-instance ストリーム**という規約は
			// D3D12 / Vulkan と共通。これを分けないと per-instance のワールド行列が
			// per-vertex データとして読まれ、インスタンス描画(路面リボン / 草 / コインリング)が
			// 姿勢を失って消える。
			//
			// **attributes[location] へそのまま入れてよい根拠**: spirv-cross が吐いた MSL の
			// `[[attribute(n)]]` の n は SPIR-V の Location 装飾をそのまま写したものであることを
			// 実機で確認済み(設計書 §0.2 / §9.3)。よって SPIR-V の location と
			// MTLVertexDescriptor の属性添字は一対一で対応する。
			@autoreleasepool {
				MTLVertexDescriptor* descriptor = [[MTLVertexDescriptor alloc] init];

				uint32_t vertexOffset   = 0;
				uint32_t instanceOffset = 0;
				for (const SpvReflectInterfaceVariable* variable : userInputs) {
					const SpvReflectFormat format      = static_cast<SpvReflectFormat>(variable->format);
					const MTLVertexFormat  metalFormat = ToMTLVertexFormat(format);
					const uint32_t         size        = FormatByteSize(format);
					if (metalFormat == MTLVertexFormatInvalid || size == 0) {
						char msg[512];
						std::snprintf(msg, sizeof(msg),
							"[MetalShader] 未対応の頂点入力フォーマット(location=%u, format=%d): %s",
							variable->location, static_cast<int>(format), spvPath.c_str());
						aq::StartupLog(msg);
						continue;
					}

					const bool     perInstance = IsPerInstanceInput(variable->name);
					const uint32_t bufferIndex = perInstance ? metal::INSTANCE_BUFFER_INDEX : metal::VERTEX_BUFFER_INDEX;

					descriptor.attributes[variable->location].format      = metalFormat;
					descriptor.attributes[variable->location].offset      = perInstance ? instanceOffset : vertexOffset;
					descriptor.attributes[variable->location].bufferIndex = bufferIndex;

					if (perInstance) { instanceOffset += size; }
					else             { vertexOffset   += size; }
				}

				vertexStride_   = vertexOffset;
				instanceStride_ = instanceOffset;

				// レイアウト(ストリーム)側。cbuffer とぶつからないよう buffer 空間の上端を使う(設計書 §5.2)。
				// インスタンス属性が無いときは layouts[29] に触らない。stride 0 のレイアウトを
				// 残すと「バッファが束ねられていない」として Validation に叱られるため。
				if (vertexStride_ > 0) {
					descriptor.layouts[metal::VERTEX_BUFFER_INDEX].stride       = vertexStride_;
					descriptor.layouts[metal::VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
					descriptor.layouts[metal::VERTEX_BUFFER_INDEX].stepRate     = 1;
				}
				if (instanceStride_ > 0) {
					descriptor.layouts[metal::INSTANCE_BUFFER_INDEX].stride       = instanceStride_;
					descriptor.layouts[metal::INSTANCE_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerInstance;
					descriptor.layouts[metal::INSTANCE_BUFFER_INDEX].stepRate     = 1;
				}

				// MRR: alloc/init で +1 したものをそのまま保持し、Release() で対に release する。
				vertexDescriptor_ = descriptor;
			}

			spvReflectDestroyShaderModule(&reflectModule);
		}


		void MetalShader::Release()
		{
			[function_ release];
			function_ = nil;
			[library_ release];
			library_  = nil;

			// PSO が参照し終えた後に呼ばれる想定。MetalPipelineCache 側は記述子を copy して
			// 使うので、ここで解放しても生成済みの PSO には影響しない。
			[vertexDescriptor_ release];
			vertexDescriptor_ = nil;
			vertexStride_     = 0;
			instanceStride_   = 0;
		}


		void MetalShader::LogLoadSummary()
		{
			LoadProfile().Flush();
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
