#include "aq.h"
// 非 Metal 構成 / ImGui 無効構成では本体をガードして空 TU にする(VulkanImGui.cpp と同じ作法)。
#if defined(ENGINE_GRAPHICS_METAL) && defined(AQ_IMGUI)
#include "Graphics/Metal/MetalImGui.h"
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/Metal/MetalGraphicsDeviceImpl.h"
#include "Graphics/Metal/MetalShader.h"
#include "Graphics/Metal/MetalResources.h"
#include "Graphics/Metal/MetalBuffers.h"
#include <imgui/imgui.h>

// 本 TU は手動参照カウント(MRR)前提。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalImGui.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace MetalImGui
		{
			namespace
			{
				/** デバイス。Init で掴み、Shutdown で捨てる */
				MetalGraphicsDeviceImpl* g_dev = nullptr;

				/**
				 * 専用 PSO と、それを作った宛先フォーマット。
				 *
				 * MTLRenderPipelineState は colorAttachments[0].pixelFormat を持つので、
				 * drawable のフォーマットが決まる Render() の初回で遅延生成する
				 * (MetalGraphicsDeviceImpl の EnsureFullscreenBlitPipeline と同じ手)。
				 */
				id<MTLRenderPipelineState> g_pipeline       = nil;
				MTLPixelFormat             g_pipelineFormat = MTLPixelFormatInvalid;

				/** ImGuiVK.fx から作った資源。VulkanImGui と同じ顔ぶれ */
				std::unique_ptr<IShader>             g_vs, g_ps;
				std::unique_ptr<IShaderResourceView> g_font;      // MetalTexture
				std::unique_ptr<ISamplerState>       g_sampler;   // MetalSampler
				std::unique_ptr<IConstantBuffer>     g_projCB;    // float4 scale/translate
				std::unique_ptr<IVertexBuffer>       g_vb;
				std::unique_ptr<IIndexBuffer>        g_ib;
				uint32_t g_vbCap = 0, g_ibCap = 0;  // 頂点数 / インデックス数

				/** フォントアトラスの MTLTexture(所有は g_font。ImTextureID 未設定時の既定) */
				id<MTLTexture> g_fontTexture = nil;

				/** PSO 生成に失敗したときのログ。毎フレーム出ると読めないので 1 度だけ */
				bool g_pipelineFailureLogged = false;


				id<MTLFunction> FunctionOf(IShader* shader)
				{
					return (shader != nullptr) ? static_cast<MetalShader*>(shader)->GetFunction() : nil;
				}


				/**
				 * ImDrawVert 用の MTLVertexDescriptor を自前で組む
				 *
				 * **MetalShader::GetVertexDescriptor() は使わない**。あれは隣の .spv の
				 * リフレクション由来で、ImGuiVK.fx が col を float4 と宣言している以上
				 * float4(16 バイト)を返してくる。しかし **バッファ上の col は RGBA8 unorm**
				 * (ImDrawVert の stride は 20 バイト)なので、そのまま使うと読み違える。
				 * VulkanImGui が VK_FORMAT_R8G8B8A8_UNORM を明示しているのと同じ理由。
				 * @return 生成した記述子(MRR: +1。呼び出し側が release する)
				 */
				MTLVertexDescriptor* CreateVertexDescriptor()
				{
					MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];  // MRR: +1

					vertexDesc.attributes[0].format      = MTLVertexFormatFloat2;
					vertexDesc.attributes[0].offset      = offsetof(ImDrawVert, pos);
					vertexDesc.attributes[0].bufferIndex = metal::VERTEX_BUFFER_INDEX;

					vertexDesc.attributes[1].format      = MTLVertexFormatFloat2;
					vertexDesc.attributes[1].offset      = offsetof(ImDrawVert, uv);
					vertexDesc.attributes[1].bufferIndex = metal::VERTEX_BUFFER_INDEX;

					// col は RGBA8 unorm。シェーダ側は float4 で受ける(Metal が正規化して渡す)。
					vertexDesc.attributes[2].format      = MTLVertexFormatUChar4Normalized;
					vertexDesc.attributes[2].offset      = offsetof(ImDrawVert, col);
					vertexDesc.attributes[2].bufferIndex = metal::VERTEX_BUFFER_INDEX;

					vertexDesc.layouts[metal::VERTEX_BUFFER_INDEX].stride       = sizeof(ImDrawVert);
					vertexDesc.layouts[metal::VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
					vertexDesc.layouts[metal::VERTEX_BUFFER_INDEX].stepRate     = 1;

					return vertexDesc;
				}


				/**
				 * 宛先フォーマットに合う PSO を用意する
				 *
				 * 起動後の初回 Render で 1 度だけ通るのが正常系。ここが毎フレーム走るようなら
				 * drawable のフォーマットが揺れている。
				 * @param colorFormat アタッチメントのピクセルフォーマット
				 * @return 使える状態になったら true
				 */
				bool EnsurePipeline(const MTLPixelFormat colorFormat)
				{
					if (g_dev == nullptr || colorFormat == MTLPixelFormatInvalid) {
						return false;
					}
					if (g_pipeline != nil && g_pipelineFormat == colorFormat) {
						return true;
					}

					id<MTLDevice>   device         = static_cast<id<MTLDevice>>(g_dev->GetMTLDeviceHandle());
					id<MTLFunction> vertexFunction = FunctionOf(g_vs.get());
					id<MTLFunction> fragmentFunc   = FunctionOf(g_ps.get());
					if (device == nil || vertexFunction == nil || fragmentFunc == nil) {
						return false;
					}

					@autoreleasepool
					{
						MTLVertexDescriptor*         vertexDesc   = CreateVertexDescriptor();
						MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
						pipelineDesc.vertexFunction                  = vertexFunction;
						pipelineDesc.fragmentFunction                = fragmentFunc;
						pipelineDesc.vertexDescriptor                = vertexDesc;
						pipelineDesc.colorAttachments[0].pixelFormat = colorFormat;
						// ImGui は通常のアルファ合成。VulkanImGui のブレンド設定と同じ値になる。
						metal::ApplyBlendMode(pipelineDesc.colorAttachments[0], BlendMode::AlphaBlend);
						// 深度アタッチメントの無いパスへ描くので深度フォーマットは付けない。

						NSError* error = nil;
						id<MTLRenderPipelineState> pipeline =
							[device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];  // MRR: +1

						[pipelineDesc release];
						[vertexDesc release];

						if (pipeline == nil)
						{
							if (!g_pipelineFailureLogged)
							{
								g_pipelineFailureLogged = true;
								char msg[512];
								std::snprintf(msg, sizeof(msg), "[MetalImGui] PSO の生成に失敗しました: %s",
								              (error != nil) ? [[error localizedDescription] UTF8String] : "unknown");
								aq::StartupLog(msg);
							}
							return false;
						}

						[g_pipeline release];
						g_pipeline       = pipeline;
						g_pipelineFormat = colorFormat;
					}
					return true;
				}


				bool CreateFontTexture()
				{
					ImGuiIO& io = ImGui::GetIO();
					unsigned char* pixels = nullptr; int w = 0, h = 0;
					io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
					if (!pixels || w <= 0 || h <= 0) return false;

					Texture2DDesc td; td.width = (uint32_t)w; td.height = (uint32_t)h; td.format = PixelFormat::R8G8B8A8_Unorm;
					// **変数名を id にしないこと**。Objective-C++ では id が型名なので、
					// 以降の id<MTLTexture> が解釈できなくなる(VulkanImGui.cpp との差はここだけ)。
					ImageData imageData; imageData.pixels = pixels; imageData.rowPitch = (uint32_t)w * 4; imageData.slicePitch = imageData.rowPitch * (uint32_t)h;
					g_font = g_dev->CreateTexture2D(td, imageData);
					if (!g_font) return false;

					// Metal の GetNativeHandle() は id<MTLTexture> をそのまま返す規約(設計書 §5.1)。
					g_fontTexture = static_cast<id<MTLTexture>>(g_font->GetNativeHandle());
					io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_fontTexture);
					return g_fontTexture != nil;
				}


				bool EnsureBuffers(uint32_t vtxCount, uint32_t idxCount)
				{
					if (vtxCount > g_vbCap)
					{
						g_vbCap = vtxCount + 5000;
						g_vb = g_dev->CreateDynamicVertexBuffer(g_vbCap, (uint32_t)sizeof(ImDrawVert), nullptr);
						if (!g_vb || static_cast<MetalVertexBuffer*>(g_vb.get())->GetBuffer() == nil) { g_vbCap = 0; return false; }
					}
					if (idxCount > g_ibCap)
					{
						g_ibCap = idxCount + 10000;
						g_ib = g_dev->CreateDynamicIndexBuffer(g_ibCap, sizeof(ImDrawIdx) == 2 ? IndexFormat::UInt16 : IndexFormat::UInt32, nullptr);
						if (!g_ib || static_cast<MetalIndexBuffer*>(g_ib.get())->GetBuffer() == nil) { g_ibCap = 0; return false; }
					}
					return true;
				}
			}


			bool Init()
			{
				g_dev = MetalGraphicsDeviceImpl::GetInstance();
				if (!g_dev) return false;

				ImGuiIO& io = ImGui::GetIO();
				io.BackendRendererName = "imgui_impl_aq_metal";
				io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

				// **ImGuiVK.fx をそのまま使う**(P0.5 で msl/ImGuiVK.*.metal が生成済み)。
				// Metal 用に .fx を増やすと shader_entries.txt が Vulkan / D3D 側にも波及する。
				g_vs = g_dev->CreateShader("Assets/Shader/ImGuiVK.fx", "VSMain", IShader::ShaderType::VS);
				g_ps = g_dev->CreateShader("Assets/Shader/ImGuiVK.fx", "PSMain", IShader::ShaderType::PS);
				if (FunctionOf(g_vs.get()) == nil || FunctionOf(g_ps.get()) == nil)
				{
					EngineAssertMsg(false, "ImGui Metal シェーダ読み込み失敗");
					return false;
				}

				SamplerDesc sd; sd.filter = FilterMode::MinMagMipLinear;
				sd.addressU = sd.addressV = sd.addressW = AddressMode::Wrap;
				g_sampler = g_dev->CreateSamplerState(sd);

				float initProj[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				g_projCB = g_dev->CreateConstantBuffer(initProj, sizeof(initProj));

				if (!g_sampler || static_cast<MetalSampler*>(g_sampler.get())->GetSampler() == nil)     return false;
				if (!g_projCB  || static_cast<MetalConstantBuffer*>(g_projCB.get())->GetBuffer() == nil) return false;
				if (!CreateFontTexture()) { EngineAssertMsg(false, "ImGui Metal フォント生成失敗"); return false; }

				// PSO は drawable のフォーマットが判る初回 Render で作る(EnsurePipeline)。
				return true;
			}


			void Shutdown()
			{
				[g_pipeline release];
				g_pipeline       = nil;
				g_pipelineFormat = MTLPixelFormatInvalid;

				g_vb.reset(); g_ib.reset(); g_projCB.reset(); g_sampler.reset(); g_font.reset();
				g_vs.reset(); g_ps.reset();
				g_vbCap = g_ibCap = 0; g_fontTexture = nil; g_dev = nullptr;
				g_pipelineFailureLogged = false;
			}


			void NewFrame() {}


			void Render(id<MTLRenderCommandEncoder> encoder,
			            const MTLPixelFormat        colorFormat,
			            const uint32_t              targetWidth,
			            const uint32_t              targetHeight,
			            ImDrawData*                 drawData)
			{
				if (!g_dev || encoder == nil || !drawData || drawData->TotalVtxCount <= 0) return;
				if (targetWidth == 0 || targetHeight == 0) return;
				if (!EnsurePipeline(colorFormat)) return;
				if (!EnsureBuffers((uint32_t)drawData->TotalVtxCount, (uint32_t)drawData->TotalIdxCount)) return;

				// imgui 座標系の原点と大きさ。scissor もこの原点を引いてから使う。
				const float L = drawData->DisplayPos.x,  T = drawData->DisplayPos.y;
				const float W = drawData->DisplaySize.x, H = drawData->DisplaySize.y;
				if (W <= 0 || H <= 0) return;

				// 毎フレーム経路なので autoreleasepool で包む(設計書 §10)。
				@autoreleasepool
				{
					// 全 draw list の頂点/インデックスを連結してアップロード。
					// ユニファイドメモリなので MTLStorageModeShared へ直接書けばよく、ステージングは要らない。
					std::vector<ImDrawVert> verts; verts.reserve(drawData->TotalVtxCount);
					std::vector<ImDrawIdx>  idxs;  idxs.reserve(drawData->TotalIdxCount);
					for (int n = 0; n < drawData->CmdListsCount; ++n)
					{
						const ImDrawList* dl = drawData->CmdLists[n];
						verts.insert(verts.end(), dl->VtxBuffer.Data, dl->VtxBuffer.Data + dl->VtxBuffer.Size);
						idxs.insert(idxs.end(),  dl->IdxBuffer.Data, dl->IdxBuffer.Data + dl->IdxBuffer.Size);
					}
					auto* vb = static_cast<MetalVertexBuffer*>(g_vb.get());
					auto* ib = static_cast<MetalIndexBuffer*>(g_ib.get());
					vb->Update(verts.data(), (uint32_t)(verts.size() * sizeof(ImDrawVert)));
					ib->Update(idxs.data(),  (uint32_t)(idxs.size()  * sizeof(ImDrawIdx)));

					// scale/translate (imgui 座標 → Metal のクリップ空間)。
					//
					// **Y は反転する**。ImGuiVK.fx は Vulkan NDC (y = -1 が画面の上端) 向けに
					// 書かれているが、**Metal のクリップ空間は D3D と同じで y = +1 が上端**
					// (設計書 §2.4)。定数だけで吸収できるので .fx は無改変で共有する。
					//   x: imgui x = L        → -1、 x = L + W → +1
					//   y: imgui y = T (上端) → +1、 y = T + H → -1
					float proj[4] = { 2.0f / W, -2.0f / H, -1.0f - L * (2.0f / W), 1.0f + T * (2.0f / H) };
					auto* projCB = static_cast<MetalConstantBuffer*>(g_projCB.get());
					projCB->Update(proj);

					[encoder setRenderPipelineState:g_pipeline];
					// ImGui のインデックスは表裏を揃えていないのでカリングしない(VulkanImGui と同じ)。
					[encoder setCullMode:MTLCullModeNone];

					// ビューポートは DisplaySize 全面。アタッチメントよりはみ出すと Metal が弾くのでクランプする。
					const double clampW = (W < (float)targetWidth)  ? (double)W : (double)targetWidth;
					const double clampH = (H < (float)targetHeight) ? (double)H : (double)targetHeight;
					MTLViewport viewport = { 0.0, 0.0, clampW, clampH, 0.0, 1.0 };
					[encoder setViewport:viewport];

					// b0 = ProjCB (b はシフト 0)、s0 = サンプラ (s もシフト 0)。設計書 §5.1。
					[encoder setVertexBuffer:vb->GetBuffer()
					                  offset:vb->GetCurrentOffset()
					                 atIndex:metal::VERTEX_BUFFER_INDEX];
					[encoder setVertexBuffer:projCB->GetBuffer()
					                  offset:projCB->GetCurrentOffset()
					                 atIndex:0];
					[encoder setFragmentSamplerState:static_cast<MetalSampler*>(g_sampler.get())->GetSampler()
					                         atIndex:0];

					uint32_t globalVtx = 0, globalIdx = 0;
					for (int n = 0; n < drawData->CmdListsCount; ++n)
					{
						const ImDrawList* dl = drawData->CmdLists[n];
						for (int c = 0; c < dl->CmdBuffer.Size; ++c)
						{
							const ImDrawCmd& dc = dl->CmdBuffer[c];
							if (dc.UserCallback) { continue; }

							// クリップ矩形 → scissor。DisplayPos を引いてから**描画先の範囲へクランプ**する
							// (MTLScissorRect は符号無しで、はみ出すと Metal が弾く)。
							ImVec2 cmin(dc.ClipRect.x - L, dc.ClipRect.y - T);
							ImVec2 cmax(dc.ClipRect.z - L, dc.ClipRect.w - T);
							if (cmin.x < 0) cmin.x = 0; if (cmin.y < 0) cmin.y = 0;
							if (cmax.x > (float)clampW) cmax.x = (float)clampW;
							if (cmax.y > (float)clampH) cmax.y = (float)clampH;
							if (cmax.x <= cmin.x || cmax.y <= cmin.y) continue;
							MTLScissorRect scissor = { (NSUInteger)cmin.x, (NSUInteger)cmin.y,
							                           (NSUInteger)(cmax.x - cmin.x), (NSUInteger)(cmax.y - cmin.y) };
							[encoder setScissorRect:scissor];

							// t0 = フォント / ユーザーテクスチャ (t はシフト 8)。
							id<MTLTexture> texture = (id<MTLTexture>)(uintptr_t)dc.GetTexID();
							if (texture == nil) texture = g_fontTexture;
							if (texture == nil) continue;  // nil を束ねると Validation がエラーにする
							[encoder setFragmentTexture:texture atIndex:metal::SRV_INDEX_SHIFT];

							// indexBufferOffset は**バイト単位**。動的 IB のスライス offset に
							// 開始インデックスのバイト数を足す(片方を忘れるとインデックスがずれる)。
							const NSUInteger indexOffset = (NSUInteger)ib->GetCurrentOffset()
							                             + (NSUInteger)(globalIdx + dc.IdxOffset) * ib->GetIndexStride();
							[encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
							                    indexCount:dc.ElemCount
							                     indexType:ib->GetIndexType()
							                   indexBuffer:ib->GetBuffer()
							             indexBufferOffset:indexOffset
							                 instanceCount:1
							                    baseVertex:(NSInteger)(globalVtx + dc.VtxOffset)
							                  baseInstance:0];
						}
						globalVtx += dl->VtxBuffer.Size;
						globalIdx += dl->IdxBuffer.Size;
					}
				}
			}
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL && AQ_IMGUI
