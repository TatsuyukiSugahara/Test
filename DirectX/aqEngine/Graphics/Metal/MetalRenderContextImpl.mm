#include "aq.h"
// Metal の RenderContext Implementor。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalRenderContextImpl.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include "Graphics/Metal/MetalRenderTarget.h"
#include "Graphics/Metal/MetalDepthMap.h"
#include "Graphics/Metal/MetalBuffers.h"
#include "Graphics/Metal/MetalResources.h"
#include "Graphics/Metal/MetalShader.h"
#include "Graphics/Metal/MetalPipelineCache.h"
#include "Graphics/Metal/MetalDepthStencilCache.h"
#include <spirv_reflect/spirv_reflect.h>
#include <cstdio>
#include <filesystem>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalRenderContextImpl.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** SPIR-V バイナリ先頭のマジックナンバー(MetalShader.mm と同じ) */
			static constexpr uint32_t SPIRV_MAGIC = 0x07230203u;

			/**
			 * dxc へ -fspv-entrypoint-name=main を渡しているので、SPIR-V 側のエントリ名は常にこれ。
			 * (MSL 側は spirv-cross が main0 へ改名するが、.spv は main のまま)
			 */
			static constexpr char SPIRV_ENTRY_NAME[] = "main";


			// ────────────────────────────────────────────────────────────
			//  CS のスレッドグループサイズを .spv から読む
			//
			//  **本来の置き場所は MetalShader**(既に .spv を spirv_reflect で読んでいる)。
			//  P4 の担当範囲が MetalShader を含まないため、いったんここへ置いている。
			//  MetalShader に GetThreadGroupSize() 相当が生えたら、この無名名前空間の
			//  4 関数と GetThreadGroupSize() / threadGroupSizes_ はまるごと消せる。
			//
			//  パス解決は MetalShader.mm の FindProjectRoot / ResolveShaderPath /
			//  BuildSpirvPath の**写し**。片方だけ変えないこと。
			// ────────────────────────────────────────────────────────────

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


			/** CS の中間生成物 .spv の探索パス: <shaderDir>/msl/<stem>.<entry>.cs.spv */
			std::string BuildComputeSpirvPath(const char* resolvedPath, const char* entry)
			{
				const std::filesystem::path src(resolvedPath ? resolvedPath : "");
				if (src.empty()) { return std::string(); }

				const std::string name = src.stem().string() + "."
				                       + ((entry && entry[0]) ? entry : "main") + ".cs.spv";
				return (src.parent_path() / "msl" / name).generic_string();
			}


			/** ファイル全体を 32bit ワード列として読む(SPIR-V 用。MetalShader.mm と同じ) */
			bool ReadWholeFileWords(const char* path, std::vector<uint32_t>& out)
			{
				out.clear();

				std::FILE* fp = std::fopen(path, "rb");
				if (!fp) { return false; }

				std::fseek(fp, 0, SEEK_END);
				const long size = std::ftell(fp);
				std::fseek(fp, 0, SEEK_SET);
				if (size <= 0 || (size % static_cast<long>(sizeof(uint32_t))) != 0) {
					std::fclose(fp);
					return false;
				}

				out.resize(static_cast<size_t>(size) / sizeof(uint32_t));
				const size_t read = std::fread(out.data(), 1, static_cast<size_t>(size), fp);
				std::fclose(fp);
				return read == static_cast<size_t>(size);
			}


			/**
			 * .spv の OpExecutionMode LocalSize を読む。
			 *
			 * MSL には [numthreads(...)] 相当の情報が**入らない**(spirv-cross が落とす)ため、
			 * dispatchThreadgroups:threadsPerThreadgroup: に渡す値はここからしか取れない。
			 * MTLComputePipelineState の threadExecutionWidth 等はハードウェアの都合の値なので
			 * 代わりにはならない。
			 *
			 * @param spirvPath 読む .spv のパス
			 * @param outSize   成功時に [numthreads(x,y,z)] が入る
			 * @return 読めたら true
			 */
			bool ReadLocalSizeFromSpirv(const std::string& spirvPath, MTLSize& outSize)
			{
				std::vector<uint32_t> spirv;
				if (spirvPath.empty() || !ReadWholeFileWords(spirvPath.c_str(), spirv) || spirv[0] != SPIRV_MAGIC) {
					return false;
				}

				SpvReflectShaderModule reflectModule{};
				if (spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &reflectModule)
				    != SPV_REFLECT_RESULT_SUCCESS) {
					return false;
				}

				// エントリ名で引けなければ先頭のエントリで代用する(SPIR-V のエントリは 1 本だけ)。
				const SpvReflectEntryPoint* entryPoint = spvReflectGetEntryPoint(&reflectModule, SPIRV_ENTRY_NAME);
				if (entryPoint == nullptr && reflectModule.entry_point_count > 0) {
					entryPoint = &reflectModule.entry_points[0];
				}

				bool ok = false;
				if (entryPoint != nullptr
				    && entryPoint->local_size.x > 0 && entryPoint->local_size.y > 0 && entryPoint->local_size.z > 0) {
					outSize = MTLSizeMake(entryPoint->local_size.x, entryPoint->local_size.y, entryPoint->local_size.z);
					ok      = true;
				}

				spvReflectDestroyShaderModule(&reflectModule);
				return ok;
			}
		}


		MetalRenderContextImpl::MetalRenderContextImpl(MetalGraphicsDeviceImpl* device)
			: device_(device)
			, pending_()
			, pendingCompute_()
			, colorRTs_()
			, colorRTCount_(0)
			, depthRT_(nullptr)
			, depthOnlyMap_(nullptr)
			, depthOnlySlice_(0)
			, encoder_(nil)
			, computeEncoder_(nil)
			, clearColorMask_(0)
			, clearColors_()
			, clearDepthPending_(false)
			, pipelineCache_()
			, depthStencilCache_()
			, fallbackTexture_(nil)
			, fallbackSampler_(nil)
			, fallbackTextureLoggedMask_(0)
			, fallbackSamplerLoggedMask_(0)
			, csFallbackTextureLoggedMask_(0)
			, csFallbackSamplerLoggedMask_(0)
			, missingUavLogged_(false)
			, threadGroupSizes_()
		{
			// PSO / 深度ステートのキャッシュとフォールバックは MTLDevice が要る。
			// SetupRenderContext は Initialize の後に呼ばれるので、ここで揃えられる。
			id<MTLDevice> mtlDevice = (device_ != nullptr)
				? static_cast<id<MTLDevice>>(device_->GetMTLDeviceHandle())
				: nil;
			if (mtlDevice == nil) {
				aq::StartupLog("[MetalRenderContext] MTLDevice が取れませんでした。描画は行われません");
				return;
			}

			pipelineCache_     = std::make_unique<MetalPipelineCache>(mtlDevice);
			depthStencilCache_ = std::make_unique<MetalDepthStencilCache>(mtlDevice);
			CreateFallbackResources(mtlDevice);
		}


		MetalRenderContextImpl::~MetalRenderContextImpl()
		{
			// エンコーダを開いたまま壊れるとコマンドバッファが不正になる。必ず閉じてから解放する。
			EndEncodingIfActive();

			[fallbackTexture_ release];
			fallbackTexture_ = nil;
			[fallbackSampler_ release];
			fallbackSampler_ = nil;
		}


		void MetalRenderContextImpl::CreateFallbackResources(id<MTLDevice> device)
		{
			@autoreleasepool
			{
				// 1x1 の白。UI シェーダは「テクスチャ無し」の矩形でも t0 を宣言しているので、
				// nil のまま描くと Validation エラーになる。白なら頂点カラーがそのまま出る。
				MTLTextureDescriptor* textureDesc =
					[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
					                                                  width:1
					                                                 height:1
					                                              mipmapped:NO];
				textureDesc.usage       = MTLTextureUsageShaderRead;
				textureDesc.storageMode = MTLStorageModeShared;

				fallbackTexture_ = [device newTextureWithDescriptor:textureDesc];
				if (fallbackTexture_ != nil) {
					const uint8_t white[4] = { 255, 255, 255, 255 };
					[fallbackTexture_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
					                    mipmapLevel:0
					                      withBytes:white
					                    bytesPerRow:sizeof(white)];
				} else {
					aq::StartupLog("[MetalRenderContext] フォールバック白テクスチャの生成に失敗しました");
				}

				MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
				samplerDesc.minFilter    = MTLSamplerMinMagFilterLinear;
				samplerDesc.magFilter    = MTLSamplerMinMagFilterLinear;
				samplerDesc.mipFilter    = MTLSamplerMipFilterLinear;
				samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
				samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
				samplerDesc.rAddressMode = MTLSamplerAddressModeClampToEdge;

				fallbackSampler_ = [device newSamplerStateWithDescriptor:samplerDesc];
				[samplerDesc release];

				if (fallbackSampler_ == nil) {
					aq::StartupLog("[MetalRenderContext] フォールバックサンプラの生成に失敗しました");
				}
			}
		}


		/**
		 * エンコーダ / 保留クリア
		 */
		void MetalRenderContextImpl::EndEncodingIfActive()
		{
			// **描画用と compute 用の両方を閉じる**(設計書 §3.1)。Metal は種類の違う
			// エンコーダを同時に開けず、閉じ忘れたまま次を開くとその場で落ちる。
			// 「閉じる」経路を 1 本に集約しておくことで、Present / CopyToBackBuffer /
			// OMSet* / Clear* といった既存の呼び出し側が種類を意識しなくて済む。
			EndRenderEncodingIfActive();
			EndComputeEncodingIfActive();
		}


		void MetalRenderContextImpl::EndRenderEncodingIfActive()
		{
			if (encoder_ == nil) {
				return;
			}

			[encoder_ endEncoding];
			[encoder_ release];
			encoder_ = nil;
		}


		void MetalRenderContextImpl::EndComputeEncodingIfActive()
		{
			if (computeEncoder_ == nil) {
				return;
			}

			[computeEncoder_ endEncoding];
			[computeEncoder_ release];
			computeEncoder_ = nil;
		}


		MTLRenderPassDescriptor* MetalRenderContextImpl::BuildRenderPassDescriptor()
		{
			// 深度のみパス(シャドウ)。カラー 0 本 + depthAttachment だけを組む(設計書 §3.3)。
			// **ここを通さないと深度マップへ 1 本も描けない**(P2 まではここで nil を返していた)。
			if (depthOnlyMap_ != nullptr)
			{
				id<MTLTexture> depthTexture = depthOnlyMap_->GetTexture();
				if (depthTexture == nil) {
					return nil;
				}

				MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];  // autorelease
				MTLRenderPassDepthAttachmentDescriptor* attachment = descriptor.depthAttachment;

				// 深度マップは 2D 配列なので、**どのカスケードへ描くかは slice で選ぶ**。
				attachment.texture     = depthTexture;
				attachment.level       = 0;
				attachment.slice       = depthOnlySlice_;
				attachment.storeAction = MTLStoreActionStore;
				attachment.loadAction  = clearDepthPending_ ? MTLLoadActionClear : MTLLoadActionLoad;
				attachment.clearDepth  = DEPTH_CLEAR_VALUE;

				// このパスが実際にクリアを行うので、予約はここで消費する。
				// カラーの予約は触らない(カラーが 0 本なので、このパスでは何もクリアしていない)。
				clearDepthPending_ = false;
				return descriptor;
			}

			// カラーも深度マップも無ければパスを組めない。
			if (colorRTCount_ == 0) {
				return nil;
			}

			MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];  // autorelease
			for (uint32_t i = 0; i < colorRTCount_; ++i)
			{
				// プロキシ RT は drawable を取れていないと実体が nil になる。
				// 中途半端なパスを開くと Metal が検証エラーを出すので、まるごと諦める。
				id<MTLTexture> texture = (colorRTs_[i] != nullptr) ? colorRTs_[i]->GetTexture() : nil;
				if (texture == nil) {
					return nil;
				}

				MTLRenderPassColorAttachmentDescriptor* attachment = descriptor.colorAttachments[i];
				attachment.texture     = texture;
				attachment.storeAction = MTLStoreActionStore;
				if ((clearColorMask_ & (1u << i)) != 0) {
					attachment.loadAction = MTLLoadActionClear;
					attachment.clearColor = MTLClearColorMake(clearColors_[i][0], clearColors_[i][1],
					                                          clearColors_[i][2], clearColors_[i][3]);
				} else {
					// 予約が無ければ既存内容を残す。DontCare にすると前のパスの結果が消える。
					attachment.loadAction = MTLLoadActionLoad;
				}
			}

			// 深度は自前深度を持つ RT か、OMSetRenderTargetWithDepth で渡された相手から取る。
			// ポストプロセスの RT のように深度を持たない構成もある。
			if (depthRT_ != nullptr && depthRT_->HasDepth())
			{
				MTLRenderPassDepthAttachmentDescriptor* attachment = descriptor.depthAttachment;
				attachment.texture     = depthRT_->GetDepthTexture();
				attachment.storeAction = MTLStoreActionStore;
				attachment.loadAction  = clearDepthPending_ ? MTLLoadActionClear : MTLLoadActionLoad;
				attachment.clearDepth  = DEPTH_CLEAR_VALUE;
			}

			// このパスが実際にクリアを行うので、予約はここで消費する。
			clearColorMask_    = 0;
			clearDepthPending_ = false;
			return descriptor;
		}


		void MetalRenderContextImpl::OpenEncoderIfNeeded()
		{
			if (encoder_ != nil || device_ == nullptr) {
				return;
			}

			// **compute エンコーダが開いていれば先に閉じる**(設計書 §3.1)。Metal は
			// MTLRenderCommandEncoder と MTLComputeCommandEncoder を同時に開けない。
			// ポストプロセスは Dispatch の直後に UI を描くので、ここは実際に通る経路。
			EndComputeEncodingIfActive();

			// 描画にもコマンドバッファが要る。まだフレームが始まっていなければここで始める(設計書 §2.3)。
			device_->BeginFrameIfNeeded();
			id<MTLCommandBuffer> commandBuffer =
				static_cast<id<MTLCommandBuffer>>(device_->GetCurrentCommandBufferHandle());
			if (commandBuffer == nil) {
				// drawable が取れずフレームを捨てた。クリア予約は次のフレームへ持ち越す。
				return;
			}

			@autoreleasepool
			{
				// 予約クリアの消費は BuildRenderPassDescriptor() が一手に行う。ここでは触らない。
				MTLRenderPassDescriptor* descriptor = BuildRenderPassDescriptor();
				if (descriptor == nil) {
					return;
				}

				// renderCommandEncoderWithDescriptor: は autorelease なので retain して保持する。
				encoder_ = [[commandBuffer renderCommandEncoderWithDescriptor:descriptor] retain];
			}
		}


		void MetalRenderContextImpl::ApplyEncoderStates()
		{
			if (encoder_ == nil) {
				return;
			}

			// Metal はアタッチメントの外へはみ出したビューポート / シザーを弾くので、
			// RT の実サイズで丸める。
			//
			// **深度のみパス(シャドウ)はカラー RT が無い**。クランプ元をカラーのままにすると
			// 0 になってビューポートが潰れ、シャドウマップへ何も描かれない。
			// このパスの基準は深度マップの解像度(正方形)。
			const bool     depthOnly = (depthOnlyMap_ != nullptr);
			const uint32_t rtWidth   = depthOnly ? depthOnlyMap_->GetResolution()
			                                     : ((colorRTs_[0] != nullptr) ? colorRTs_[0]->GetWidth()  : 0);
			const uint32_t rtHeight  = depthOnly ? depthOnlyMap_->GetResolution()
			                                     : ((colorRTs_[0] != nullptr) ? colorRTs_[0]->GetHeight() : 0);

			// ビューポート。一度も RSSetViewport が来ていなければ RT 全面とみなす。
			MTLViewport viewport = pending_.viewport;
			if (viewport.width <= 0.0 || viewport.height <= 0.0)
			{
				viewport.originX = 0.0;
				viewport.originY = 0.0;
				viewport.width   = static_cast<double>(rtWidth);
				viewport.height  = static_cast<double>(rtHeight);
				viewport.znear   = 0.0;
				viewport.zfar    = 1.0;
			}
			if (rtWidth > 0 && rtHeight > 0)
			{
				if (viewport.originX < 0.0) { viewport.originX = 0.0; }
				if (viewport.originY < 0.0) { viewport.originY = 0.0; }
				if (viewport.originX + viewport.width  > static_cast<double>(rtWidth)) {
					viewport.width  = static_cast<double>(rtWidth)  - viewport.originX;
				}
				if (viewport.originY + viewport.height > static_cast<double>(rtHeight)) {
					viewport.height = static_cast<double>(rtHeight) - viewport.originY;
				}
			}
			[encoder_ setViewport:viewport];

			// シザー。Metal に「無効」が無いので、無効時は RT 全面を指定する(設計書 §3.3)。
			MTLScissorRect scissor = {};
			if (pending_.scissorEnabled)
			{
				scissor = pending_.scissor;
				if (scissor.x > rtWidth)  { scissor.x = rtWidth;  }
				if (scissor.y > rtHeight) { scissor.y = rtHeight; }
				if (scissor.x + scissor.width  > rtWidth)  { scissor.width  = rtWidth  - scissor.x; }
				if (scissor.y + scissor.height > rtHeight) { scissor.height = rtHeight - scissor.y; }
			}
			else
			{
				scissor.x      = 0;
				scissor.y      = 0;
				scissor.width  = rtWidth;
				scissor.height = rtHeight;
			}
			[encoder_ setScissorRect:scissor];

			// 深度ステート。**深度アタッチメントが無いパスで深度書き込みを有効にすると Metal が弾く**
			// ため、深度が無ければ Disabled 相当へ落とす(意味としても正しい)。
			//
			// **深度のみパスはこの「Disabled 強制」に巻き込まないこと**。このパスは
			// depthAttachment が「ある」(depthOnlyMap_ のスライス)ので、条件に depthOnly を足す。
			// さらにモードは pending_.depth ではなく **ReadWrite を強制**する。シャドウパスの
			// 手前で UI などが DepthMode::Disabled を残していることがあり、そのまま使うと
			// 深度が 1 つも書かれず「全面が影」に戻ってしまう(Vulkan 版が PSO キーで
			// depthOnly のとき ReadWrite を固定しているのと同じ扱い)。
			if (depthStencilCache_ != nullptr)
			{
				const bool      hasDepth = depthOnly || ((depthRT_ != nullptr) && depthRT_->HasDepth());
				const DepthMode mode     = depthOnly ? DepthMode::ReadWrite
				                                     : (hasDepth ? pending_.depth : DepthMode::Disabled);

				id<MTLDepthStencilState> depthState = depthStencilCache_->Get(mode);
				if (depthState != nil) {
					[encoder_ setDepthStencilState:depthState];
				}
			}
		}


		void MetalRenderContextImpl::FlushPendingClears()
		{
			// 描画があるときは Draw がエンコーダを開くついでに loadAction = Clear へ畳むので、
			// ここへ来た時点で予約は空になっている(= この空エンコーダは出ない)。
			// P1 は描画が 1 本も無く、予約が残ったままフレームが終わるため、ここで実際に塗る。
			if (clearColorMask_ == 0 && !clearDepthPending_) {
				return;
			}

			// 予約が残るのはエンコーダが開かれなかったときだけだが(Clear* は必ず閉じる)、念のため。
			EndEncodingIfActive();

			if (device_ == nullptr) {
				return;
			}

			// クリアもコマンドバッファが要る。まだフレームが始まっていなければここで始める(設計書 §2.3)。
			device_->BeginFrameIfNeeded();
			id<MTLCommandBuffer> commandBuffer =
				static_cast<id<MTLCommandBuffer>>(device_->GetCurrentCommandBufferHandle());
			if (commandBuffer == nil) {
				// drawable が取れずフレームを捨てた。予約は次のフレームへ持ち越す。
				return;
			}

			@autoreleasepool
			{
				MTLRenderPassDescriptor* descriptor = BuildRenderPassDescriptor();
				if (descriptor == nil) {
					return;
				}

				// 空のエンコーダ。実際に塗るのは loadAction = Clear なので描画コマンドは要らない。
				id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
				[encoder endEncoding];
			}
		}


		void MetalRenderContextImpl::SetAttachments(MetalRenderTarget* const* colorTargets,
		                                            const uint32_t            count,
		                                            MetalRenderTarget*        depthTarget)
		{
			const uint32_t colorCount = (count < MAX_MRT) ? count : MAX_MRT;

			// 構成が同一ならエンコーダを開いたまま維持する(設計書 §3.1)。
			// **深度のみパスの最中は必ず組み直す**。カラー 0 本の構成同士は colorRTCount_ と
			// depthRT_ が一致してしまい、シャドウパスから抜けられなくなる。
			bool sameConfig = (depthOnlyMap_ == nullptr) && (colorCount == colorRTCount_) && (depthTarget == depthRT_);
			for (uint32_t i = 0; sameConfig && i < colorCount; ++i) {
				sameConfig = (colorTargets[i] == colorRTs_[i]);
			}
			if (sameConfig) {
				return;
			}

			// 予約中のクリアは「今の構成」宛て。差し替える前にここで確定させる。
			// エンジンの Clear は D3D11 由来で即時実行の意味なので、描画が 1 本も無いまま
			// RT を切り替えても、クリアだけは効いていなければならない。
			// 描画があるときは Draw がエンコーダを開くついでに畳むので、ここで空パスは出ない。
			FlushPendingClears();
			EndEncodingIfActive();

			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				colorRTs_[i] = (i < colorCount) ? colorTargets[i] : nullptr;
			}
			colorRTCount_ = colorCount;
			depthRT_      = depthTarget;

			// カラーを伴う構成へ移る = 深度のみパスは終わり。
			// **FlushPendingClears() より後に落とすこと**。先に落とすと、消費されていない
			// 深度クリアの予約が対象を失って次のパスへ紛れ込む。
			depthOnlyMap_   = nullptr;
			depthOnlySlice_ = 0;

			// PSO キーにも反映する(設計書 §4.1)。P2 の Draw 時 flush がここを見る。
			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				pending_.colorFormat[i] = (colorRTs_[i] != nullptr) ? colorRTs_[i]->GetPixelFormat()
				                                                   : MTLPixelFormatInvalid;
			}
			pending_.colorCount    = colorCount;
			pending_.depthFormat   = (depthRT_ != nullptr) ? depthRT_->GetDepthPixelFormat() : MTLPixelFormatInvalid;
			pending_.pipelineDirty = true;
		}


		/**
		 * レンダーターゲット / クリア
		 */
		void MetalRenderContextImpl::OMSetRenderTargets(const uint32_t numViews, IRenderTarget* renderTarget)
		{
			MetalRenderTarget* target = (numViews > 0) ? static_cast<MetalRenderTarget*>(renderTarget) : nullptr;

			// 自前深度を持つ RT ならそれを深度ソースにする(ポストプロセスの RT は深度なし)。
			MetalRenderTarget* depthTarget = (target != nullptr && target->HasDepth()) ? target : nullptr;
			SetAttachments(&target, (target != nullptr) ? 1u : 0u, depthTarget);
		}


		void MetalRenderContextImpl::OMSetMRTRenderTargets(const uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			// TODO(P4): MRT 描画そのもの (SV_Target0..7) は P4。ここで構成だけ憶えておかないと、
			//           GBuffer 宛てのクリア予約が直前の RT へ紛れ込む。
			if (renderTargets == nullptr) {
				SetAttachments(nullptr, 0u, nullptr);
				return;
			}

			MetalRenderTarget* targets[MAX_MRT] = {};
			MetalRenderTarget* depthTarget      = nullptr;
			const uint32_t     count            = (numViews < MAX_MRT) ? numViews : MAX_MRT;
			for (uint32_t i = 0; i < count; ++i)
			{
				targets[i] = static_cast<MetalRenderTarget*>(renderTargets[i]);
				if (depthTarget == nullptr && targets[i] != nullptr && targets[i]->HasDepth()) {
					depthTarget = targets[i];
				}
			}
			SetAttachments(targets, count, depthTarget);
		}


		void MetalRenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			// TODO(P4): フォワードパスの本実装は P4。P1 は構成の記録だけ。
			MetalRenderTarget* target = static_cast<MetalRenderTarget*>(&colorRT);
			SetAttachments(&target, 1u, static_cast<MetalRenderTarget*>(&depthSourceRT));
		}


		void MetalRenderContextImpl::OMSetDepthMode(const DepthMode mode)
		{
			// MTLDepthStencilState は PSO と別オブジェクトなので、PSO キーには入れない(設計書 §4.3)。
			pending_.depth = mode;
		}


		void MetalRenderContextImpl::OMSetBlendMode(const BlendMode mode)
		{
			if (pending_.blend != mode) {
				pending_.blend         = mode;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::RSSetViewport(const float topLeftX, const float topLeftY,
		                                           const float width, const float height)
		{
			// Metal の NDC は D3D と同じ (Y 下向き / Z が [0,1]) なので Y-flip は要らない(設計書 §2.4)。
			pending_.viewport.originX = topLeftX;
			pending_.viewport.originY = topLeftY;
			pending_.viewport.width   = width;
			pending_.viewport.height  = height;
			pending_.viewport.znear   = 0.0;
			pending_.viewport.zfar    = 1.0;
		}


		void MetalRenderContextImpl::RSSetScissorEnabled(const bool enabled)
		{
			// Metal に「シザー無効」は無いため、無効時は RT 全面を指定することになる(設計書 §3.3)。
			pending_.scissorEnabled = enabled;
		}


		void MetalRenderContextImpl::RSSetScissorRect(const int x, const int y, const int w, const int h)
		{
			// MTLScissorRect は符号無しで、RT の外へはみ出すと Metal が落とすため負値を丸める。
			const int clampedX = (x > 0) ? x : 0;
			const int clampedY = (y > 0) ? y : 0;
			const int clampedW = (w > 0) ? w : 0;
			const int clampedH = (h > 0) ? h : 0;
			pending_.scissor.x      = static_cast<NSUInteger>(clampedX);
			pending_.scissor.y      = static_cast<NSUInteger>(clampedY);
			pending_.scissor.width  = static_cast<NSUInteger>(clampedW);
			pending_.scissor.height = static_cast<NSUInteger>(clampedH);
		}


		void MetalRenderContextImpl::ClearRenderTargetView(const uint32_t index, float* clearColor)
		{
			// Metal には「エンコーダ内で RT をクリアする」コマンドが無い。即実行はせず、
			// 次にこのアタッチメントでエンコーダを開くときの loadAction = Clear へ予約する(設計書 §3.1)。
			if (index >= colorRTCount_ || index >= MAX_MRT) {
				return;
			}

			// 開いているパスの loadAction はもう変えられないので、いったん閉じて開き直させる。
			EndEncodingIfActive();

			if (clearColor != nullptr) {
				for (uint32_t i = 0; i < 4; ++i) {
					clearColors_[index][i] = clearColor[i];
				}
			}
			clearColorMask_ |= (1u << index);
		}


		void MetalRenderContextImpl::ClearDepthBuffer()
		{
			// 深度を持たない構成(ポストプロセスの RT 等)では予約しない。
			// 予約したまま誰にも消費されないと、後続の別パスが巻き添えでクリアされる。
			if (depthRT_ == nullptr || !depthRT_->HasDepth()) {
				return;
			}

			// カラーと同じく、次に開くパスの depthAttachment.loadAction = Clear として予約する。
			EndEncodingIfActive();
			clearDepthPending_ = true;
		}


		/**
		 * 入力アセンブラ
		 */
		void MetalRenderContextImpl::IASetVertexBuffer(IVertexBuffer& vertexBuffer)
		{
			// 束ねるのは Draw 時 flush。ここでは保留するだけ(設計書 §3.2)。
			pending_.vb[0] = &vertexBuffer;

			// slot0 を差し替えたら slot1 は持ち越さない(次の描画がインスタンスとは限らない)。
			pending_.vb[1] = nullptr;
		}


		void MetalRenderContextImpl::IASetVertexBufferSlot(const uint32_t slot, IVertexBuffer& vertexBuffer)
		{
			// slot0 = per-vertex(buffer 30)、slot1 = per-instance(buffer 29)。
			// D3D12 は任意スロットを受けるが、本バックエンドが使うのは 0 と 1 だけ(設計書 §5.2)。
			if (slot < PendingGraphicsState::MAX_VERTEX_STREAM) {
				pending_.vb[slot] = &vertexBuffer;
			}
		}


		void MetalRenderContextImpl::IASetIndexBuffer(IIndexBuffer& indexBuffer)
		{
			// drawIndexedPrimitives: の引数として渡すので、保留するだけでよい。
			pending_.ib    = &indexBuffer;
			pending_.gpuIB = nullptr;   // CPU 側 IB と GPU 駆動 IB は排他
		}


		/**
		 * GPU 駆動カリングが compact した IB をバインドする(設計書 §12 の P5)。
		 *
		 * ClusterCull.fx が RWByteAddressBuffer へ **R32_UINT** で書くので、
		 * インデックス型は常に 32bit。CPU 側 IB とは排他にする。
		 */
		void MetalRenderContextImpl::IASetIndexBufferGpu(IGpuBuffer& indexBuffer)
		{
			pending_.gpuIB = &indexBuffer;
			pending_.ib    = nullptr;
		}


		void MetalRenderContextImpl::IASetPrimitiveTopology(const PrimitiveTopology topology)
		{
			// PSO 側は class (point/line/triangle) 単位なので、変化時は PSO も引き直す(設計書 §3.3)。
			if (pending_.topology != topology) {
				pending_.topology      = topology;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::IASetInputLayout(IShader& vsShader)
		{
			// 入力レイアウトは VS のリフレクション由来(設計書 §9.3)。MTLVertexDescriptor は
			// MetalShader が持っているので、ここは VS を記録するだけでよい(VSSetShader と同源)。
			MetalShader* vs = static_cast<MetalShader*>(&vsShader);
			if (pending_.vs != vs) {
				pending_.vs            = vs;
				pending_.pipelineDirty = true;
			}
		}


		/**
		 * シェーダ / 定数バッファ / リソース
		 */
		void MetalRenderContextImpl::VSSetShader(IShader& shader)
		{
			MetalShader* vs = static_cast<MetalShader*>(&shader);
			if (pending_.vs != vs) {
				pending_.vs            = vs;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::VSSetConstantBuffer(const uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// b レジスタはシフト 0 なので、スロット番号がそのまま buffer index になる(設計書 §5.1)。
			if (startSlot < PendingGraphicsState::MAX_CONSTANT_COUNT) {
				pending_.vsCB[startSlot] = &constantBuffer;
			}
		}


		void MetalRenderContextImpl::PSSetShader(IShader& shader)
		{
			MetalShader* ps = static_cast<MetalShader*>(&shader);
			if (pending_.ps != ps) {
				pending_.ps            = ps;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::PSUnsetShader()
		{
			if (pending_.ps != nullptr) {
				pending_.ps            = nullptr;
				pending_.pipelineDirty = true;
			}
		}


		void MetalRenderContextImpl::PSSetConstantBuffer(const uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// Metal は VS / PS で buffer 空間が独立しているので、VS 側とは別に保持する。
			if (startSlot < PendingGraphicsState::MAX_CONSTANT_COUNT) {
				pending_.psCB[startSlot] = &constantBuffer;
			}
		}


		void MetalRenderContextImpl::PSSetShaderResource(const uint32_t startSlot, IShaderResourceView& shaderResourceView)
		{
			// **実体の解決は Draw 時**に GetNativeHandle() で行う(設計書 §5.1)。
			// UI の DeferredSRV のようにロード完了で中身が差し替わるラッパがあるため、
			// ここで id<MTLTexture> にしてしまうと「ロード前に束ねた nil」が残る。
			if (startSlot < PendingGraphicsState::MAX_SRV_COUNT) {
				pending_.psSRV[startSlot] = &shaderResourceView;
			}
		}


		void MetalRenderContextImpl::PSUnsetShaderResource(const uint32_t slot)
		{
			// nil のまま描くと Validation エラーになるので、Draw 時にフォールバックへ差し替える。
			if (slot < PendingGraphicsState::MAX_SRV_COUNT) {
				pending_.psSRV[slot] = nullptr;
			}
		}


		void MetalRenderContextImpl::PSSetSampler(const uint32_t startSlot, ISamplerState& samplerState)
		{
			// s レジスタはシフト 0。スロット番号がそのまま sampler index になる(設計書 §5.1)。
			if (startSlot < PendingGraphicsState::MAX_SAMPLER_COUNT) {
				pending_.psSampler[startSlot] = &samplerState;
			}
		}


		/**
		 * compute
		 */
		void MetalRenderContextImpl::CSSetShader(IShader& shader)
		{
			// compute PSO はここでは引かない。Dispatch までエンコーダが無いかもしれないうえ、
			// 描画側と同じく「保留して Dispatch でまとめて流す」ほうが経路が 1 本になる(設計書 §3.3)。
			pendingCompute_.cs = static_cast<MetalShader*>(&shader);
		}


		void MetalRenderContextImpl::CSUnsetShader()
		{
			// CS が無い状態で Dispatch が来たら捨てる(FlushComputeState が弾く)。
			pendingCompute_.cs = nullptr;
		}


		void MetalRenderContextImpl::CSSetConstantBuffer(const uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// b レジスタはシフト 0 なので、スロット番号がそのまま buffer index になる(設計書 §5.1)。
			if (startSlot < PendingComputeState::MAX_CONSTANT_COUNT) {
				pendingCompute_.cb[startSlot] = &constantBuffer;
			}
		}


		void MetalRenderContextImpl::CSSetSampler(const uint32_t startSlot, ISamplerState& samplerState)
		{
			// s レジスタもシフト 0。スロット番号がそのまま sampler index になる。
			if (startSlot < PendingComputeState::MAX_SAMPLER_COUNT) {
				pendingCompute_.sampler[startSlot] = &samplerState;
			}
		}


		void MetalRenderContextImpl::CSSetShaderResource(const uint32_t startSlot, IShaderResourceView& shaderResourceView)
		{
			// **実体の解決は Dispatch 時**に GetNativeHandle() で行う(描画側の PSSetShaderResource と同じ理由。
			// ロード完了で中身が差し替わるラッパがあるため、ここで id<MTLTexture> にすると nil が残る)。
			if (startSlot < PendingComputeState::MAX_SRV_COUNT) {
				pendingCompute_.srv[startSlot] = &shaderResourceView;
			}
		}


		void MetalRenderContextImpl::CSUnsetShaderResource(const uint32_t slot)
		{
			if (slot < PendingComputeState::MAX_SRV_COUNT) {
				pendingCompute_.srv[slot] = nullptr;
			}
		}


		void MetalRenderContextImpl::CSSetUnorderedAccessView(const uint32_t startSlot, IUnorderedAccessView& unorderedAccessView)
		{
			// テクスチャ UAV か バッファ UAV かの振り分けは Dispatch 時に MetalUAVBase 経由で行う
			// (RT の ColorUAV はプロキシだと実体が毎フレーム変わるので、ここで確定させられない)。
			if (startSlot < PendingComputeState::MAX_UAV_COUNT) {
				pendingCompute_.uav[startSlot] = &unorderedAccessView;
			}
		}


		void MetalRenderContextImpl::CSUnsetUnorderedAccessView(const uint32_t slot)
		{
			// **UAV にフォールバックは無い**。外したスロットは次の Dispatch で束ねられない。
			if (slot < PendingComputeState::MAX_UAV_COUNT) {
				pendingCompute_.uav[slot] = nullptr;
			}
		}


		/**
		 * 描画 / ディスパッチ
		 */
		bool MetalRenderContextImpl::FlushGraphicsState()
		{
			if (device_ == nullptr || pipelineCache_ == nullptr) {
				return false;
			}
			if (pending_.vs == nullptr) {
				return false;
			}

			// **深度のみパス(シャドウ)では PS 無し・カラー 0 本が正常**なので、ここで捨てない。
			// 通常パスだけ「PS とカラー RT が揃っていること」を要求する(Vulkan 版の FlushGraphics と同型)。
			const bool depthOnly = (depthOnlyMap_ != nullptr);
			if (!depthOnly && (pending_.ps == nullptr || colorRTCount_ == 0 || colorRTs_[0] == nullptr)) {
				return false;
			}

			OpenEncoderIfNeeded();
			if (encoder_ == nil) {
				return false;
			}

			MetalVertexBuffer* vertexBuffer   = static_cast<MetalVertexBuffer*>(pending_.vb[0]);
			MetalVertexBuffer* instanceBuffer = static_cast<MetalVertexBuffer*>(pending_.vb[1]);

			// ── PSO(設計書 §4.1)──
			// stride は**リフレクション値ではなく実バッファの値**を使う。DXC が未使用の入力を
			// 削るとリフレクション側が短くなり、2 個目以降の要素が 1 つずつずれて読まれる
			// (Vulkan 側で踏んだ穴。設計書 §13-5)。
			pending_.vertexStride = (vertexBuffer != nullptr) ? vertexBuffer->GetStride() : 0;
			// VS がインスタンス属性を持ち、かつ slot1 が束ねられているときだけ layouts[29] を作る
			// (片方だけでは PSO と頂点バッファが食い違う)。
			pending_.instanceStride = ((instanceBuffer != nullptr) && (pending_.vs->GetInstanceStride() > 0))
				? instanceBuffer->GetStride()
				: 0;

			MetalPipelineKey key = {};
			key.vsFunction     = static_cast<const void*>(pending_.vs->GetFunction());
			// 深度のみパスは fragment 関数が無い(MetalPipelineCache は nullptr を許容している)。
			key.psFunction     = (pending_.ps != nullptr) ? static_cast<const void*>(pending_.ps->GetFunction())
			                                             : nullptr;
			key.topologyClass  = static_cast<uint8_t>(metal::ToMTLTopologyClass(pending_.topology));
			key.blendMode      = static_cast<uint8_t>(pending_.blend);
			key.colorCount     = depthOnly ? 0 : static_cast<uint8_t>(colorRTCount_);
			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				key.colorFormat[i] = pending_.colorFormat[i];
			}
			key.depthFormat    = pending_.depthFormat;
			key.vertexStride   = pending_.vertexStride;
			key.instanceStride = pending_.instanceStride;

			id<MTLRenderPipelineState> pipelineState =
				pipelineCache_->GetOrCreate(key, pending_.vs->GetVertexDescriptor());
			if (pipelineState == nil) {
				return false;
			}
			[encoder_ setRenderPipelineState:pipelineState];
			pending_.pipelineDirty = false;

			// ── エンコーダ固有のステート ──
			// エンコーダを開き直すと消えるので、Draw ごとに流し直す。
			ApplyEncoderStates();

			// ── 頂点ストリーム(設計書 §5.2)──
			if (vertexBuffer != nullptr && vertexBuffer->GetBuffer() != nil)
			{
				[encoder_ setVertexBuffer:vertexBuffer->GetBuffer()
				                   offset:vertexBuffer->GetCurrentOffset()
				                  atIndex:metal::VERTEX_BUFFER_INDEX];
			}
			if (pending_.instanceStride > 0 && instanceBuffer != nullptr && instanceBuffer->GetBuffer() != nil)
			{
				[encoder_ setVertexBuffer:instanceBuffer->GetBuffer()
				                   offset:instanceBuffer->GetCurrentOffset()
				                  atIndex:metal::INSTANCE_BUFFER_INDEX];
			}

			// ── 定数バッファ(b はシフト 0。スロット番号がそのまま index)──
			// offset は **GetCurrentOffset()**。CB は Update ごとに別スライスへ確保されるので、
			// ここを 0 にすると全オブジェクトが最後の行列で描かれる(設計書 §13-7)。
			for (uint32_t i = 0; i < PendingGraphicsState::MAX_CONSTANT_COUNT; ++i)
			{
				MetalConstantBuffer* constantBuffer = static_cast<MetalConstantBuffer*>(pending_.vsCB[i]);
				if (constantBuffer == nullptr || constantBuffer->GetBuffer() == nil) {
					continue;
				}
				[encoder_ setVertexBuffer:constantBuffer->GetBuffer()
				                   offset:constantBuffer->GetCurrentOffset()
				                  atIndex:i];
			}
			// 深度のみパスは fragment 関数を持たない。**fragment 側のバインドは丸ごと要らない**
			// (束ねても無視されるだけだが、シャドウは描画数が多いので無駄を出さない)。
			if (depthOnly) {
				pending_.bindingDirty = false;
				return true;
			}

			for (uint32_t i = 0; i < PendingGraphicsState::MAX_CONSTANT_COUNT; ++i)
			{
				MetalConstantBuffer* constantBuffer = static_cast<MetalConstantBuffer*>(pending_.psCB[i]);
				if (constantBuffer == nullptr || constantBuffer->GetBuffer() == nil) {
					continue;
				}
				[encoder_ setFragmentBuffer:constantBuffer->GetBuffer()
				                     offset:constantBuffer->GetCurrentOffset()
				                    atIndex:i];
			}

			// ── テクスチャ / サンプラ(t はシフト 8、s はシフト 0)──
			// **未バインドのスロットもフォールバックで埋める**。シェーダが宣言している引数が
			// nil だと Metal API Validation がエラーにするため(Vulkan 版と同じ考え方)。
			for (uint32_t i = 0; i < BIND_SRV_COUNT; ++i)
			{
				id<MTLTexture> texture = nil;
				if (pending_.psSRV[i] != nullptr) {
					// MetalTexture / MetalRenderTarget::ColorSRV / MetalDepthMap::DepthSRV は
					// いずれも id<MTLTexture> をそのまま返す規約(設計書 §5.1)。
					texture = static_cast<id<MTLTexture>>(pending_.psSRV[i]->GetNativeHandle());
				}

				if (texture == nil)
				{
					texture = fallbackTexture_;
					if ((fallbackTextureLoggedMask_ & (1u << i)) == 0)
					{
						fallbackTextureLoggedMask_ |= (1u << i);
						char msg[128];
						std::snprintf(msg, sizeof(msg),
						              "[MetalRenderContext] t%u が未バインドのため白テクスチャで埋めました", i);
						aq::StartupLog(msg);
					}
				}
				[encoder_ setFragmentTexture:texture atIndex:(metal::SRV_INDEX_SHIFT + i)];
			}
			for (uint32_t i = 0; i < BIND_SAMPLER_COUNT; ++i)
			{
				MetalSampler*       sampler      = static_cast<MetalSampler*>(pending_.psSampler[i]);
				id<MTLSamplerState> samplerState = (sampler != nullptr) ? sampler->GetSampler() : nil;

				if (samplerState == nil)
				{
					samplerState = fallbackSampler_;
					if ((fallbackSamplerLoggedMask_ & (1u << i)) == 0)
					{
						fallbackSamplerLoggedMask_ |= (1u << i);
						char msg[128];
						std::snprintf(msg, sizeof(msg),
						              "[MetalRenderContext] s%u が未バインドのため既定サンプラで埋めました", i);
						aq::StartupLog(msg);
					}
				}
				[encoder_ setFragmentSamplerState:samplerState atIndex:i];
			}

			pending_.bindingDirty = false;
			return true;
		}


		void MetalRenderContextImpl::Draw(const uint32_t vertexCount, const uint32_t startVertexLocation)
		{
			if (vertexCount == 0) {
				return;
			}

			// 毎フレーム通る経路なので autorelease を溜めない(MRR。設計書 §10)。
			@autoreleasepool
			{
				if (!FlushGraphicsState()) {
					return;
				}

				[encoder_ drawPrimitives:metal::ToMTLPrimitiveType(pending_.topology)
				             vertexStart:startVertexLocation
				             vertexCount:vertexCount];
			}
		}


		void MetalRenderContextImpl::DrawIndexedInternal(const uint32_t indexCount,
		                                                 const uint32_t startIndexLocation,
		                                                 const uint32_t instanceCount,
		                                                 const int32_t  baseVertexLocation,
		                                                 const uint32_t startInstanceLocation)
		{
			MetalIndexBuffer* indexBuffer = static_cast<MetalIndexBuffer*>(pending_.ib);
			if (indexCount == 0 || instanceCount == 0 || indexBuffer == nullptr || indexBuffer->GetBuffer() == nil) {
				return;
			}

			@autoreleasepool
			{
				if (!FlushGraphicsState()) {
					return;
				}

				// indexBufferOffset は**バイト単位**。動的 IB のスライス offset に、
				// 開始インデックスのバイト数を足したものになる(片方を忘れるとインデックスがずれる)。
				const NSUInteger indexOffset = static_cast<NSUInteger>(indexBuffer->GetCurrentOffset())
				                             + static_cast<NSUInteger>(startIndexLocation) * indexBuffer->GetIndexStride();

				// 通常描画も instanceCount = 1 のインスタンス描画として出す。
				// baseVertex / baseInstance が要るのはインスタンス側だけだが、分けても差は無い。
				[encoder_ drawIndexedPrimitives:metal::ToMTLPrimitiveType(pending_.topology)
				                     indexCount:indexCount
				                      indexType:indexBuffer->GetIndexType()
				                    indexBuffer:indexBuffer->GetBuffer()
				              indexBufferOffset:indexOffset
				                  instanceCount:instanceCount
				                     baseVertex:baseVertexLocation
				                   baseInstance:startInstanceLocation];
			}
		}


		void MetalRenderContextImpl::DrawIndexed(const uint32_t indexCount)
		{
			DrawIndexedInternal(indexCount, 0, 1, 0, 0);
		}


		void MetalRenderContextImpl::DrawIndexed(const uint32_t indexCount, const uint32_t startIndexLocation)
		{
			DrawIndexedInternal(indexCount, startIndexLocation, 1, 0, 0);
		}


		/**
		 * インデックス付きインスタンス描画。
		 *
		 * IRenderContextImpl の既定は no-op。override し忘れると
		 * **インスタンス描画が 1 つも発行されない**(Vulkan 側で踏んだ穴)。
		 * per-instance ストリーム(buffer 29)の束ねは FlushGraphicsState() が行う。
		 */
		void MetalRenderContextImpl::DrawIndexedInstanced(const uint32_t indexCount, const uint32_t instanceCount,
		                                                  const uint32_t startIndexLocation, const int32_t baseVertexLocation,
		                                                  const uint32_t startInstanceLocation)
		{
			DrawIndexedInternal(indexCount, startIndexLocation, instanceCount, baseVertexLocation, startInstanceLocation);
		}


		void MetalRenderContextImpl::OpenComputeEncoderIfNeeded()
		{
			// 連続 Dispatch では開いたままにする(開き直すと束ね直しが要るうえ、
			// エンコーダ境界ごとに GPU の同期点が入る)。
			if (computeEncoder_ != nil || device_ == nullptr) {
				return;
			}

			// **描画エンコーダを先に閉じる**(設計書 §3.1)。Metal は両方を同時に開けない。
			EndRenderEncodingIfActive();

			// compute にもコマンドバッファが要る。まだフレームが始まっていなければここで始める(設計書 §2.3)。
			device_->BeginFrameIfNeeded();
			id<MTLCommandBuffer> commandBuffer =
				static_cast<id<MTLCommandBuffer>>(device_->GetCurrentCommandBufferHandle());
			if (commandBuffer == nil) {
				// drawable が取れずフレームを捨てた。このフレームの compute も丸ごと捨てる。
				return;
			}

			// **MTLDispatchTypeSerial を明示する**(引数無しの computeCommandEncoder と同じ既定)。
			// serial なら同一エンコーダ内の Dispatch 同士が順に実行され、前の書き込みが
			// 次の読み取りから見える。UavBarrier() を no-op にできるのはこのため(設計書 §8)。
			@autoreleasepool
			{
				computeEncoder_ = [[commandBuffer computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial] retain];
			}
		}


		MTLSize MetalRenderContextImpl::GetThreadGroupSize(MetalShader* cs)
		{
			const void* key = static_cast<const void*>(cs);

			std::unordered_map<const void*, MTLSize>::const_iterator it = threadGroupSizes_.find(key);
			if (it != threadGroupSizes_.end()) {
				return it->second;
			}

			// **MSL には [numthreads(...)] 相当の情報が入らない**ので、隣の .spv の
			// OpExecutionMode LocalSize から拾う(設計書 §9.3 と同じ「.metal の隣の .spv」規約)。
			MTLSize size = MTLSizeMake(FALLBACK_THREADGROUP_X, FALLBACK_THREADGROUP_Y, FALLBACK_THREADGROUP_Z);

			const std::string resolved = ResolveShaderPath(cs->GetFilePath());
			const std::string spvPath  = BuildComputeSpirvPath(resolved.c_str(), cs->GetEntryFuncName());

			char msg[512];
			if (ReadLocalSizeFromSpirv(spvPath, size)) {
				// 1 シェーダにつき 1 行。10 本しか無いので、実際の値が追えるほうが得。
				std::snprintf(msg, sizeof(msg),
				              "[MetalRenderContext] CS threadsPerThreadgroup = %ux%ux%u  %s (%s)",
				              static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height),
				              static_cast<uint32_t>(size.depth), cs->GetFilePath(), cs->GetEntryFuncName());
				aq::StartupLog(msg);
			} else {
				// **決め打ちに落ちたことを必ず残す**。シェーダ側の [numthreads(...)] と食い違うと、
				// エラーも Validation も出ないまま結果だけが静かに壊れる(いちばん追いにくい形)。
				std::snprintf(msg, sizeof(msg),
				              "[MetalRenderContext] CS の .spv から [numthreads] を読めません。"
				              "**暫定で %ux%ux%u を使います**(シェーダ側と食い違うと結果が壊れます): %s",
				              FALLBACK_THREADGROUP_X, FALLBACK_THREADGROUP_Y, FALLBACK_THREADGROUP_Z,
				              spvPath.empty() ? cs->GetFilePath() : spvPath.c_str());
				aq::StartupLog(msg);
			}

			threadGroupSizes_[key] = size;
			return size;
		}


		bool MetalRenderContextImpl::FlushComputeState(MTLSize& outThreadsPerThreadgroup)
		{
			if (device_ == nullptr || pipelineCache_ == nullptr) {
				return false;
			}

			MetalShader* cs = pendingCompute_.cs;
			if (cs == nullptr || cs->GetFunction() == nil) {
				return false;
			}

			// ── UAV の有無を先に見る ──
			// **UAV にフォールバックを入れてはいけない**。UAV は compute の書き込み先なので、
			// 適当なテクスチャで埋めると「Validation は通るのに結果がどこにも残らない」形で
			// 静かに壊れる。1 本も束ねられないなら Dispatch ごと捨てるほうが安全。
			bool hasUav = false;
			for (uint32_t i = 0; i < BIND_UAV_COUNT && !hasUav; ++i) {
				hasUav = (pendingCompute_.uav[i] != nullptr);
			}
			if (!hasUav)
			{
				if (!missingUavLogged_)
				{
					missingUavLogged_ = true;
					char msg[512];
					std::snprintf(msg, sizeof(msg),
					              "[MetalRenderContext] UAV が 1 本も束ねられていないため Dispatch を捨てました: %s (%s)",
					              cs->GetFilePath(), cs->GetEntryFuncName());
					aq::StartupLog(msg);
				}
				return false;
			}

			id<MTLComputePipelineState> pipelineState = pipelineCache_->GetOrCreateCompute(cs->GetFunction());
			if (pipelineState == nil) {
				return false;
			}

			OpenComputeEncoderIfNeeded();
			if (computeEncoder_ == nil) {
				return false;
			}

			[computeEncoder_ setComputePipelineState:pipelineState];

			// ── 定数バッファ(b はシフト 0。スロット番号がそのまま index)──
			// offset は **GetCurrentOffset()**。CB は Update ごとに別スライスへ確保されるので、
			// ここを 0 にすると最後に Update した内容で全 Dispatch が走る(設計書 §13-7)。
			for (uint32_t i = 0; i < PendingComputeState::MAX_CONSTANT_COUNT; ++i)
			{
				MetalConstantBuffer* constantBuffer = static_cast<MetalConstantBuffer*>(pendingCompute_.cb[i]);
				if (constantBuffer == nullptr || constantBuffer->GetBuffer() == nil) {
					continue;
				}
				[computeEncoder_ setBuffer:constantBuffer->GetBuffer()
				                    offset:constantBuffer->GetCurrentOffset()
				                   atIndex:i];
			}

			// ── テクスチャ / サンプラ(t はシフト 8、s はシフト 0)──
			// 描画側と同じく**未バインドのスロットもフォールバックで埋める**。
			// Metal API Validation は「シェーダが宣言している引数が nil」をエラーにするため。
			for (uint32_t i = 0; i < BIND_SRV_COUNT; ++i)
			{
				IShaderResourceView* srv = pendingCompute_.srv[i];

				// テクスチャ SRV。GetNativeHandle() が id<MTLTexture> を返す規約(設計書 §5.1)。
				id<MTLTexture> texture = (srv != nullptr)
					? static_cast<id<MTLTexture>>(srv->GetNativeHandle())
					: nil;
				if (texture != nil) {
					[computeEncoder_ setTexture:texture atIndex:(metal::SRV_INDEX_SHIFT + i)];
					continue;
				}

				// バッファ SRV(StructuredBuffer / ByteAddressBuffer)は texture ではなく
				// **buffer 空間の同じ index** へ落ちる(設計書 §5.3)。GetNativeHandle() は
				// テクスチャ用の口なので nullptr しか返せず、MetalSRVBase 経由で取り直す。
				// 現在は Metal の SRV 実装すべて(MetalTexture / ColorSRV / BufferSRV / DepthSRV)が
				// MetalSRVBase を継承しているので static_cast でも通るが、基底を持たない実装が
				// 足されたときに静かに壊れるより nullptr が返るほうがよいので dynamic_cast のままにする
				// (ここはディスパッチごとに数回なのでコストは問題にならない)。
				MetalSRVBase* srvBase = (srv != nullptr) ? dynamic_cast<MetalSRVBase*>(srv) : nullptr;
				id<MTLBuffer> buffer  = (srvBase != nullptr) ? srvBase->GetBuffer() : nil;
				if (buffer != nil) {
					[computeEncoder_ setBuffer:buffer offset:0 atIndex:(metal::SRV_INDEX_SHIFT + i)];
					continue;
				}

				// 未バインド。シェーダが宣言している引数が nil だと Validation がエラーにするので、
				// 描画側と同じく白テクスチャで埋める(**UAV と違い読み取り専用なので無害**)。
				if ((csFallbackTextureLoggedMask_ & (1u << i)) == 0)
				{
					csFallbackTextureLoggedMask_ |= (1u << i);
					char msg[128];
					std::snprintf(msg, sizeof(msg),
					              "[MetalRenderContext] CS の t%u が未バインドのため白テクスチャで埋めました", i);
					aq::StartupLog(msg);
				}
				[computeEncoder_ setTexture:fallbackTexture_ atIndex:(metal::SRV_INDEX_SHIFT + i)];
			}
			for (uint32_t i = 0; i < BIND_SAMPLER_COUNT; ++i)
			{
				MetalSampler*       sampler      = static_cast<MetalSampler*>(pendingCompute_.sampler[i]);
				id<MTLSamplerState> samplerState = (sampler != nullptr) ? sampler->GetSampler() : nil;

				if (samplerState == nil)
				{
					samplerState = fallbackSampler_;
					if ((csFallbackSamplerLoggedMask_ & (1u << i)) == 0)
					{
						csFallbackSamplerLoggedMask_ |= (1u << i);
						char msg[128];
						std::snprintf(msg, sizeof(msg),
						              "[MetalRenderContext] CS の s%u が未バインドのため既定サンプラで埋めました", i);
						aq::StartupLog(msg);
					}
				}
				[computeEncoder_ setSamplerState:samplerState atIndex:i];
			}

			// ── UAV(u はシフト 24)──
			// HLSL の型次第でテクスチャにもバッファにもなるので、MetalUAVBase 経由で振り分ける
			// (テクスチャとバッファは Metal では別の番号空間。設計書 §5.3)。
			for (uint32_t i = 0; i < BIND_UAV_COUNT; ++i)
			{
				MetalUAVBase* uav = static_cast<MetalUAVBase*>(pendingCompute_.uav[i]);
				if (uav == nullptr) {
					continue;
				}

				id<MTLTexture> texture = uav->GetTexture();
				if (texture != nil) {
					[computeEncoder_ setTexture:texture atIndex:(metal::UAV_INDEX_SHIFT + i)];
					continue;
				}

				// バッファ UAV(RWByteAddressBuffer / RWStructuredBuffer)。
				// offset はビュー側が持たないので 0 固定でよい。
				id<MTLBuffer> buffer = uav->GetBuffer();
				if (buffer != nil) {
					[computeEncoder_ setBuffer:buffer offset:0 atIndex:(metal::UAV_INDEX_SHIFT + i)];
				}
			}

			outThreadsPerThreadgroup = GetThreadGroupSize(cs);
			return true;
		}


		/**
		 * 間接描画(GPU 駆動クラスタカリング)。
		 *
		 * **引数レイアウトは D3D12 / Vulkan / Metal で完全に同一**なので変換は要らない
		 * (設計書 §13-3 の懸念は実機の型定義とシェーダの書き込みを突き合わせて解消した)。
		 *
		 *   offset  ClusterCullReset.fx が書く値      MTLDrawIndexedPrimitivesIndirectArguments
		 *   ------  --------------------------------  -----------------------------------------
		 *        0  IndexCountPerInstance (uint)      indexCount    (uint32_t)
		 *        4  InstanceCount         (uint)      instanceCount (uint32_t)
		 *        8  StartIndexLocation    (uint)      indexStart    (uint32_t)
		 *       12  BaseVertexLocation    (int)       baseVertex    (int32_t)
		 *       16  StartInstanceLocation (uint)      baseInstance  (uint32_t)
		 *
		 * indexStart は**インデックス単位**(バイトではない)で、indexBufferOffset とは
		 * 別に加算される。ここでは IB の先頭から使うので indexBufferOffset は 0。
		 */
		void MetalRenderContextImpl::DrawIndexedIndirect(IGpuBuffer& argsBuffer)
		{
			MetalGpuBuffer* indexBuffer = static_cast<MetalGpuBuffer*>(pending_.gpuIB);
			MetalGpuBuffer* args        = static_cast<MetalGpuBuffer*>(&argsBuffer);
			if (indexBuffer == nullptr || indexBuffer->GetBuffer() == nil || args->GetBuffer() == nil) {
				return;
			}

			@autoreleasepool
			{
				if (!FlushGraphicsState()) {
					return;
				}

				[encoder_ drawIndexedPrimitives:metal::ToMTLPrimitiveType(pending_.topology)
				                      indexType:MTLIndexTypeUInt32
				                    indexBuffer:indexBuffer->GetBuffer()
				              indexBufferOffset:0
				                 indirectBuffer:args->GetBuffer()
				           indirectBufferOffset:0];
			}
		}


		void MetalRenderContextImpl::Dispatch(const uint32_t x, const uint32_t y, const uint32_t z)
		{
			if (x == 0 || y == 0 || z == 0) {
				return;
			}

			// 毎フレーム通る経路なので autorelease を溜めない(MRR。設計書 §10)。
			@autoreleasepool
			{
				// **予約中のクリアは「今のアタッチメント構成」宛て**なので、compute へ移る前に確定させる。
				// 予約を持ち越したまま compute が RT へ書くと、後続の描画が開くパスの
				// loadAction = Clear で compute の結果ごと消える。
				// 予約が無ければ即 return するので、通常は何も起きない(設計書 §3.1 への追加)。
				FlushPendingClears();

				MTLSize threadsPerThreadgroup = MTLSizeMake(1, 1, 1);
				if (!FlushComputeState(threadsPerThreadgroup)) {
					return;
				}

				// 抽象の Dispatch(x,y,z) は D3D の Dispatch と同じく**スレッドグループ数**。
				// Metal の dispatchThreadgroups: も同じ意味なので、そのまま渡してよい
				// (threadsPerThreadgroup のほうが HLSL の [numthreads(...)] に対応する)。
				[computeEncoder_ dispatchThreadgroups:MTLSizeMake(x, y, z)
				                threadsPerThreadgroup:threadsPerThreadgroup];
			}
		}


		void MetalRenderContextImpl::UavBarrier(IGpuBuffer& /*buffer*/)
		{
			// **Metal では no-op で足りる**(設計書 §8)。
			//
			// - エンコーダを跨ぐ依存(compute → graphics など)は MTLCommandBuffer の
			//   レベルで順序が保証される。追跡対象のリソース(MTLHeap を使っていない
			//   通常の MTLTexture / MTLBuffer)は Metal が自動で同期する。
			// - 同一エンコーダ内も、本バックエンドは MTLDispatchTypeSerial で開いているので
			//   Dispatch 同士が順に実行され、前の書き込みが次の読み取りから見える。
			//
			// 設計書は「同一エンコーダ内は memoryBarrierWithScope:」としているが、
			// **あれは MTLDispatchTypeConcurrent のエンコーダ専用の API** で、
			// serial のまま呼ぶと Metal API Validation がエラーにする。serial を選んでいる限り
			// バリアは不要なので、ここは意図的に何もしない(設計との差分。報告済み)。
		}


		void MetalRenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			// ここだけは P0 から実際に動かす。エンジンが毎フレーム呼ぶため、
			// 空にしておくと後のフェーズで原因の分かりにくい不具合になる。
			if (!data) {
				return;
			}
			static_cast<MetalConstantBuffer&>(buf).Update(data);
		}


		/**
		 * シャドウ深度パス
		 */
		void MetalRenderContextImpl::OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, const uint32_t slice)
		{
			// カラー 0 本 + 深度マップの 1 スライスへ切り替える(設計書 §3.3)。
			// VulkanRenderContextImpl::OMSetDepthOnlyTargetSlice と同じ構造。

			// 予約中のクリアは「今の構成」宛て。差し替える前にここで確定させる
			// (SetAttachments と同じ理由。設計書 §3.1 の 3 番目の呼び出し元にあたる)。
			FlushPendingClears();
			EndEncodingIfActive();

			for (uint32_t i = 0; i < MAX_MRT; ++i) {
				colorRTs_[i]            = nullptr;
				pending_.colorFormat[i] = MTLPixelFormatInvalid;
			}
			colorRTCount_ = 0;
			depthRT_      = nullptr;

			MetalDepthMap* target = static_cast<MetalDepthMap*>(&depthMap);
			depthOnlyMap_   = target;
			depthOnlySlice_ = slice;

			// PSO キーにも反映する(設計書 §4.1)。深度のみパスは color 0 本 + 深度フォーマットだけ。
			pending_.colorCount    = 0;
			pending_.depthFormat   = (target != nullptr) ? target->GetPixelFormat() : MTLPixelFormatInvalid;
			pending_.pipelineDirty = true;
		}


		void MetalRenderContextImpl::OMSetDepthOnlyTarget(IDepthMap& depthMap)
		{
			OMSetDepthOnlyTargetSlice(depthMap, 0);
		}


		void MetalRenderContextImpl::ClearDepthMapSlice(IDepthMap& /*depthMap*/, const uint32_t /*slice*/)
		{
			// 次に開く深度のみパスの depthAttachment.loadAction = Clear として予約する(設計書 §3.1)。
			// 対象とスライスは直前の OMSetDepthOnlyTargetSlice が既に決めているので引数は見ない
			// (Vulkan 版の ClearDepthMapSlice も pendingDepthClear_ を立てるだけ)。
			//
			// **P1/P2 はここで予約を立てていなかった**。消費側(color 0 本 + depthAttachment)が
			// 無く、予約が誰にも消費されないまま居座って別パスを巻き添えにするのを避けるためで、
			// P3 で BuildRenderPassDescriptor() に消費側を入れたので立てるのが正しくなった。
			if (depthOnlyMap_ == nullptr) {
				return;
			}

			// 開いているパスの loadAction はもう変えられないので、いったん閉じて開き直させる。
			EndEncodingIfActive();
			clearDepthPending_ = true;
		}


		void MetalRenderContextImpl::ClearDepthMap(IDepthMap& depthMap)
		{
			ClearDepthMapSlice(depthMap, 0);
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
