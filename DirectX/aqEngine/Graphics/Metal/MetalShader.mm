#include "aq.h"
// Metal のシェーダ。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalShader.h"
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

			// TODO(P2): 隣の .spv を spirv_reflect で読んで MTLVertexDescriptor を組む(設計書 §9.3)。
			//   compile_msl.cmake は .metal と同じ場所へ同名の .spv を残しているので、
			//   BuildMslPath() の拡張子を差し替えるだけで引ける。
			return true;
		}


		void MetalShader::Release()
		{
			[function_ release];
			function_ = nil;
			[library_ release];
			library_  = nil;
		}


		void MetalShader::LogLoadSummary()
		{
			LoadProfile().Flush();
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
