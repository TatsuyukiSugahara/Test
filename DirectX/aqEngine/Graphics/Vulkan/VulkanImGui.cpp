#include "aq.h"
#if defined(ENGINE_GRAPHICS_VULKAN) && defined(AQ_IMGUI)
#include "Graphics/Vulkan/VulkanImGui.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aq
{
	namespace graphics
	{
		namespace VulkanImGui
		{
			namespace
			{
				/**
				 * バックエンド内部プールの SAMPLED_IMAGE ディスクリプタ数。
				 * フォントアトラス + (ユーザーテクスチャ × 在フライトのフレーム数) を見込む。
				 */
				static constexpr uint32_t DESCRIPTOR_POOL_SIZE = 128;

				/**
				 * 1 フレームで変換するユーザーテクスチャ (VkImageView) の上限。
				 * 超えた分は描かない。プールを枯らして vkAllocateDescriptorSets が落ちるより、
				 * 絵が欠けるほうを選ぶ。
				 */
				static constexpr uint32_t MAX_USER_TEXTURE_COUNT = 16;

				/**
				 * DescriptorSet を解放するまで待つフレーム数。
				 * フレーム N の提出が GPU を抜けたことは、フレーム N + FRAME_COUNT の
				 * BeginFrameIfNeeded のフェンス待ちで保証される (+1 は余裕)。
				 */
				static constexpr uint64_t RETIRE_DELAY_FRAME_COUNT = VulkanGraphicsDeviceImpl::GetFrameCount() + 1;

				/** 解放待ちの DescriptorSet */
				struct RetiredSet
				{
					uint64_t        frameSerial = 0;              // 最後に使ったフレーム通し番号
					VkDescriptorSet set         = VK_NULL_HANDLE;
				};

				VulkanGraphicsDeviceImpl* g_dev = nullptr;

				/** 公式バックエンドの状態 */
				bool     g_backendReady   = false;
				VkFormat g_initFormat     = VK_FORMAT_UNDEFINED;  // Init 時のスワップチェーンフォーマット
				uint32_t g_initImageCount = 0;                    // Init 時のスワップチェーン画像数

				/** 今フレームの VkImageView → VkDescriptorSet (同じビューの重複 Add を避ける) */
				std::unordered_map<VkImageView, VkDescriptorSet> g_frameSets;
				/** 解放待ち (GPU が抜けるまで RemoveTexture できない) */
				std::vector<RetiredSet> g_retiredSets;
				/** 自分が確保済みの DescriptorSet 全部。変換済みの ID を見分けるのに使う */
				std::unordered_set<VkDescriptorSet> g_ownedSets;

				/** テクスチャ数が上限に達した警告を 1 回だけ出すためのフラグ */
				bool g_warnedTooManyTextures = false;


				// VkDescriptorSet と ImTextureID の変換。非ディスパッチャブルハンドルは
				// 64bit 環境ではポインタ、32bit 環境では uint64_t なので、変換はここに閉じ込める。
#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES == 1
				inline ImTextureID     ToTexID(VkDescriptorSet set)    { return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(set)); }
				inline VkDescriptorSet ToDescriptorSet(ImTextureID id) { return reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(id)); }
#else
				inline ImTextureID     ToTexID(VkDescriptorSet set)    { return static_cast<ImTextureID>(set); }
				inline VkDescriptorSet ToDescriptorSet(ImTextureID id) { return static_cast<VkDescriptorSet>(id); }
#endif


				/** バックエンドの失敗をエンジンのログへ流す (CheckVkResultFn) */
				void CheckVkResult(VkResult result)
				{
					(void)VkCheckImpl(result, "imgui_impl_vulkan", __FILE__, __LINE__);
				}


				/** GPU を抜けた DescriptorSet を解放する */
				void ReleaseRetiredSets(const uint64_t frameSerial)
				{
					size_t kept = 0;
					for (size_t i = 0; i < g_retiredSets.size(); ++i)
					{
						const RetiredSet retired = g_retiredSets[i];
						if (frameSerial >= retired.frameSerial + RETIRE_DELAY_FRAME_COUNT) {
							ImGui_ImplVulkan_RemoveTexture(retired.set);
							g_ownedSets.erase(retired.set);
						} else {
							g_retiredSets[kept++] = retired;
						}
					}
					g_retiredSets.resize(kept);
				}


				/** 自前の DescriptorSet を全部返す。GPU が空の区間でだけ呼ぶ */
				void ReleaseAllSets()
				{
					for (const auto& entry : g_frameSets)
					{
						ImGui_ImplVulkan_RemoveTexture(entry.second);
					}
					for (const RetiredSet& retired : g_retiredSets)
					{
						ImGui_ImplVulkan_RemoveTexture(retired.set);
					}
					g_frameSets.clear();
					g_retiredSets.clear();
					g_ownedSets.clear();
				}


				/** 公式バックエンドを初期化する (設計書/エディタ全体設計.md 4.4 の決定) */
				bool InitBackend(const VkFormat colorFormat, const uint32_t imageCount)
				{
					// pColorAttachmentFormats はバックエンドが深いコピーを取るので
					// (imgui_impl_vulkan.cpp の ImGui_ImplVulkan_CreateMainPipeline)、
					// ローカル変数を指して渡してよい。
					VkFormat attachmentFormat = colorFormat;

					ImGui_ImplVulkan_InitInfo info = {};
					info.ApiVersion     = VK_API_VERSION_1_3;   // CreateInstance の apiVersion と揃える
					info.Instance       = g_dev->GetVkInstance();
					info.PhysicalDevice = g_dev->GetPhysicalDevice();
					info.Device         = g_dev->GetDevice();
					info.QueueFamily    = g_dev->GetGraphicsQueueFamily();
					info.Queue          = g_dev->GetGraphicsQueue();

					// エンジンのフレーム別プール (GetCurrentDescriptorPool) はフレーム頭で Reset するので
					// 流用できない (自分のリセットで ImGui 用のディスクリプタも一緒に消える)。
					// DescriptorPoolSize を渡してバックエンド内部のプールを作らせる。
					info.DescriptorPool     = VK_NULL_HANDLE;
					info.DescriptorPoolSize = DESCRIPTOR_POOL_SIZE;

					// MinImageCount は 2 以上、ImageCount は MinImageCount 以上がバックエンドの前提。
					// ImageCount は頂点/インデックスバッファのリング段数にしか使われないので、
					// 画像数が 1 の環境でも 2 に丸めて渡してよい。
					info.MinImageCount  = 2;
					info.ImageCount     = imageCount < 2 ? 2 : imageCount;
					info.PipelineCache  = VK_NULL_HANDLE;

					// dynamic rendering (VkRenderPass を作らない)。描画先はその都度呼び出し側が渡す。
					info.UseDynamicRendering           = true;
					info.PipelineInfoMain.MSAASamples  = VK_SAMPLE_COUNT_1_BIT;
					info.PipelineInfoMain.PipelineRenderingCreateInfo =
						VkPipelineRenderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
					info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
					info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &attachmentFormat;

					info.CheckVkResultFn = CheckVkResult;

					if (!ImGui_ImplVulkan_Init(&info))
					{
						EngineAssertMsg(false, "ImGui Vulkan バックエンドの初期化失敗");
						return false;
					}

					g_initFormat     = colorFormat;
					g_initImageCount = imageCount;
					g_backendReady   = true;
					return true;
				}


				/** 公式バックエンドを畳む。自前の DescriptorSet は内部プールごと消える前に返す */
				void ShutdownBackend()
				{
					if (!g_backendReady) return;

					// 在フライトのコマンドが DescriptorSet とパイプラインを参照している。
					if (g_dev) g_dev->WaitDeviceIdle();
					ReleaseAllSets();
					ImGui_ImplVulkan_Shutdown();

					g_backendReady   = false;
					g_initFormat     = VK_FORMAT_UNDEFINED;
					g_initImageCount = 0;
				}


				/**
				 * スワップチェーンの作り直しに追従する。
				 *
				 * 寸法だけが変わる場合は何もしない (dynamic rendering なので描画先はその都度渡す)。
				 * フォーマットか画像数が変わると ImGui のパイプラインとフレームリングが合わなくなるが、
				 * ImGui_ImplVulkan_SetMinImageCount は非対応 (内部で assert) なので、
				 * Shutdown から Init をやり直す (設計書 4.4)。Android のサーフェス再生成もここを通る。
				 */
				bool EnsureBackend()
				{
					if (!g_dev) return false;

					const VkFormat format     = g_dev->GetSwapchainFormat();
					const uint32_t imageCount = g_dev->GetSwapchainImageCount();
					if (format == VK_FORMAT_UNDEFINED || imageCount == 0)
					{
						return false;   // スワップチェーン未確立。描く相手がいないので初期化もしない
					}
					if (g_backendReady && format == g_initFormat && imageCount == g_initImageCount)
					{
						return true;
					}

					ShutdownBackend();
					return InitBackend(format, imageCount);
				}


				/** SRV の画像を SHADER_READ_ONLY_OPTIMAL へ遷移する。rendering scope の外でだけ呼ぶ */
				void TransitionToShaderRead(VkCommandBuffer cmd, VulkanSRV* srv, VkImageLayout* layout)
				{
					VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
					// 直前の用途 (カラー書き込み / compute の GENERAL 書き込み) は分からないので、
					// src は全コマンドの書き込みを待つ。dst は ImGui のサンプリングだけ。
					barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
					barrier.srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT;
					barrier.dstStageMask     = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
					barrier.dstAccessMask    = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
					barrier.oldLayout        = *layout;
					barrier.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					barrier.image            = srv->GetImage();
					// Hi-Z のようにミップを持つ画像もあるので全サブリソースを揃える (描画側と同方式)。
					barrier.subresourceRange = { srv->Aspect(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

					VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
					dep.imageMemoryBarrierCount = 1;
					dep.pImageMemoryBarriers    = &barrier;
					vkCmdPipelineBarrier2(cmd, &dep);

					*layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
			}


			bool Init()
			{
				g_dev = VulkanGraphicsDeviceImpl::GetInstance();
				if (!g_dev) return false;

				// フォント (io.Fonts) は呼び出し側が積む。公式バックエンドは RendererHasTextures を
				// 立てて動的アトラスにするので、ここでアトラスを作る処理は要らない。
				return EnsureBackend();
			}


			void Shutdown()
			{
				ShutdownBackend();
				g_dev = nullptr;
				g_warnedTooManyTextures = false;
			}


			void NewFrame()
			{
				// アトラスの更新は RenderDrawData が draw_data->Textures を見て自分で行う。
				if (g_backendReady) ImGui_ImplVulkan_NewFrame();
			}


			void PrepareTextures(ImDrawData* drawData)
			{
				if (!drawData || !EnsureBackend()) return;

				VkCommandBuffer cmd = g_dev->GetCommandBuffer();
				const uint64_t frameSerial = VulkanGraphicsDeviceImpl::GetStaticFrameSerial();

				// GPU を抜けた DescriptorSet を回収する。
				ReleaseRetiredSets(frameSerial);

				// 対応表はフレームごとに作り直す。前フレームの set を VkImageView で引き当てて
				// 使い回すと、RT を作り直したあとにハンドル値が再利用された場合、
				// 破棄済みのビューを指す DescriptorSet を掴んだことに気付けない。
				// TODO(E2a): device 側にビュー破棄のフックを入れて、設計書 4.4 のとおり
				//            「破棄時に RemoveTexture してキャッシュから外す」持続キャッシュへ替える。
				for (const auto& entry : g_frameSets)
				{
					g_retiredSets.push_back(RetiredSet{ frameSerial, entry.second });
				}
				g_frameSets.clear();

				uint32_t viewCount = 0;
				for (ImDrawList* drawList : drawData->CmdLists)
				{
					if (!drawList) continue;

					for (int c = 0; c < drawList->CmdBuffer.Size; ++c)
					{
						ImDrawCmd& drawCmd = drawList->CmdBuffer[c];
						if (drawCmd.UserCallback != nullptr)    continue;
						// _TexData を持つものはバックエンドが管理しているテクスチャ (フォントアトラス等)。
						if (drawCmd.TexRef._TexData != nullptr) continue;

						const ImTextureID texId = drawCmd.GetTexID();
						// 同じ ImDrawData を 2 回描く経路に備えた保険。自分が配った DescriptorSet は
						// 解放まで生きているので、生のエンジンハンドルと値が衝突することはない。
						if (g_ownedSets.find(ToDescriptorSet(texId)) != g_ownedSets.end()) continue;

						// ImGui::Image にはエンジンのハンドル (IShaderResourceView::GetNativeHandle) が
						// 渡ってくる。Vulkan ではその実体が VulkanSRV。
						// TODO(E1a): 配列テクスチャ (シャドウ深度マップ) は公式シェーダが読めない。
						//            VulkanSRV にビュー種別が無いので、ここでは検出できていない。
						VulkanSRV*  srv  = reinterpret_cast<VulkanSRV*>(static_cast<uintptr_t>(texId));
						VkImageView view = srv ? srv->GetImageView() : VK_NULL_HANDLE;
						if (view == VK_NULL_HANDLE)
						{
							drawCmd.ElemCount = 0;   // 変換できないものは描かせない (不正な ID を渡さない)
							continue;
						}

						// レイアウト遷移は dynamic rendering の scope 内では打てないので、ここで済ませる。
						// 常時 SHADER_READ_ONLY の通常テクスチャは LayoutPtr()=nullptr で遷移不要。
						VkImageLayout* layout = srv->LayoutPtr();
						if (layout != nullptr && *layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
							&& srv->GetImage() != VK_NULL_HANDLE)
						{
							TransitionToShaderRead(cmd, srv, layout);
						}

						auto it = g_frameSets.find(view);
						if (it == g_frameSets.end())
						{
							if (viewCount >= MAX_USER_TEXTURE_COUNT)
							{
								if (!g_warnedTooManyTextures)
								{
									g_warnedTooManyTextures = true;
									aq::debug::OutputString("[Vulkan] ImGui: 1 フレームのユーザーテクスチャが上限を超えたので描画を省きました\n");
								}
								drawCmd.ElemCount = 0;
								continue;
							}

							// AddTexture はレイアウト遷移をしない (遷移は上で済ませている)。
							VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
							if (set == VK_NULL_HANDLE)
							{
								drawCmd.ElemCount = 0;
								continue;
							}
							it = g_frameSets.emplace(view, set).first;
							g_ownedSets.insert(set);
							++viewCount;
						}

						drawCmd.TexRef = ImTextureRef(ToTexID(it->second));
					}
				}
			}


			void Render(VkCommandBuffer cmd, ImDrawData* drawData)
			{
				if (!g_backendReady || !drawData) return;
				ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
			}
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN && AQ_IMGUI
