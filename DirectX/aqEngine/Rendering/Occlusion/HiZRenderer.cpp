#include "aq.h"
#include "HiZRenderer.h"
#include "HiZBuildCommand.h"
#include "HiZReadbackCommand.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IShader.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <DirectXMath.h>
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Occlusion/Debug/HiZDebugPanel.h"
#endif


namespace aq
{
	namespace rendering
	{
		bool HiZRenderer::Initialize(uint32_t fullWidth, uint32_t fullHeight)
		{
			auto& gd = graphics::GraphicsDevice::Get();

			reconstructShader_ = gd.CreateShader(
				"Assets/Shader/HiZReconstruct.fx", "main", graphics::IShader::ShaderType::CS);
			downsampleShader_ = gd.CreateShader(
				"Assets/Shader/HiZDownsample.fx", "main", graphics::IShader::ShaderType::CS);
			if (!reconstructShader_ || !downsampleShader_)
			{
				EngineAssertMsg(false, "Hi-Z シェーダーのロードに失敗しました");
				return false;
			}

			// レベル0 = フル解像度の 1/2。以降 1/2 ずつ。1x1 になるか kMaxLevels で打ち切る。
			uint32_t w = (fullWidth  > 1u) ? (fullWidth  >> 1) : 1u;
			uint32_t h = (fullHeight > 1u) ? (fullHeight >> 1) : 1u;
			levelCount_ = 0;
			for (uint32_t i = 0; i < kMaxLevels; ++i)
			{
				graphics::RenderTargetDesc desc;
				desc.width       = w;
				desc.height      = h;
				desc.colorFormat = graphics::PixelFormat::R32_Float;  // 深度 (最遠) を格納
				desc.hasDepth    = false;
				levelHandles_[i] = gd.CreateOffscreenRenderTarget(desc);
				if (!levelHandles_[i].IsValid())
				{
					EngineAssertMsg(false, "Hi-Z レベル RT の生成に失敗しました");
					return false;
				}
				levelW_[i] = w;
				levelH_[i] = h;
				++levelCount_;

				if (w == 1u && h == 1u) break;
				w = (w > 1u) ? (w >> 1) : 1u;
				h = (h > 1u) ? (h >> 1) : 1u;
			}

			HiZCBData initData{};
			hiZCB_ = gd.CreateConstantBuffer(&initData, sizeof(initData));
			if (!hiZCB_)
			{
				EngineAssertMsg(false, "Hi-Z CB の生成に失敗しました");
				return false;
			}

			// CPU リードバック対象レベル: 幅が ~200px 以下になる最初の (粗い) レベル。
			// 粗いほどリードバックが軽く、テストも保守的になる。
			readbackLevel_ = 0;
			for (uint32_t i = 0; i < levelCount_; ++i)
			{
				if (levelW_[i] <= 200u) { readbackLevel_ = i; break; }
			}

			return true;
		}


		void HiZRenderer::BuildCommandList(const RenderFrame& frame, RenderCommandList& outList,
		                                   RenderTargetHandle gbufferWorldPos)
		{
			if (levelCount_ == 0 || !gbufferWorldPos.IsValid()) return;

			// viewProj = view * projection (列ベクトル規約・非転置アップロード)
			HiZCBData cb{};
			cb.viewProj.Mull(frame.camera.viewMatrix, frame.camera.projectionMatrix);
			cb.nearZ = frame.camera.nearZ;
			cb.farZ  = frame.camera.farZ;

			outList.Enqueue<HiZBuildCommand>(
				reconstructShader_.get(),
				downsampleShader_.get(),
				hiZCB_.get(),
				cb,
				gbufferWorldPos,
				levelHandles_,
				levelW_,
				levelH_,
				levelCount_);

			// 粗いレベルを CPU へリードバック (オクリュージョンテスト用)
			outList.Enqueue<HiZReadbackCommand>(
				this, levelHandles_[readbackLevel_], levelW_[readbackLevel_], levelH_[readbackLevel_]);
		}


		void HiZRenderer::StoreReadback(std::vector<float>&& data, uint32_t w, uint32_t h)
		{
			std::lock_guard<std::mutex> lock(cpuHiZMutex_);
			cpuHiZ_  = std::move(data);
			cpuHiZW_ = w;
			cpuHiZH_ = h;
		}


		bool HiZRenderer::IsOccluded(const math::AABB& worldBox,
		                             const math::Matrix4x4& viewProj,
		                             float nearZ, float farZ) const
		{
			std::lock_guard<std::mutex> lock(cpuHiZMutex_);
			if (cpuHiZ_.empty() || cpuHiZW_ == 0 || cpuHiZH_ == 0) return false;

			const float W = static_cast<float>(cpuHiZW_);
			const float H = static_cast<float>(cpuHiZH_);
			const DirectX::XMMATRIX vp = viewProj;

			const math::Vector3& c = worldBox.center;
			const math::Vector3& e = worldBox.extent;

			float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
			float nearestLin = FLT_MAX;

			for (int i = 0; i < 8; ++i)
			{
				const float cx = c.x + ((i & 1) ? e.x : -e.x);
				const float cy = c.y + ((i & 2) ? e.y : -e.y);
				const float cz = c.z + ((i & 4) ? e.z : -e.z);
				DirectX::XMVECTOR clip =
					DirectX::XMVector4Transform(DirectX::XMVectorSet(cx, cy, cz, 1.0f), vp);
				const float cw = DirectX::XMVectorGetW(clip);
				if (cw <= 1e-4f) return false;  // near 平面跨ぎ → 可視扱い (保守的)

				const float ndcx = DirectX::XMVectorGetX(clip) / cw;
				const float ndcy = DirectX::XMVectorGetY(clip) / cw;
				const float sx = (ndcx * 0.5f + 0.5f) * W;
				const float sy = (0.5f - ndcy * 0.5f) * H;  // NDC y 上 → テクスチャ y 下

				minX = std::min(minX, sx); maxX = std::max(maxX, sx);
				minY = std::min(minY, sy); maxY = std::max(maxY, sy);

				const float lin = (cw - nearZ) / std::max(farZ - nearZ, 1e-6f);  // ビュー深度の線形正規化
				nearestLin = std::min(nearestLin, lin);
			}

			// 画面外なら判定しない (可視扱い)
			if (maxX < 0.0f || maxY < 0.0f || minX >= W || minY >= H) return false;

			const int ix0 = static_cast<int>(std::floor(std::max(minX, 0.0f)));
			const int iy0 = static_cast<int>(std::floor(std::max(minY, 0.0f)));
			const int ix1 = static_cast<int>(std::floor(std::min(maxX, W - 1.0f)));
			const int iy1 = static_cast<int>(std::floor(std::min(maxY, H - 1.0f)));

			// 覆う領域の最遠オクルーダー深度 (Hi-Z は max-reduction)
			float maxHiZ = 0.0f;
			for (int ty = iy0; ty <= iy1; ++ty)
				for (int tx = ix0; tx <= ix1; ++tx)
					maxHiZ = std::max(maxHiZ, cpuHiZ_[static_cast<size_t>(ty) * cpuHiZW_ + tx]);

			// 物体の最近点が、覆う領域の最遠面より奥 → 完全に背後 → オクルード
			constexpr float EPS = 0.002f;
			return nearestLin > maxHiZ + EPS;
		}


		HiZRenderer::ReadbackInfo HiZRenderer::GetReadbackInfo() const
		{
			ReadbackInfo info;
			std::lock_guard<std::mutex> lock(cpuHiZMutex_);
			if (cpuHiZ_.empty()) return info;
			info.hasData = true;
			info.width   = cpuHiZW_;
			info.height  = cpuHiZH_;
			info.minV = FLT_MAX;
			info.maxV = -FLT_MAX;
			for (float v : cpuHiZ_)
			{
				info.minV = std::min(info.minV, v);
				info.maxV = std::max(info.maxV, v);
			}
			return info;
		}


		HiZRenderer::SelfTestResult HiZRenderer::SelfTest(const math::Matrix4x4& viewProj,
		                                                  const math::Vector3& camPos,
		                                                  const math::Vector3& camForward,
		                                                  float nearZ, float farZ) const
		{
			SelfTestResult r;
			(void)camPos;  // レイは逆投影から求めるため未使用 (API 対称性のため受け取る)

			// 中間深度を持つ画素を1つ探す (背景=1 や近接=0 は避ける)
			uint32_t W = 0, H = 0, tx = 0, ty = 0;
			float d = 0.0f;
			{
				std::lock_guard<std::mutex> lock(cpuHiZMutex_);
				if (cpuHiZ_.empty() || cpuHiZW_ == 0) return r;
				W = cpuHiZW_; H = cpuHiZH_;
				bool found = false;
				for (uint32_t y = 0; y < H && !found; ++y)
					for (uint32_t x = 0; x < W; ++x)
					{
						const float v = cpuHiZ_[static_cast<size_t>(y) * W + x];
						if (v > 0.02f && v < 0.9f) { tx = x; ty = y; d = v; found = true; break; }
					}
				if (!found) return r;
			}

			r.valid = true;
			r.sampleDepth = d;

			// 画素中心の NDC
			const float ndcx = ((static_cast<float>(tx) + 0.5f) / W) * 2.0f - 1.0f;
			const float ndcy = 1.0f - ((static_cast<float>(ty) + 0.5f) / H) * 2.0f;

			// 逆 viewProj で near/far 平面のワールド点を求めレイを作る
			const DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, viewProj);
			const DirectX::XMVECTOR wn = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcx, ndcy, 0.0f, 1.0f), invVP);
			const DirectX::XMVECTOR wf = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcx, ndcy, 1.0f, 1.0f), invVP);
			DirectX::XMVECTOR dir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(wf, wn));

			const DirectX::XMVECTOR fwd = DirectX::XMVector3Normalize(camForward);
			float cosT = DirectX::XMVectorGetX(DirectX::XMVector3Dot(dir, fwd));
			if (cosT < 1e-3f) cosT = 1e-3f;

			// 目標ビュー深度 (線形 d) とその前後 Δ
			const float viewZ = nearZ + d * (farZ - nearZ);
			const float delta = 0.1f * (farZ - nearZ);

			auto worldAt = [&](float vz) -> math::Vector3
			{
				const float dist = (vz - nearZ) / cosT;
				DirectX::XMVECTOR w = DirectX::XMVectorAdd(wn, DirectX::XMVectorScale(dir, dist));
				math::Vector3 out;
				DirectX::XMStoreFloat3(&out.vector, w);
				return out;
			};

			const math::Vector3 nearCenter = worldAt(viewZ - delta);  // 手前 → 可視のはず
			const math::Vector3 farCenter  = worldAt(viewZ + delta);  // 奥   → 遮蔽のはず
			const math::Vector3 ext(0.1f, 0.1f, 0.1f);

			r.nearOccluded = IsOccluded(math::AABB(nearCenter, ext), viewProj, nearZ, farZ);
			r.farOccluded  = IsOccluded(math::AABB(farCenter,  ext), viewProj, nearZ, farZ);
			r.pass = (!r.nearOccluded && r.farOccluded);
			return r;
		}


#ifdef AQ_DEBUG_IMGUI
		std::unique_ptr<IDebugRenderable> HiZRenderer::CreateDebugPanel()
		{
			return std::make_unique<HiZDebugPanel>(*this);
		}
#endif
	}
}
