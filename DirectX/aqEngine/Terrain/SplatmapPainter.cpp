#include "aq.h"
#include "SplatmapPainter.h"
#ifdef AQ_DEBUG_IMGUI

#include "HeightmapChunk.h"
#include "TerrainPaintTool.h"
#include "Graphics/Camera.h"
#include "Graphics/GraphicsTypes.h"

#include <imgui/imgui.h>
#include <DirectXTex/DirectXTex.h>
#include <cmath>

namespace aq
{
	namespace terrain
	{
		namespace
		{
			constexpr uint32_t kLayerCount = 3;
			const char* kLayerLabels[kLayerCount] = { "Grass", "Snow", "Rock" };
			const ImU32 kLayerColors[kLayerCount] = {
				IM_COL32(70, 210, 70, 230),
				IM_COL32(220, 240, 255, 230),
				IM_COL32(140, 140, 140, 230),
			};

			float SmoothStep(float edge0, float edge1, float x)
			{
				if (fabsf(edge1 - edge0) < 0.0001f)
					return x >= edge1 ? 1.0f : 0.0f;
				float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
				return t * t * (3.0f - 2.0f * t);
			}

			float HashNoise(int x, int y)
			{
				uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
				h = (h ^ (h >> 13)) * 1274126177u;
				h ^= h >> 16;
				return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
			}

			void NormalizeWeight(math::Vector4& w)
			{
				w.x = std::clamp(w.x, 0.0f, 1.0f);
				w.y = std::clamp(w.y, 0.0f, 1.0f);
				w.z = std::clamp(w.z, 0.0f, 1.0f);
				const float total = w.x + w.y + w.z;
				if (total <= 0.0001f)
				{
					w.Set(1.0f, 0.0f, 0.0f, 1.0f);
					return;
				}
				const float inv = 1.0f / total;
				w.x *= inv;
				w.y *= inv;
				w.z *= inv;
				w.w = 1.0f;
			}

			float SampleHeightNormalized(const HeightmapChunk& chunk, float u, float v)
			{
				const float* heights = chunk.GetHeightData();
				const uint32_t w = chunk.GetMapWidth();
				const uint32_t h = chunk.GetMapHeight();
				if (!heights || w == 0 || h == 0) return 0.0f;

				u = std::clamp(u, 0.0f, 1.0f);
				v = std::clamp(v, 0.0f, 1.0f);
				const float hx = u * (w - 1);
				const float hz = v * (h - 1);
				const int32_t x0 = std::clamp(static_cast<int32_t>(hx), 0, static_cast<int32_t>(w) - 1);
				const int32_t z0 = std::clamp(static_cast<int32_t>(hz), 0, static_cast<int32_t>(h) - 1);
				const int32_t x1 = std::min(x0 + 1, static_cast<int32_t>(w) - 1);
				const int32_t z1 = std::min(z0 + 1, static_cast<int32_t>(h) - 1);
				const float fx = hx - x0;
				const float fz = hz - z0;
				const float h00 = heights[static_cast<size_t>(z0) * w + x0];
				const float h10 = heights[static_cast<size_t>(z0) * w + x1];
				const float h01 = heights[static_cast<size_t>(z1) * w + x0];
				const float h11 = heights[static_cast<size_t>(z1) * w + x1];
				return (h00 + (h10 - h00) * fx) * (1.0f - fz)
				     + (h01 + (h11 - h01) * fx) * fz;
			}

			float EstimateSlope(const HeightmapChunk& chunk, float u, float v)
			{
				const float du = 1.0f / std::max(1u, chunk.GetMapWidth() - 1);
				const float dv = 1.0f / std::max(1u, chunk.GetMapHeight() - 1);
				const float hL = SampleHeightNormalized(chunk, u - du, v) * chunk.GetHeightScale();
				const float hR = SampleHeightNormalized(chunk, u + du, v) * chunk.GetHeightScale();
				const float hU = SampleHeightNormalized(chunk, u, v - dv) * chunk.GetHeightScale();
				const float hD = SampleHeightNormalized(chunk, u, v + dv) * chunk.GetHeightScale();
				const float dx = std::max(0.001f, du * 2.0f * chunk.GetTerrainSize());
				const float dz = std::max(0.001f, dv * 2.0f * chunk.GetTerrainSize());
				const float dydx = (hR - hL) / dx;
				const float dydz = (hD - hU) / dz;
				const float normalY = 1.0f / std::sqrt(dydx * dydx + dydz * dydz + 1.0f);
				return std::clamp(1.0f - normalY, 0.0f, 1.0f);
			}
		}

		void SplatmapPainter::Attach(HeightmapChunk* chunk, math::Vector3 worldOffset)
		{
			Detach();
			chunk_ = chunk;
			worldOffset_ = worldOffset;
			if (chunk_)
			{
				smoothTmp_.resize(static_cast<size_t>(chunk_->GetSplatMapWidth()) * chunk_->GetSplatMapHeight());
			}
		}

		void SplatmapPainter::Detach()
		{
			undoStack_.clear();
			smoothTmp_.clear();
			chunk_ = nullptr;
			dirty_ = false;
			hitLastFrame_ = false;
			wasMouseDownOnCanvas_ = false;
		}

		void SplatmapPainter::PushUndo()
		{
			if (!chunk_) return;
			const math::Vector4* src = chunk_->GetSplatData();
			const size_t sz = static_cast<size_t>(chunk_->GetSplatMapWidth()) * chunk_->GetSplatMapHeight();
			if (!src || sz == 0) return;

			if (static_cast<int>(undoStack_.size()) >= kMaxUndo)
				undoStack_.erase(undoStack_.begin());
			undoStack_.emplace_back(src, src + sz);
		}

		void SplatmapPainter::PopUndo()
		{
			if (!chunk_ || undoStack_.empty()) return;
			math::Vector4* dst = chunk_->GetSplatDataMutable();
			const size_t sz = static_cast<size_t>(chunk_->GetSplatMapWidth()) * chunk_->GetSplatMapHeight();
			if (!dst || undoStack_.back().size() != sz) return;
			std::copy(undoStack_.back().begin(), undoStack_.back().end(), dst);
			undoStack_.pop_back();
			dirty_ = true;
		}

		void SplatmapPainter::ApplyBrushAtUV(float centerU, float centerV, float dt)
		{
			if (!chunk_) return;
			math::Vector4* splats = chunk_->GetSplatDataMutable();
			const uint32_t w = chunk_->GetSplatMapWidth();
			const uint32_t h = chunk_->GetSplatMapHeight();
			if (!splats || w == 0 || h == 0) return;

			// ヒッチ時に一気に塗れないよう dt を上限クランプ
			const float safeDt = std::min(dt, 1.0f / 15.0f);

			const float radiusUV = brushRadius_ / chunk_->GetTerrainSize();
			const float r2       = radiusUV * radiusUV;
			// flow = 1秒間に蓄積される量。dt * 60 係数は廃止
			const float flow     = std::clamp(brushStrength_ * safeDt, 0.0f, 1.0f);

			const int32_t uMin = std::max(0, static_cast<int32_t>((centerU - radiusUV) * (w - 1)));
			const int32_t uMax = std::min(static_cast<int32_t>(w - 1), static_cast<int32_t>((centerU + radiusUV) * (w - 1) + 1));
			const int32_t vMin = std::max(0, static_cast<int32_t>((centerV - radiusUV) * (h - 1)));
			const int32_t vMax = std::min(static_cast<int32_t>(h - 1), static_cast<int32_t>((centerV + radiusUV) * (h - 1) + 1));

			// コサイン falloff: cos(√d² * π/2)²  エッジが自然に 0 へ収束する
			auto CosineFalloff = [](float d2) -> float
			{
				const float c = std::cos(std::sqrt(d2) * (3.14159265f * 0.5f));
				return c * c;
			};

			if (brushMode_ == BrushMode::Smooth)
			{
				if (smoothTmp_.size() != static_cast<size_t>(w) * h)
					smoothTmp_.resize(static_cast<size_t>(w) * h);
				std::copy(splats, splats + static_cast<size_t>(w) * h, smoothTmp_.begin());

				const int32_t su = std::max(1, uMin);
				const int32_t eu = std::min(static_cast<int32_t>(w) - 2, uMax);
				const int32_t sv = std::max(1, vMin);
				const int32_t ev = std::min(static_cast<int32_t>(h) - 2, vMax);
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

						math::Vector4 avg(0.0f);
						for (int dy = -1; dy <= 1; ++dy)
							for (int dx = -1; dx <= 1; ++dx)
							{
								avg.x += splats[static_cast<size_t>(vi + dy) * w + (ui + dx)].x;
								avg.y += splats[static_cast<size_t>(vi + dy) * w + (ui + dx)].y;
								avg.z += splats[static_cast<size_t>(vi + dy) * w + (ui + dx)].z;
							}
						avg.x /= 9.0f;
						avg.y /= 9.0f;
						avg.z /= 9.0f;

						const float amount = std::clamp(CosineFalloff(d2) * flow, 0.0f, 1.0f);
						math::Vector4& dst  = smoothTmp_[static_cast<size_t>(vi) * w + ui];
						dst.x += (avg.x - dst.x) * amount;
						dst.y += (avg.y - dst.y) * amount;
						dst.z += (avg.z - dst.z) * amount;
						NormalizeWeight(dst);
					}
				}

				for (int32_t vi = sv; vi <= ev; ++vi)
					for (int32_t ui = su; ui <= eu; ++ui)
						splats[static_cast<size_t>(vi) * w + ui] = smoothTmp_[static_cast<size_t>(vi) * w + ui];
				return;
			}

			const uint32_t layer = std::min(activeLayer_, 2u);
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

					const float delta = CosineFalloff(d2) * flow;
					math::Vector4& val = splats[static_cast<size_t>(vi) * w + ui];

					if (brushMode_ == BrushMode::Erase)
					{
						// active layer を固定量だけ削り、残量を他レイヤーへ配分
						val.a[layer] = std::max(val.a[layer] - delta, 0.0f);
						const float remain   = 1.0f - val.a[layer];
						float       otherSum = 0.0f;
						for (uint32_t i = 0; i < 3; ++i)
							if (i != layer) otherSum += val.a[i];

						if (otherSum > 1e-4f)
						{
							// 他レイヤーを比例スケールして残量を埋める
							const float scale = remain / otherSum;
							for (uint32_t i = 0; i < 3; ++i)
								if (i != layer) val.a[i] *= scale;
						}
						else
						{
							// 他レイヤーが全滅している場合は均等配分
							const float share = remain / 2.0f;
							for (uint32_t i = 0; i < 3; ++i)
								if (i != layer) val.a[i] = share;
						}
						val.w = 1.0f;
					}
					else // Paint
					{
						// active layer を固定量だけ積み上げ、他レイヤーを比例で削る
						val.a[layer] = std::min(val.a[layer] + delta, 1.0f);
						const float remain   = 1.0f - val.a[layer];
						float       otherSum = 0.0f;
						for (uint32_t i = 0; i < 3; ++i)
							if (i != layer) otherSum += val.a[i];

						if (otherSum > 1e-4f)
						{
							const float scale = remain / otherSum;
							for (uint32_t i = 0; i < 3; ++i)
								if (i != layer) val.a[i] *= scale;
						}
						else
						{
							// active が 100% に達したので他は 0
							for (uint32_t i = 0; i < 3; ++i)
								if (i != layer) val.a[i] = 0.0f;
						}
						val.w = 1.0f;
					}
				}
			}
		}

		void SplatmapPainter::AutoGenerate()
		{
			if (!chunk_) return;
			math::Vector4* splats = chunk_->GetSplatDataMutable();
			const uint32_t w = chunk_->GetSplatMapWidth();
			const uint32_t h = chunk_->GetSplatMapHeight();
			if (!splats || w == 0 || h == 0) return;

			for (uint32_t y = 0; y < h; ++y)
			{
				for (uint32_t x = 0; x < w; ++x)
				{
					const float u = w > 1 ? static_cast<float>(x) / (w - 1) : 0.0f;
					const float v = h > 1 ? static_cast<float>(y) / (h - 1) : 0.0f;
					const float noise = (HashNoise(static_cast<int>(u * noiseScale_ * 97.0f),
					                               static_cast<int>(v * noiseScale_ * 97.0f)) - 0.5f) * noiseStrength_;
					const float heightM = SampleHeightNormalized(*chunk_, u, v) * chunk_->GetHeightScale();
					const float slope = EstimateSlope(*chunk_, u, v);
					float snow = SmoothStep(snowHeightStart_, snowHeightEnd_, heightM + noise);
					float rock = SmoothStep(rockSlopeStart_, rockSlopeEnd_, slope + noise * 0.04f);
					snow *= (1.0f - rock * 0.45f);
					const float grass = std::max(0.0f, 1.0f - std::max(snow, rock));

					math::Vector4 weights(grass, snow, rock, 1.0f);
					NormalizeWeight(weights);
					splats[static_cast<size_t>(y) * w + x] = weights;
				}
			}
			dirty_ = true;
		}

		bool SplatmapPainter::ScreenToRay(float mx, float my, float sw, float sh,
		                                  const Camera& cam,
		                                  math::Vector3& outOrigin, math::Vector3& outDir) const
		{
			const float nx =  2.0f * mx / sw - 1.0f;
			const float ny = -(2.0f * my / sh - 1.0f);

			math::Matrix4x4 vpInv;
			vpInv.Inverse(cam.GetViewProjectionMatrix());
			const DirectX::XMMATRIX xmVP = vpInv;

			const DirectX::XMVECTOR nearNDC = DirectX::XMVectorSet(nx, ny, 0.0f, 1.0f);
			const DirectX::XMVECTOR farNDC  = DirectX::XMVectorSet(nx, ny, 1.0f, 1.0f);
			const DirectX::XMVECTOR nearClip = DirectX::XMVector4Transform(nearNDC, xmVP);
			const DirectX::XMVECTOR farClip  = DirectX::XMVector4Transform(farNDC, xmVP);

			const float wn = DirectX::XMVectorGetW(nearClip);
			const float wf = DirectX::XMVectorGetW(farClip);
			if (fabsf(wn) < 1e-7f || fabsf(wf) < 1e-7f) return false;

			DirectX::XMFLOAT4 np, fp;
			DirectX::XMStoreFloat4(&np, DirectX::XMVectorScale(nearClip, 1.0f / wn));
			DirectX::XMStoreFloat4(&fp, DirectX::XMVectorScale(farClip, 1.0f / wf));

			outOrigin.Set(np.x, np.y, np.z);
			math::Vector3 farPt(fp.x, fp.y, fp.z);
			outDir = farPt - outOrigin;
			return outDir.TryNormalize();
		}

		bool SplatmapPainter::RaycastHeightmap(const math::Vector3& origin, const math::Vector3& dir,
		                                      float& outU, float& outV, math::Vector3& outHit) const
		{
			if (!chunk_) return false;

			const float terrSize = chunk_->GetTerrainSize();
			const uint32_t res = chunk_->GetResolution();
			const float stepSize = terrSize / static_cast<float>(res) * 0.5f;
			const int maxSteps = static_cast<int>(res) * 4;

			math::Vector3 step = dir;
			step.Scale(stepSize);

			const float minX = worldOffset_.x;
			const float minZ = worldOffset_.z;
			const float maxX = worldOffset_.x + terrSize;
			const float maxZ = worldOffset_.z + terrSize;

			math::Vector3 pos = origin;
			math::Vector3 prevPos = origin;

			for (int i = 0; i < maxSteps; ++i)
			{
				prevPos = pos;
				pos.Add(step);
				if (pos.x < minX || pos.x > maxX || pos.z < minZ || pos.z > maxZ) continue;

				const float terrH = chunk_->GetHeight(pos.x - worldOffset_.x, pos.z - worldOffset_.z);
				if (pos.y <= terrH)
				{
					math::Vector3 lo = prevPos;
					math::Vector3 hi = pos;
					for (int b = 0; b < 8; ++b)
					{
						math::Vector3 mid = lo;
						mid.Add(hi);
						mid.Scale(0.5f);
						const float mh = chunk_->GetHeight(mid.x - worldOffset_.x, mid.z - worldOffset_.z);
						if (mid.y <= mh) hi = mid;
						else             lo = mid;
					}

					math::Vector3 hit = lo;
					hit.Add(hi);
					hit.Scale(0.5f);
					outU = std::clamp((hit.x - worldOffset_.x) / terrSize, 0.0f, 1.0f);
					outV = std::clamp((hit.z - worldOffset_.z) / terrSize, 0.0f, 1.0f);
					outHit = hit;
					return true;
				}
			}
			return false;
		}

		void SplatmapPainter::DrawBrushOverlay(const Camera& cam, float sw, float sh) const
		{
			if (!hitLastFrame_) return;
			const DirectX::XMMATRIX vp = cam.GetViewProjectionMatrix();

			auto projectToScreen = [&](const math::Vector3& world) -> ImVec2
			{
				const DirectX::XMVECTOR v = DirectX::XMVectorSet(world.x, world.y, world.z, 1.0f);
				const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(v, vp);
				const float w = DirectX::XMVectorGetW(clip);
				if (fabsf(w) < 1e-7f) return ImVec2(-9999.f, -9999.f);
				const float nx =  DirectX::XMVectorGetX(clip) / w;
				const float ny = -DirectX::XMVectorGetY(clip) / w;
				return ImVec2((nx * 0.5f + 0.5f) * sw, (ny * 0.5f + 0.5f) * sh);
			};

			const ImVec2 center = projectToScreen(lastHitWorld_);
			math::Vector3 edgePt = lastHitWorld_;
			edgePt.x += brushRadius_;
			const ImVec2 edge = projectToScreen(edgePt);
			const float dx = edge.x - center.x;
			const float dy = edge.y - center.y;
			const float screenR = std::max(2.0f, std::sqrt(dx * dx + dy * dy));
			ImGui::GetForegroundDrawList()->AddCircle(center, screenR,
				kLayerColors[std::min(activeLayer_, 2u)], 32, 2.0f);
		}

		void SplatmapPainter::ExportPNG(const char* path) const
		{
			if (!chunk_) return;
			const uint32_t w = chunk_->GetSplatMapWidth();
			const uint32_t h = chunk_->GetSplatMapHeight();
			const math::Vector4* splats = chunk_->GetSplatData();
			if (!splats || w == 0 || h == 0) return;

			std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
			for (uint32_t y = 0; y < h; ++y)
			{
				for (uint32_t x = 0; x < w; ++x)
				{
					math::Vector4 weights = splats[static_cast<size_t>(y) * w + x];
					NormalizeWeight(weights);
					const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
					pixels[idx + 0] = static_cast<uint8_t>(weights.x * 255.0f + 0.5f);
					pixels[idx + 1] = static_cast<uint8_t>(weights.y * 255.0f + 0.5f);
					pixels[idx + 2] = static_cast<uint8_t>(weights.z * 255.0f + 0.5f);
					pixels[idx + 3] = 255u;
				}
			}

			DirectX::Image img = {};
			img.width = w;
			img.height = h;
			img.format = DXGI_FORMAT_R8G8B8A8_UNORM;
			img.rowPitch = static_cast<size_t>(w) * 4;
			img.slicePitch = pixels.size();
			img.pixels = pixels.data();

			wchar_t wpath[512] = {};
			mbstowcs_s(nullptr, wpath, path, 511);
			DirectX::SaveToWICFile(img, DirectX::WIC_FLAGS_NONE,
			                       DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wpath);
		}

		void SplatmapPainter::DebugRenderMenu()
		{
			ImGui::MenuItem("Splatmap Painter", nullptr, &show_);
		}

		void SplatmapPainter::DebugRender()
		{
			if (!chunk_ || !show_) return;

			const ImGuiIO& io = ImGui::GetIO();
			const float sw = io.DisplaySize.x;
			const float sh = io.DisplaySize.y;
			const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

			hitLastFrame_ = false;
			if (enabled_ && paint3DEnabled_ && ActiveTerrainPaintTool() == TerrainPaintTool::Splatmap &&
			    !io.WantCaptureMouse && sw > 0.0f && sh > 0.0f)
			{
				Camera* cam = CameraManager::Get().GetCamera(CameraType::Main);
				if (cam)
				{
					math::Vector3 rayOrigin, rayDir;
					if (ScreenToRay(io.MousePos.x, io.MousePos.y, sw, sh, *cam, rayOrigin, rayDir))
					{
						float hitU, hitV;
						math::Vector3 hitWorld;
						if (RaycastHeightmap(rayOrigin, rayDir, hitU, hitV, hitWorld))
						{
							lastHitWorld_ = hitWorld;
							hitLastFrame_ = true;
							if (io.MouseClicked[0]) PushUndo();
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
				chunk_->RebuildSplatTexture();
				dirty_ = false;
			}

			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None;
			if (windowLocked_) windowFlags |= ImGuiWindowFlags_NoMove;
			ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin("Splatmap Painter", &show_, windowFlags))
			{
				ImGui::End();
				return;
			}

			ImGui::Checkbox("Enabled", &enabled_);
			ImGui::SameLine();
			ImGui::Checkbox("Lock Window", &windowLocked_);
			ImGui::SameLine();
			if (ImGui::RadioButton("Active 3D Tool", ActiveTerrainPaintTool() == TerrainPaintTool::Splatmap))
				ActiveTerrainPaintTool() = TerrainPaintTool::Splatmap;
			ImGui::Separator();

			ImGui::BeginDisabled(!enabled_);

			ImGui::Text("Layer");
			for (uint32_t i = 0; i < kLayerCount; ++i)
			{
				if (i > 0) ImGui::SameLine();
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::RadioButton(kLayerLabels[i], activeLayer_ == i))
					activeLayer_ = i;
				ImGui::PopID();
			}

			static const char* kModeLabels[] = { "Paint", "Erase", "Smooth" };
			int modeIdx = static_cast<int>(brushMode_);
			for (int i = 0; i < 3; ++i)
			{
				if (i > 0) ImGui::SameLine();
				if (ImGui::RadioButton(kModeLabels[i], modeIdx == i))
					brushMode_ = static_cast<BrushMode>(i);
			}

			ImGui::SliderFloat("Radius (m)", &brushRadius_, 0.5f, 50.0f);
			ImGui::SliderFloat("Strength", &brushStrength_, 0.01f, 1.0f);
			ImGui::Checkbox("3D Paint", &paint3DEnabled_);
			ImGui::SameLine();
			if (ImGui::Button("Undo") || (enabled_ && ActiveTerrainPaintTool() == TerrainPaintTool::Splatmap && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)))
				PopUndo();

			if (ImGui::CollapsingHeader("Auto Generate"))
			{
				const float heightScale = chunk_->GetHeightScale();
				ImGui::SliderFloat("Snow Start (m)", &snowHeightStart_, 0.0f, heightScale);
				ImGui::SliderFloat("Snow End (m)", &snowHeightEnd_, 0.0f, heightScale);
				if (snowHeightEnd_ < snowHeightStart_) snowHeightEnd_ = snowHeightStart_;
				ImGui::SliderFloat("Rock Slope Start", &rockSlopeStart_, 0.0f, 1.0f);
				ImGui::SliderFloat("Rock Slope End", &rockSlopeEnd_, 0.0f, 1.0f);
				if (rockSlopeEnd_ < rockSlopeStart_) rockSlopeEnd_ = rockSlopeStart_;
				ImGui::SliderFloat("Noise Scale", &noiseScale_, 0.0f, 24.0f);
				ImGui::SliderFloat("Noise Strength", &noiseStrength_, 0.0f, 3.0f);
				if (ImGui::Button("Generate"))
				{
					PushUndo();
					AutoGenerate();
				}
			}

			ImGui::Separator();
			if (auto* srv = chunk_->GetSplatTexture())
			{
				ImTextureID texID = reinterpret_cast<ImTextureID>(srv->GetNativeHandle());
				constexpr float kSize = 256.0f;
				const ImVec2 imageMin = ImGui::GetCursorScreenPos();
				ImGui::Image(texID, ImVec2(kSize, kSize));

				if (ImGui::IsItemHovered())
				{
					const ImVec2 mp = ImGui::GetMousePos();
					const float u = (mp.x - imageMin.x) / kSize;
					const float v = (mp.y - imageMin.y) / kSize;
					const float brushUVPx = (brushRadius_ / chunk_->GetTerrainSize()) * kSize;
					ImGui::GetWindowDrawList()->AddCircle(mp, brushUVPx,
						kLayerColors[std::min(activeLayer_, 2u)], 32, 1.5f);

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
			else
			{
				ImGui::TextDisabled("Splat texture is not available");
			}

			ImGui::Separator();
			ImGui::Text("Splat: %ux%u", chunk_->GetSplatMapWidth(), chunk_->GetSplatMapHeight());
			static char exportPath[256] = "Assets/Terrain/splatmap.png";
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##splat_export_path", exportPath, sizeof(exportPath));
			ImGui::SameLine();
			if (ImGui::Button("Export PNG"))
				ExportPNG(exportPath);

			if (ImGui::Button("Reset Grass"))
			{
				PushUndo();
				chunk_->FillSplatLayer(0);
			}

			ImGui::EndDisabled();
			ImGui::End();
		}
	}
}
#endif // AQ_DEBUG_IMGUI