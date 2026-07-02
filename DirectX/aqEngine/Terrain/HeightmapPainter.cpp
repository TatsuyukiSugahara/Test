#include "aq.h"
#include "HeightmapPainter.h"
#ifdef AQ_DEBUG_IMGUI

#include "HeightmapChunk.h"
#include "TerrainPaintTool.h"
#include "Graphics/Camera.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"

#include <imgui/imgui.h>
#include <cmath>

#include <DirectXTex/DirectXTex.h>

namespace aq
{
	namespace terrain
	{
		// -----------------------------------------------------------------------
		// ライフタイム
		// -----------------------------------------------------------------------

		HeightmapPainter::~HeightmapPainter()
		{
			ReleasePreviewTexture();
		}

		void HeightmapPainter::Attach(HeightmapChunk* chunk, math::Vector3 worldOffset,
		                              math::Vector3* offsetPtr)
		{
			Detach();
			chunk_       = chunk;
			worldOffset_ = worldOffset;
			offsetPtr_   = offsetPtr;

			if (chunk_)
			{
				const uint32_t w = chunk_->GetMapWidth();
				const uint32_t h = chunk_->GetMapHeight();
				CreatePreviewTexture(w, h);
				smoothTmp_.resize(static_cast<size_t>(w) * h, 0.0f);
				UpdatePreviewTexture();
			}
		}

		void HeightmapPainter::Detach()
		{
			ReleasePreviewTexture();
			undoStack_.clear();
			smoothTmp_.clear();
			chunk_        = nullptr;
			offsetPtr_    = nullptr;
			dirty_        = false;
			hitLastFrame_ = false;
		}

		// -----------------------------------------------------------------------
		// プレビューテクスチャ (API 非依存)
		// -----------------------------------------------------------------------

		void HeightmapPainter::CreatePreviewTexture(uint32_t w, uint32_t h)
		{
			ReleasePreviewTexture();
			previewW_     = w;
			previewH_     = h;
			previewPixels_.assign(static_cast<size_t>(w) * h * 4, 0u);

			graphics::Texture2DDesc desc;
			desc.width     = w;
			desc.height    = h;
			desc.mipLevels = 1;
			desc.format    = graphics::PixelFormat::R8G8B8A8_Unorm;

			graphics::ImageData imgData;
			imgData.pixels     = previewPixels_.data();
			imgData.rowPitch   = w * 4;
			imgData.slicePitch = w * h * 4;

			previewSrv_ = graphics::GraphicsDevice::Get().CreateTexture2D(desc, imgData);
		}

		void HeightmapPainter::ReleasePreviewTexture()
		{
			previewSrv_.reset();
			previewPixels_.clear();
			previewW_ = 0;
			previewH_ = 0;
		}

		void HeightmapPainter::UpdatePreviewTexture()
		{
			if (!chunk_ || previewW_ == 0) return;

			const float*   heights = chunk_->GetHeightDataMutable();
			const uint32_t w       = previewW_;
			const uint32_t h       = previewH_;

			for (uint32_t y = 0; y < h; ++y)
			{
				for (uint32_t x = 0; x < w; ++x)
				{
					const uint8_t v = static_cast<uint8_t>(
						std::clamp(heights[y * w + x], 0.0f, 1.0f) * 255.0f + 0.5f);
					const size_t idx    = (y * w + x) * 4;
					previewPixels_[idx + 0] = v;
					previewPixels_[idx + 1] = v;
					previewPixels_[idx + 2] = v;
					previewPixels_[idx + 3] = 255u;
				}
			}

			// dirty 時はテクスチャを再生成して SRV を差し替える
			graphics::Texture2DDesc desc;
			desc.width     = w;
			desc.height    = h;
			desc.mipLevels = 1;
			desc.format    = graphics::PixelFormat::R8G8B8A8_Unorm;

			graphics::ImageData imgData;
			imgData.pixels     = previewPixels_.data();
			imgData.rowPitch   = w * 4;
			imgData.slicePitch = static_cast<uint32_t>(previewPixels_.size());

			previewSrv_ = graphics::GraphicsDevice::Get().CreateTexture2D(desc, imgData);
		}

		// -----------------------------------------------------------------------
		// アンドゥ
		// -----------------------------------------------------------------------

		void HeightmapPainter::PushUndo()
		{
			if (!chunk_) return;
			const float*  src = chunk_->GetHeightDataMutable();
			const size_t  sz  = static_cast<size_t>(chunk_->GetMapWidth()) * chunk_->GetMapHeight();

			if (static_cast<int>(undoStack_.size()) >= kMaxUndo)
				undoStack_.erase(undoStack_.begin());

			undoStack_.emplace_back(src, src + sz);
		}

		void HeightmapPainter::PopUndo()
		{
			if (undoStack_.empty() || !chunk_) return;
			float*       dst = chunk_->GetHeightDataMutable();
			const size_t sz  = static_cast<size_t>(chunk_->GetMapWidth()) * chunk_->GetMapHeight();
			std::copy(undoStack_.back().begin(), undoStack_.back().end(), dst);
			undoStack_.pop_back();
			dirty_ = true;
		}

		// -----------------------------------------------------------------------
		// ブラシ
		// -----------------------------------------------------------------------

		void HeightmapPainter::ApplyBrushAtUV(float centerU, float centerV, float dt)
		{
			if (!chunk_) return;

			float*         heights  = chunk_->GetHeightDataMutable();
			const uint32_t w        = chunk_->GetMapWidth();
			const uint32_t h        = chunk_->GetMapHeight();
			const float    terrSize = chunk_->GetTerrainSize();
			const float    radiusUV = brushRadius_ / terrSize;
			const float    r2       = radiusUV * radiusUV;
			const float    strength = brushStrength_ * dt * 60.0f;

			const int32_t uMin = std::max(0, static_cast<int32_t>((centerU - radiusUV) * (w - 1)));
			const int32_t uMax = std::min(static_cast<int32_t>(w - 1),
			                              static_cast<int32_t>((centerU + radiusUV) * (w - 1) + 1));
			const int32_t vMin = std::max(0, static_cast<int32_t>((centerV - radiusUV) * (h - 1)));
			const int32_t vMax = std::min(static_cast<int32_t>(h - 1),
			                              static_cast<int32_t>((centerV + radiusUV) * (h - 1) + 1));

			if (brushMode_ == BrushMode::Smooth)
			{
				const int32_t su = std::max(1, uMin);
				const int32_t eu = std::min(static_cast<int32_t>(w) - 2, uMax);
				const int32_t sv = std::max(1, vMin);
				const int32_t ev = std::min(static_cast<int32_t>(h) - 2, vMax);

				std::copy(heights, heights + static_cast<size_t>(w) * h, smoothTmp_.begin());

				for (int32_t vi = sv; vi <= ev; ++vi)
				{
					for (int32_t ui = su; ui <= eu; ++ui)
					{
						const float u  = static_cast<float>(ui) / (w - 1);
						const float v  = static_cast<float>(vi) / (h - 1);
						const float du = u - centerU;
						const float dv = v - centerV;
						const float d2 = (du * du + dv * dv) / (r2 + 1e-8f);
						if (d2 > 1.0f) continue;

						const float fall = (1.0f - d2) * (1.0f - d2);
						float avg = 0.0f;
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
								avg += heights[(vi + dy) * w + (ui + dx)];
						avg /= 9.0f;

						smoothTmp_[vi * w + ui] = heights[vi * w + ui]
							+ (avg - heights[vi * w + ui]) * std::clamp(fall * strength, 0.0f, 1.0f);
					}
				}
				for (int32_t vi = sv; vi <= ev; ++vi)
					for (int32_t ui = su; ui <= eu; ++ui)
						heights[vi * w + ui] = smoothTmp_[vi * w + ui];
				return;
			}

			for (int32_t vi = vMin; vi <= vMax; ++vi)
			{
				for (int32_t ui = uMin; ui <= uMax; ++ui)
				{
					const float u  = static_cast<float>(ui) / (w - 1);
					const float v  = static_cast<float>(vi) / (h - 1);
					const float du = u - centerU;
					const float dv = v - centerV;
					const float d2 = (du * du + dv * dv) / (r2 + 1e-8f);
					if (d2 > 1.0f) continue;

					const float fall = (1.0f - d2) * (1.0f - d2);
					float& val       = heights[vi * w + ui];

					switch (brushMode_)
					{
					case BrushMode::Raise:
						val = std::clamp(val + strength * fall, 0.0f, 1.0f);
						break;
					case BrushMode::Lower:
						val = std::clamp(val - strength * fall, 0.0f, 1.0f);
						break;
					case BrushMode::Flatten:
						val = val + (flattenTarget_ - val) * std::clamp(strength * fall, 0.0f, 1.0f);
						break;
					default:
						break;
					}
				}
			}
		}

		// -----------------------------------------------------------------------
		// レイキャスト (DirectX Math は API 非依存の数学ライブラリ)
		// -----------------------------------------------------------------------

		bool HeightmapPainter::ScreenToRay(float mx, float my, float sw, float sh,
		                                    const Camera& cam,
		                                    math::Vector3& outOrigin, math::Vector3& outDir) const
		{
			const float nx =  2.0f * mx / sw - 1.0f;
			const float ny = -(2.0f * my / sh - 1.0f);

			math::Matrix4x4 vpInv;
			vpInv.Inverse(cam.GetViewProjectionMatrix());
			const DirectX::XMMATRIX xmVP = vpInv;

			const DirectX::XMVECTOR nearNDC  = DirectX::XMVectorSet(nx, ny, 0.0f, 1.0f);
			const DirectX::XMVECTOR farNDC   = DirectX::XMVectorSet(nx, ny, 1.0f, 1.0f);
			const DirectX::XMVECTOR nearClip = DirectX::XMVector4Transform(nearNDC, xmVP);
			const DirectX::XMVECTOR farClip  = DirectX::XMVector4Transform(farNDC,  xmVP);

			const float wn = DirectX::XMVectorGetW(nearClip);
			const float wf = DirectX::XMVectorGetW(farClip);
			if (fabsf(wn) < 1e-7f || fabsf(wf) < 1e-7f) return false;

			DirectX::XMFLOAT4 np, fp;
			DirectX::XMStoreFloat4(&np, DirectX::XMVectorScale(nearClip, 1.0f / wn));
			DirectX::XMStoreFloat4(&fp, DirectX::XMVectorScale(farClip,  1.0f / wf));

			outOrigin.Set(np.x, np.y, np.z);

			math::Vector3 farPt(fp.x, fp.y, fp.z);
			outDir = farPt - outOrigin;
			return outDir.TryNormalize();
		}

		bool HeightmapPainter::RaycastHeightmap(const math::Vector3& origin, const math::Vector3& dir,
		                                          float& outU, float& outV, math::Vector3& outHit) const
		{
			if (!chunk_) return false;

			const float    terrSize = chunk_->GetTerrainSize();
			const uint32_t res      = chunk_->GetResolution();
			const float    stepSize = terrSize / static_cast<float>(res) * 0.5f;
			const int      maxSteps = static_cast<int>(res) * 4;

			math::Vector3 step = dir;
			step.Scale(stepSize);

			const float minX = worldOffset_.x;
			const float minZ = worldOffset_.z;
			const float maxX = worldOffset_.x + terrSize;
			const float maxZ = worldOffset_.z + terrSize;

			math::Vector3 pos     = origin;
			math::Vector3 prevPos = origin;

			for (int i = 0; i < maxSteps; ++i)
			{
				prevPos = pos;
				pos.Add(step);

				if (pos.x < minX || pos.x > maxX || pos.z < minZ || pos.z > maxZ) continue;

				const float terrH = chunk_->GetHeight(pos.x - worldOffset_.x,
				                                       pos.z - worldOffset_.z);
				if (pos.y <= terrH)
				{
					math::Vector3 lo = prevPos;
					math::Vector3 hi = pos;
					for (int b = 0; b < 8; ++b)
					{
						math::Vector3 mid = lo;
						mid.Add(hi);
						mid.Scale(0.5f);

						const float mh = chunk_->GetHeight(mid.x - worldOffset_.x,
						                                    mid.z - worldOffset_.z);
						if (mid.y <= mh) hi = mid;
						else             lo = mid;
					}

					math::Vector3 hit = lo;
					hit.Add(hi);
					hit.Scale(0.5f);

					outU   = std::clamp((hit.x - worldOffset_.x) / terrSize, 0.0f, 1.0f);
					outV   = std::clamp((hit.z - worldOffset_.z) / terrSize, 0.0f, 1.0f);
					outHit = hit;
					return true;
				}
			}
			return false;
		}

		// -----------------------------------------------------------------------
		// 3D ブラシ円オーバーレイ
		// -----------------------------------------------------------------------

		void HeightmapPainter::DrawBrushOverlay(const Camera& cam, float sw, float sh) const
		{
			if (!hitLastFrame_) return;

			const DirectX::XMMATRIX vp = cam.GetViewProjectionMatrix();

			auto projectToScreen = [&](const math::Vector3& world) -> ImVec2
			{
				const DirectX::XMVECTOR v    = DirectX::XMVectorSet(world.x, world.y, world.z, 1.0f);
				const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(v, vp);
				const float w = DirectX::XMVectorGetW(clip);
				if (fabsf(w) < 1e-7f) return ImVec2(-9999.f, -9999.f);
				const float nx =  DirectX::XMVectorGetX(clip) / w;
				const float ny = -DirectX::XMVectorGetY(clip) / w;
				return ImVec2((nx * 0.5f + 0.5f) * sw,
				              (ny * 0.5f + 0.5f) * sh);
			};

			const ImVec2 center = projectToScreen(lastHitWorld_);

			math::Vector3 edgePt = lastHitWorld_;
			edgePt.x += brushRadius_;
			const ImVec2 edge = projectToScreen(edgePt);

			const float dx      = edge.x - center.x;
			const float dy      = edge.y - center.y;
			const float screenR = std::max(2.0f, std::sqrt(dx * dx + dy * dy));

			ImGui::GetForegroundDrawList()->AddCircle(
				center, screenR, IM_COL32(255, 220, 0, 200), 32, 2.0f);
		}

		// -----------------------------------------------------------------------
		// PNG エクスポート
		// -----------------------------------------------------------------------

		void HeightmapPainter::ExportPNG(const char* path) const
		{
			if (!chunk_ || previewPixels_.empty()) return;

#if !defined(AQ_PLATFORM_UWP)
			DirectX::Image img  = {};
			img.width           = previewW_;
			img.height          = previewH_;
			img.format          = DXGI_FORMAT_R8G8B8A8_UNORM;
			img.rowPitch        = static_cast<size_t>(previewW_) * 4;
			img.slicePitch      = previewPixels_.size();
			img.pixels          = const_cast<uint8_t*>(previewPixels_.data());

			wchar_t wpath[512] = {};
			mbstowcs_s(nullptr, wpath, path, 511);
			DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE,
			                       DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wpath);
#else
			(void)path;   // UWP: DirectXTex 未リンク(保存無効)
#endif
		}

		// -----------------------------------------------------------------------
		// IDebugRenderable::DebugRender  (メインエントリ)
		// -----------------------------------------------------------------------

		void HeightmapPainter::DebugRenderMenu()
		{
			ImGui::MenuItem("Heightmap Painter", nullptr, &show_);
		}

		void HeightmapPainter::DebugRender()
		{
			if (!chunk_ || !show_) return;

			const ImGuiIO& io = ImGui::GetIO();
			const float    sw = io.DisplaySize.x;
			const float    sh = io.DisplaySize.y;
			const float    dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

			// ---- 3D ビュー直接ペイント ----
			hitLastFrame_ = false;
			if (enabled_ && paint3DEnabled_ && ActiveTerrainPaintTool() == TerrainPaintTool::Heightmap &&
			    !io.WantCaptureMouse && sw > 0.0f && sh > 0.0f)
			{
				Camera* cam = CameraManager::Get().GetCamera(CameraType::Main);
				if (cam)
				{
					math::Vector3 rayOrigin, rayDir;
					if (ScreenToRay(io.MousePos.x, io.MousePos.y, sw, sh,
					                *cam, rayOrigin, rayDir))
					{
						float hitU, hitV;
						math::Vector3 hitWorld;
						if (RaycastHeightmap(rayOrigin, rayDir, hitU, hitV, hitWorld))
						{
							lastHitWorld_ = hitWorld;
							hitLastFrame_ = true;

							if (io.MouseClicked[0])
								PushUndo();

							if (io.MouseDown[0])
							{
								ApplyBrushAtUV(hitU, hitV, dt);
								dirty_ = true;
							}
						}
					}
					DrawBrushOverlay(*cam, sw, sh);
				}
			}

			if (dirty_)
			{
				chunk_->RebuildFromHeights();
				UpdatePreviewTexture();
				dirty_ = false;
			}

			// ---- ImGui パネル ----
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
			if (windowLocked_) windowFlags |= ImGuiWindowFlags_NoMove;

			ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Heightmap Painter", &show_, windowFlags))
			{
				ImGui::End();
				return;
			}

			ImGui::Checkbox("Enabled", &enabled_);
			ImGui::SameLine();
			ImGui::Checkbox("Lock Window", &windowLocked_);
			ImGui::SameLine();
			if (ImGui::RadioButton("Active 3D Tool", ActiveTerrainPaintTool() == TerrainPaintTool::Heightmap))
				ActiveTerrainPaintTool() = TerrainPaintTool::Heightmap;
			ImGui::Separator();

			ImGui::BeginDisabled(!enabled_);

			// ---- World Settings ----
			if (ImGui::CollapsingHeader("World Settings"))
			{
				float terrainSize = chunk_->GetTerrainSize();
				if (ImGui::DragFloat("Terrain Size (m)", &terrainSize, 5.0f, 10.0f, 2000.0f, "%.0f m"))
				{
					terrainSize = std::clamp(terrainSize, 10.0f, 2000.0f);
					chunk_->SetTerrainSize(terrainSize);
					const float half = terrainSize * 0.5f;
					worldOffset_.Set(-half, 0.0f, -half);
					if (offsetPtr_) offsetPtr_->Set(-half, 0.0f, -half);
					UpdatePreviewTexture();
				}

				float heightScale = chunk_->GetHeightScale();
				if (ImGui::DragFloat("Height Scale (m)", &heightScale, 0.5f, 1.0f, 500.0f, "%.1f m"))
				{
					heightScale = std::clamp(heightScale, 1.0f, 500.0f);
					chunk_->SetHeightScale(heightScale);
					UpdatePreviewTexture();
				}
			}

			{
				static const char* kLabels[] = { "Raise", "Lower", "Smooth", "Flatten" };
				int modeIdx = static_cast<int>(brushMode_);
				for (int i = 0; i < 4; ++i)
				{
					if (i > 0) ImGui::SameLine();
					if (ImGui::RadioButton(kLabels[i], modeIdx == i))
						brushMode_ = static_cast<BrushMode>(i);
				}
			}

			ImGui::SliderFloat("Radius (m)",  &brushRadius_,   0.5f, 50.0f);
			ImGui::SliderFloat("Strength",    &brushStrength_,  0.01f, 2.0f);
			if (brushMode_ == BrushMode::Flatten)
			{
				float targetM = flattenTarget_ * chunk_->GetHeightScale();
				if (ImGui::SliderFloat("Target (m)", &targetM, 0.0f, chunk_->GetHeightScale()))
					flattenTarget_ = targetM / chunk_->GetHeightScale();
			}

			ImGui::Checkbox("3D Paint", &paint3DEnabled_);
			ImGui::SameLine();
			if (ImGui::Button("Undo") || (enabled_ && ActiveTerrainPaintTool() == TerrainPaintTool::Heightmap && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)))
				PopUndo();

			ImGui::Separator();

			// 2D プレビューキャンバス (256×256)
			if (previewSrv_)
			{
				// GetNativeHandle() は API 固有ポインタ (D3D11: ID3D11ShaderResourceView*)
				ImTextureID texID = reinterpret_cast<ImTextureID>(previewSrv_->GetNativeHandle());
				constexpr float kSize = 256.0f;
				const ImVec2    imageMin = ImGui::GetCursorScreenPos();
				ImGui::Image(texID, ImVec2(kSize, kSize));

				if (ImGui::IsItemHovered())
				{
					const ImVec2 mp = ImGui::GetMousePos();
					const float  u  = (mp.x - imageMin.x) / kSize;
					const float  v  = (mp.y - imageMin.y) / kSize;

					const float brushUVPx = (brushRadius_ / chunk_->GetTerrainSize()) * kSize;
					ImGui::GetWindowDrawList()->AddCircle(
						mp, brushUVPx, IM_COL32(255, 220, 0, 220), 32, 1.5f);

					if (enabled_)
					{
						if (ImGui::IsMouseClicked(0))
						{
							PushUndo();
							wasMouseDownOnCanvas_ = true;
						}
						if (io.MouseDown[0] && wasMouseDownOnCanvas_)
						{
							ApplyBrushAtUV(u, v, dt);
							dirty_ = true;
						}
					}
				}

				if (!io.MouseDown[0])
					wasMouseDownOnCanvas_ = false;
			}

			ImGui::Separator();
			ImGui::Text("Map: %ux%u  Scale: %.1fm  Size: %.1fm",
			            chunk_->GetMapWidth(), chunk_->GetMapHeight(),
			            chunk_->GetHeightScale(), chunk_->GetTerrainSize());
			ImGui::Separator();

			static char exportPath[256] = "Assets/Terrain/heightmap_export.png";
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##path", exportPath, sizeof(exportPath));
			ImGui::SameLine();
			if (ImGui::Button("Export PNG"))
				ExportPNG(exportPath);

			if (ImGui::Button("Reset Flat"))
			{
				PushUndo();
				float* h = chunk_->GetHeightDataMutable();
				std::fill(h, h + static_cast<size_t>(chunk_->GetMapWidth()) * chunk_->GetMapHeight(), 0.0f);
				dirty_ = true;
			}

			ImGui::EndDisabled();
			ImGui::End();
		}

	}
}
#endif // AQ_DEBUG_IMGUI
