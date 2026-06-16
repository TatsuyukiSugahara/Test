#include "EnginePreCompile.h"
#include "HeightmapChunk.h"
#include "Graphics/GraphicsDevice.h"
#include "Resource/Resource.h"

#include <cmath>


namespace engine
{
	namespace terrain
	{
		namespace
		{
			/**
			 * 画像ファイルを読み込み、Rチャンネルを [0,1] の float 配列として返す。
			 * 内部で DirectXTex を使い R8G8B8A8_UNORM に変換してから取り出す。
			 */
			bool LoadHeightValues(const char* path,
			                      std::vector<float>& outHeights,
			                      uint32_t& outW, uint32_t& outH)
			{
				if (!path || !path[0]) return false;

				wchar_t wpath[512] = {};
				size_t converted = 0;
				if (mbstowcs_s(&converted, wpath, path, 511) != 0) return false;

				DirectX::TexMetadata meta;
				DirectX::ScratchImage raw;

				std::string spath(path);
				const auto dot = spath.rfind('.');
				std::string ext = (dot != std::string::npos) ? spath.substr(dot) : "";
				for (char& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

				HRESULT hr = (ext == ".dds")
					? DirectX::LoadFromDDSFile(wpath, DirectX::DDS_FLAGS_NONE, &meta, raw)
					: DirectX::LoadFromWICFile(wpath, DirectX::WIC_FLAGS_NONE, &meta, raw);
				if (FAILED(hr)) return false;

				// RGBA8 に統一してRチャンネルだけ取り出す
				DirectX::ScratchImage rgba;
				hr = DirectX::Convert(*raw.GetImage(0, 0, 0),
				                      DXGI_FORMAT_R8G8B8A8_UNORM,
				                      DirectX::TEX_FILTER_DEFAULT,
				                      DirectX::TEX_THRESHOLD_DEFAULT,
				                      rgba);
				if (FAILED(hr)) return false;

				const DirectX::Image* img = rgba.GetImage(0, 0, 0);
				if (!img || !img->pixels) return false;

				outW = static_cast<uint32_t>(img->width);
				outH = static_cast<uint32_t>(img->height);
				outHeights.resize(outW * outH);

				for (uint32_t y = 0; y < outH; ++y)
				{
					for (uint32_t x = 0; x < outW; ++x)
					{
						// 1ピクセル = RGBA 4バイト。R チャンネルのみ使用
						const uint8_t r = img->pixels[y * img->rowPitch + x * 4];
						outHeights[y * outW + x] = r / 255.0f;
					}
				}
				return true;
			}


			/** 高さ配列からバイリニア補間でサンプリングする。nx, nz は [0,1] */
			float BilinearSample(const std::vector<float>& h,
			                     uint32_t w, uint32_t mapH,
			                     float nx, float nz)
			{
				const float hx = nx * (w    - 1);
				const float hz = nz * (mapH - 1);
				const int32_t x0 = std::clamp(static_cast<int32_t>(hx), 0, static_cast<int32_t>(w)    - 1);
				const int32_t z0 = std::clamp(static_cast<int32_t>(hz), 0, static_cast<int32_t>(mapH) - 1);
				const int32_t x1 = std::min(x0 + 1, static_cast<int32_t>(w)    - 1);
				const int32_t z1 = std::min(z0 + 1, static_cast<int32_t>(mapH) - 1);
				const float fx = hx - x0;
				const float fz = hz - z0;
				const float h00 = h[z0 * w + x0];
				const float h10 = h[z0 * w + x1];
				const float h01 = h[z1 * w + x0];
				const float h11 = h[z1 * w + x1];
				return (h00 + (h10 - h00) * fx) * (1.0f - fz)
				     + (h01 + (h11 - h01) * fx) *         fz;
			}
		}


		void HeightmapChunk::Initialize(const Desc& desc)
		{
			terrainSize_ = desc.terrainSize;
			heightScale_ = desc.heightScale;

			// ハイトマップを CPU で読み込む (DirectXTex 直接使用)
			if (!LoadHeightValues(desc.heightmapPath, heightData_, hmapWidth_, hmapHeight_))
			{
				hmapWidth_ = hmapHeight_ = desc.resolution + 1;
				heightData_.assign(static_cast<size_t>(hmapWidth_) * hmapHeight_, 0.0f);
			}

			BuildMesh(heightData_, hmapWidth_, hmapHeight_, desc);

			auto& rm = res::ResourceManager::Get();

			// t0: スプラットマップ (Albedo スロット)
			// フラグが立っているときのみシェーダーでスプラット合成を行う
			if (desc.splatmapPath && desc.splatmapPath[0])
			{
				mesh_.SetTexture(rendering::TextureSlot::Albedo,
				                 rm.Load<res::GPUResource>(desc.splatmapPath));
				mesh_.SetMaterialFlag(graphics::MatFlag_HasSplatMap, true);
			}

			// t1-t3: レイヤーテクスチャ (Normal/Specular/Emissive スロットを流用)
			static constexpr rendering::TextureSlot kLayerSlots[3] = {
				rendering::TextureSlot::Normal,
				rendering::TextureSlot::Specular,
				rendering::TextureSlot::Emissive,
			};
			for (int i = 0; i < 3; ++i)
			{
				if (desc.layerPaths[i] && desc.layerPaths[i][0])
				{
					mesh_.SetTexture(kLayerSlots[i],
					                 rm.Load<res::GPUResource>(desc.layerPaths[i]));
				}
			}

			// シェーダーに渡すレイヤーUV倍率
			mesh_.Param(0).x = desc.layerTiling;

			// Wrap サンプラー (タイリング用)
			graphics::SamplerDesc sd;
			sd.filter   = graphics::FilterMode::MinMagMipLinear;
			sd.addressU = graphics::AddressMode::Wrap;
			sd.addressV = graphics::AddressMode::Wrap;
			sd.addressW = graphics::AddressMode::Wrap;
			mesh_.SetSamplerState(graphics::GraphicsDevice::Get().CreateSamplerState(sd));
		}


		void HeightmapChunk::BuildMesh(const std::vector<float>& heights,
		                               uint32_t mapW, uint32_t mapH,
		                               const Desc& desc)
		{
			const uint32_t N        = desc.resolution;
			const uint32_t vN       = N + 1;            // 1辺の頂点数
			const float    cellSize = desc.terrainSize / static_cast<float>(N);

			// --- 頂点位置・UV 生成 ---
			std::vector<graphics::VertexData> verts(static_cast<size_t>(vN) * vN);
			for (uint32_t zi = 0; zi < vN; ++zi)
			{
				for (uint32_t xi = 0; xi < vN; ++xi)
				{
					const float nx = static_cast<float>(xi) / N;
					const float nz = static_cast<float>(zi) / N;

					auto& v = verts[zi * vN + xi];
					v.position.Set(nx * desc.terrainSize,
					               BilinearSample(heights, mapW, mapH, nx, nz) * desc.heightScale,
					               nz * desc.terrainSize);
					// UV は [0,1] 正規化で保持。タイリングはシェーダー (params[0].x) で適用する。
					// スプラットマップは正規化 UV をそのまま使い、レイヤーはシェーダー内で倍率をかける。
					v.uv.Set(nx, nz);
				}
			}

			// --- 法線・タンジェント計算 (中心差分) ---
			for (uint32_t zi = 0; zi < vN; ++zi)
			{
				for (uint32_t xi = 0; xi < vN; ++xi)
				{
					// 隣接インデックス (端は同じ頂点でクランプ)
					const uint32_t xL = (xi > 0) ? xi - 1 : xi;
					const uint32_t xR = (xi < N) ? xi + 1 : xi;
					const uint32_t zU = (zi > 0) ? zi - 1 : zi;
					const uint32_t zD = (zi < N) ? zi + 1 : zi;

					const float hL = verts[zi  * vN + xL].position.y;
					const float hR = verts[zi  * vN + xR].position.y;
					const float hU = verts[zU  * vN + xi].position.y;
					const float hD = verts[zD  * vN + xi].position.y;

					// X/Z 方向の勾配 (中心差分。端は片側差分になる)
					const float scaleX  = static_cast<float>(xR - xL);
					const float scaleZ  = static_cast<float>(zD - zU);
					const float dydx    = (hR - hL) / (scaleX * cellSize);
					const float dydz    = (hD - hU) / (scaleZ * cellSize);

					// 法線: 勾配から導出 (上向き (+Y) が正)
					math::Vector3 n(-dydx, 1.0f, -dydz);
					n.Normalize();
					verts[zi * vN + xi].normal = n;

					// タンジェント: X 方向の接線ベクトル (1, dydx, 0) を正規化
					const float tlen = std::sqrt(1.0f + dydx * dydx);
					verts[zi * vN + xi].tangent.Set(1.0f / tlen, dydx / tlen, 0.0f, 1.0f);
				}
			}

			// --- インデックス生成 ---
			// D3D11 デフォルト: 時計回り (CW) = 表面。カメラ上方から見て CW になるよう i00→i10→i01 とする。
			// (i00→i01→i10 は上方から見ると CCW になり、バックフェースカリングで消える)
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

					indices.push_back(i00); indices.push_back(i10); indices.push_back(i01);
					indices.push_back(i10); indices.push_back(i11); indices.push_back(i01);
				}
			}

			mesh_.Initialize(verts.data(),   static_cast<uint32_t>(verts.size()),
			                 indices.data(),  static_cast<uint32_t>(indices.size()),
			                 graphics::StaticMesh::ShaderType::TerrainLit);
		}


		void HeightmapChunk::Update(const math::Vector3& position,
		                            const math::Quaternion& rotation,
		                            const math::Vector3& scale)
		{
			mesh_.Update(position, rotation, scale);
		}


		bool HeightmapChunk::FillRenderItem(rendering::RenderItem& item) const
		{
			return mesh_.FillRenderItem(item);
		}


		float HeightmapChunk::GetHeight(float worldX, float worldZ) const
		{
			if (heightData_.empty() || terrainSize_ <= 0.0f) return 0.0f;
			return BilinearSample(heightData_, hmapWidth_, hmapHeight_,
			                      worldX / terrainSize_,
			                      worldZ / terrainSize_) * heightScale_;
		}
	}
}
