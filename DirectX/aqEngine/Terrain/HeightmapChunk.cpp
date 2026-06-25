#include "aq.h"
#include "HeightmapChunk.h"
#include "Graphics/GraphicsDevice.h"
#include "Resource/Resource.h"

#include <cmath>
#include <cctype>


namespace aq
{
	namespace terrain
	{
		namespace
		{
			void NormalizeSplatWeight(math::Vector4& w)
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

			bool LoadSplatValues(const char* path,
			                     std::vector<math::Vector4>& outSplat,
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
				outSplat.resize(static_cast<size_t>(outW) * outH);

				for (uint32_t y = 0; y < outH; ++y)
				{
					for (uint32_t x = 0; x < outW; ++x)
					{
						const uint8_t* p = img->pixels + y * img->rowPitch + x * 4;
						math::Vector4 w(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f);
						NormalizeSplatWeight(w);
						outSplat[static_cast<size_t>(y) * outW + x] = w;
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

			/** heightData_ から頂点配列を構築する (法線・タンジェントを含む) */
			void ComputeVertices(const std::vector<float>& heights,
			                     uint32_t mapW, uint32_t mapH,
			                     const HeightmapChunk::Desc& desc,
			                     std::vector<graphics::VertexData>& verts)
			{
				const uint32_t N        = desc.resolution;
				const uint32_t vN       = N + 1;
				const float    cellSize = desc.terrainSize / static_cast<float>(N);

				verts.resize(static_cast<size_t>(vN) * vN);

				// 位置・UV
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
						v.uv.Set(nx, nz);
					}
				}

				// 法線・タンジェント (中心差分)
				for (uint32_t zi = 0; zi < vN; ++zi)
				{
					for (uint32_t xi = 0; xi < vN; ++xi)
					{
						const uint32_t xL = (xi > 0) ? xi - 1 : xi;
						const uint32_t xR = (xi < N) ? xi + 1 : xi;
						const uint32_t zU = (zi > 0) ? zi - 1 : zi;
						const uint32_t zD = (zi < N) ? zi + 1 : zi;

						const float hL = verts[zi  * vN + xL].position.y;
						const float hR = verts[zi  * vN + xR].position.y;
						const float hU = verts[zU  * vN + xi].position.y;
						const float hD = verts[zD  * vN + xi].position.y;

						const float scaleX = static_cast<float>(xR - xL);
						const float scaleZ = static_cast<float>(zD - zU);
						const float dydx   = (hR - hL) / (scaleX * cellSize);
						const float dydz   = (hD - hU) / (scaleZ * cellSize);

						math::Vector3 n(-dydx, 1.0f, -dydz);
						n.Normalize();
						verts[zi * vN + xi].normal = n;

						const float tlen = std::sqrt(1.0f + dydx * dydx);
						verts[zi * vN + xi].tangent.Set(1.0f / tlen, dydx / tlen, 0.0f, 1.0f);
					}
				}
			}
		}


		void HeightmapChunk::Initialize(const Desc& desc)
		{
			desc_        = desc;
			terrainSize_ = desc.terrainSize;
			heightScale_ = desc.heightScale;

			// ハイトマップを CPU で読み込む (DirectXTex 直接使用)
			if (!LoadHeightValues(desc.heightmapPath, heightData_, hmapWidth_, hmapHeight_))
			{
				hmapWidth_ = hmapHeight_ = desc.resolution + 1;
				heightData_.assign(static_cast<size_t>(hmapWidth_) * hmapHeight_, 0.0f);
			}

			if (!LoadSplatValues(desc.splatmapPath, splatData_, splatMapWidth_, splatMapHeight_))
			{
				splatMapWidth_  = hmapWidth_;
				splatMapHeight_ = hmapHeight_;
				splatData_.assign(static_cast<size_t>(splatMapWidth_) * splatMapHeight_,
				                  math::Vector4(1.0f, 0.0f, 0.0f, 1.0f));
			}

			BuildMesh(heightData_, hmapWidth_, hmapHeight_, desc);

			auto& rm = res::ResourceManager::Get();

			// t0: runtime splat map generated from CPU weights.
			RebuildSplatTexture();

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
			math::Vector4 tiling = mesh_.GetParameter(0);
			tiling.x = desc.layerTiling;
			mesh_.SetParameter(0, tiling);

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
			const uint32_t N  = desc.resolution;
			const uint32_t vN = N + 1;

			// 頂点生成 (vertCache_ に保持して RebuildFromHeights でも再利用)
			ComputeVertices(heights, mapW, mapH, desc, vertCache_);

			// インデックス生成 (地形変更時は変わらないため1回のみ)
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

			// 動的VBで初期化: ペイント時に Map/Unmap で頂点を書き換える
			mesh_.InitializeDynamic(vertCache_.data(),  static_cast<uint32_t>(vertCache_.size()),
			                        indices.data(),      static_cast<uint32_t>(indices.size()),
			                        graphics::StaticMesh::ShaderType::TerrainPBRLit);
			// 地形デフォルト PBR 値（metallic=0 は TerrainPBRGBuffer.fx でハードコード）
			mesh_.SetRoughness(0.8f);
			mesh_.SpecularRef() = 0.5f;  // 誘電体標準 F0 = 0.08 * 0.5 = 0.04
		}

		void HeightmapChunk::RebuildSplatTexture()
		{
			if (splatData_.empty() || splatMapWidth_ == 0 || splatMapHeight_ == 0)
			{
				runtimeSplatSrv_.reset();
				mesh_.ClearTextureOverride(rendering::TextureSlot::Albedo);
				mesh_.SetMaterialFlag(graphics::MatFlag_HasSplatMap, false);
				return;
			}

			std::vector<uint8_t> pixels(static_cast<size_t>(splatMapWidth_) * splatMapHeight_ * 4);
			for (uint32_t y = 0; y < splatMapHeight_; ++y)
			{
				for (uint32_t x = 0; x < splatMapWidth_; ++x)
				{
					math::Vector4 w = splatData_[static_cast<size_t>(y) * splatMapWidth_ + x];
					NormalizeSplatWeight(w);
					const size_t idx = (static_cast<size_t>(y) * splatMapWidth_ + x) * 4;
					pixels[idx + 0] = static_cast<uint8_t>(std::clamp(w.x, 0.0f, 1.0f) * 255.0f + 0.5f);
					pixels[idx + 1] = static_cast<uint8_t>(std::clamp(w.y, 0.0f, 1.0f) * 255.0f + 0.5f);
					pixels[idx + 2] = static_cast<uint8_t>(std::clamp(w.z, 0.0f, 1.0f) * 255.0f + 0.5f);
					pixels[idx + 3] = 255u;
				}
			}

			graphics::Texture2DDesc texDesc;
			texDesc.width     = splatMapWidth_;
			texDesc.height    = splatMapHeight_;
			texDesc.mipLevels = 1;
			texDesc.format    = graphics::PixelFormat::R8G8B8A8_Unorm;

			graphics::ImageData imgData;
			imgData.pixels     = pixels.data();
			imgData.rowPitch   = splatMapWidth_ * 4;
			imgData.slicePitch = static_cast<uint32_t>(pixels.size());

			auto srv = graphics::GraphicsDevice::Get().CreateTexture2D(texDesc, imgData);
			runtimeSplatSrv_.reset(srv ? srv.release() : nullptr);
			mesh_.SetTextureOverride(rendering::TextureSlot::Albedo, runtimeSplatSrv_);
			mesh_.SetMaterialFlag(graphics::MatFlag_HasSplatMap, runtimeSplatSrv_ != nullptr);
		}


		void HeightmapChunk::FillSplatLayer(uint32_t layerIndex)
		{
			if (splatData_.empty()) return;
			layerIndex = std::min(layerIndex, 2u);
			for (auto& w : splatData_)
			{
				w.Set(layerIndex == 0 ? 1.0f : 0.0f,
				      layerIndex == 1 ? 1.0f : 0.0f,
				      layerIndex == 2 ? 1.0f : 0.0f,
				      1.0f);
			}
			RebuildSplatTexture();
		}

		void HeightmapChunk::RebuildFromHeights()
		{
			if (vertCache_.empty() || hmapWidth_ == 0) return;

			const uint32_t N        = desc_.resolution;
			const uint32_t vN       = N + 1;
			const float    cellSize = desc_.terrainSize / static_cast<float>(N);

			// Y 座標を heightData_ から再計算
			for (uint32_t zi = 0; zi < vN; ++zi)
			{
				for (uint32_t xi = 0; xi < vN; ++xi)
				{
					const float nx = static_cast<float>(xi) / N;
					const float nz = static_cast<float>(zi) / N;
					vertCache_[zi * vN + xi].position.y =
						BilinearSample(heightData_, hmapWidth_, hmapHeight_, nx, nz) * desc_.heightScale;
				}
			}

			// 法線・タンジェント再計算
			for (uint32_t zi = 0; zi < vN; ++zi)
			{
				for (uint32_t xi = 0; xi < vN; ++xi)
				{
					const uint32_t xL = (xi > 0) ? xi - 1 : xi;
					const uint32_t xR = (xi < N) ? xi + 1 : xi;
					const uint32_t zU = (zi > 0) ? zi - 1 : zi;
					const uint32_t zD = (zi < N) ? zi + 1 : zi;

					const float hL = vertCache_[zi  * vN + xL].position.y;
					const float hR = vertCache_[zi  * vN + xR].position.y;
					const float hU = vertCache_[zU  * vN + xi].position.y;
					const float hD = vertCache_[zD  * vN + xi].position.y;

					const float scaleX = static_cast<float>(xR - xL);
					const float scaleZ = static_cast<float>(zD - zU);
					const float dydx   = (hR - hL) / (scaleX * cellSize);
					const float dydz   = (hD - hU) / (scaleZ * cellSize);

					math::Vector3 n(-dydx, 1.0f, -dydz);
					n.Normalize();
					vertCache_[zi * vN + xi].normal = n;

					const float tlen = std::sqrt(1.0f + dydx * dydx);
					vertCache_[zi * vN + xi].tangent.Set(1.0f / tlen, dydx / tlen, 0.0f, 1.0f);
				}
			}

			// 動的VBへアップロード
			mesh_.UpdateVertices(vertCache_.data(), static_cast<uint32_t>(vertCache_.size()));
		}


		void HeightmapChunk::SetTerrainSize(float size)
		{
			if (size <= 0.0f || heightData_.empty()) return;
			terrainSize_      = size;
			desc_.terrainSize = size;
			ComputeVertices(heightData_, hmapWidth_, hmapHeight_, desc_, vertCache_);
			mesh_.UpdateVertices(vertCache_.data(), static_cast<uint32_t>(vertCache_.size()));
		}


		void HeightmapChunk::SetHeightScale(float scale)
		{
			if (heightData_.empty()) return;
			heightScale_      = scale;
			desc_.heightScale = scale;
			RebuildFromHeights();
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
