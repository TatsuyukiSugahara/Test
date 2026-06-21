#include "aq.h"
#include "OceanMesh.h"
#include "Rendering/RenderFrame.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Resource/Resource.h"
#include "Engine.h"


namespace aq
{
	namespace ocean
	{
		void OceanMesh::Initialize(const OceanParams& params)
		{
			const uint32_t N    = params.resolution;
			const uint32_t vN   = N + 1;           // 1辺の頂点数
			const float cellSize = params.size / static_cast<float>(N);

			// --- 頂点生成 (Y=0 の平坦なグリッド) ---
			std::vector<graphics::VertexData> verts(static_cast<size_t>(vN) * vN);
			for (uint32_t zi = 0; zi < vN; ++zi)
			{
				for (uint32_t xi = 0; xi < vN; ++xi)
				{
					const float nx = static_cast<float>(xi) / N;
					const float nz = static_cast<float>(zi) / N;

					auto& v = verts[zi * vN + xi];
					v.position.Set(nx * params.size, 0.0f, nz * params.size);
					v.normal.Set(0.0f, 1.0f, 0.0f);             // 上向き (+Y)
					v.uv.Set(nx, nz);
					v.tangent.Set(1.0f, 0.0f, 0.0f, 1.0f);      // +X 方向, w=+1
				}
			}

			// --- インデックス生成 (HeightmapChunk と同じ CW 規約) ---
			std::vector<uint32_t> indices;
			indices.reserve(static_cast<size_t>(N) * N * 6);
			for (uint32_t zi = 0; zi < N; ++zi)
			{
				for (uint32_t xi = 0; xi < N; ++xi)
				{
					const uint32_t i00 =  zi      * vN + xi;
					const uint32_t i10 =  zi      * vN + (xi + 1);
					const uint32_t i01 = (zi + 1) * vN + xi;
					const uint32_t i11 = (zi + 1) * vN + (xi + 1);

					indices.push_back(i00); indices.push_back(i01); indices.push_back(i10);
					indices.push_back(i10); indices.push_back(i01); indices.push_back(i11);
				}
			}

			mesh_.Initialize(verts.data(),   static_cast<uint32_t>(verts.size()),
			                 indices.data(),  static_cast<uint32_t>(indices.size()),
			                 graphics::StaticMesh::ShaderType::OceanLit);

			// UV スクロール用に Wrap サンプラーを設定
			graphics::SamplerDesc sd;
			sd.filter   = graphics::FilterMode::MinMagMipLinear;
			sd.addressU = graphics::AddressMode::Wrap;
			sd.addressV = graphics::AddressMode::Wrap;
			sd.addressW = graphics::AddressMode::Wrap;
			mesh_.SetSamplerState(graphics::GraphicsDevice::Get().CreateSamplerState(sd));

			// --- 法線マップテクスチャ (オプション) ---
			auto& rm = res::ResourceManager::Get();

			if (params.normalMapPath1 && params.normalMapPath1[0])
			{
				mesh_.SetTexture(rendering::TextureSlot::Albedo,
				                 rm.Load<res::GPUResource>(params.normalMapPath1));
			}
			if (params.normalMapPath2 && params.normalMapPath2[0])
			{
				mesh_.SetTexture(rendering::TextureSlot::Normal,
				                 rm.Load<res::GPUResource>(params.normalMapPath2));
			}
			// 両方ロード済みのとき MAT_HAS_NORMAL を立てる
			// (ロード完了は Update() 後なので FillRenderItem 内でフラグを補正)
		}


		void OceanMesh::Update(const math::Vector3& position,
		                       const math::Quaternion& rotation,
		                       const math::Vector3& scale)
		{
			mesh_.Update(position, rotation, scale);
		}


		bool OceanMesh::FillRenderItem(rendering::OceanRenderItem& item,
		                               const OceanParams& params,
		                               float time) const
		{
			// 法線マップが両スロットとも存在する場合のみ MAT_HAS_NORMAL を立てる
			const bool hasNM1 = (params.normalMapPath1 && params.normalMapPath1[0]);
			const bool hasNM2 = (params.normalMapPath2 && params.normalMapPath2[0]);
			// 一時的にフラグを合わせる (StaticMesh 側の flags に触れないよう const_cast は避ける)
			// → FillRenderItem 後に draw item の flags を上書きする
			if (!mesh_.FillRenderItem(item.base))
				return false;

			// 法線マップが揃っているときのみ PS 側のフラグを立てる
			if (hasNM1 && hasNM2
			    && item.base.textures[static_cast<uint32_t>(rendering::TextureSlot::Albedo)]
			    && item.base.textures[static_cast<uint32_t>(rendering::TextureSlot::Normal)])
			{
				item.base.materialCB.flags |= static_cast<uint32_t>(graphics::MatFlag_HasNormal);
			}
			else
			{
				item.base.materialCB.flags &= ~static_cast<uint32_t>(graphics::MatFlag_HasNormal);
			}

			// --- OceanCBData を params + time から構築 ---
			auto& cb = item.oceanCB;

			cb.time       = time;
			cb.steepness  = params.waveQ;
			cb._p0[0] = cb._p0[1] = 0.0f;

			cb.normalScale1 = params.normalScale1;
			cb.normalDirX1  = params.normalDirX1;
			cb.normalDirZ1  = params.normalDirZ1;
			cb.normalSpeed1 = params.normalSpeed1;

			cb.normalScale2 = params.normalScale2;
			cb.normalDirX2  = params.normalDirX2;
			cb.normalDirZ2  = params.normalDirZ2;
			cb.normalSpeed2 = params.normalSpeed2;

			cb.deepR = params.deepColor.x;
			cb.deepG = params.deepColor.y;
			cb.deepB = params.deepColor.z;
			cb._p1   = 0.0f;

			cb.shallR = params.shallowColor.x;
			cb.shallG = params.shallowColor.y;
			cb.shallB = params.shallowColor.z;
			cb._p2    = 0.0f;

			cb.fresnelBias   = params.fresnelBias;
			cb.fresnelScale  = params.fresnelScale;
			cb.fresnelPower  = params.fresnelPower;
			cb.sunShininess  = params.sunShininess;

			cb.sunIntensity = params.sunIntensity;
			cb.skyR = params.skyColor.x;
			cb.skyG = params.skyColor.y;
			cb.skyB = params.skyColor.z;

			for (int i = 0; i < 4; ++i)
			{
				cb.waves[i].dirX       = params.waves[i].dirX;
				cb.waves[i].dirZ       = params.waves[i].dirZ;
				cb.waves[i].amplitude  = params.waves[i].amplitude;
				cb.waves[i].wavelength = params.waves[i].wavelength;
				cb.waveSpeeds[i]       = params.waves[i].speed;
			}

			return true;
		}
	}
}
