#include "stdafx.h"
#include "AquaDashStates.h"
#include "UI/AquaDashScreens.h"
#include "GameInput.h"
#include "GameAction.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "ECS/CoinComponentSystem.h"
#include "Component/TerrainComponent.h"
#include "Component/AnimationComponentSystem.h"
#include "Component/ParticleComponentSystem.h"
#include "Component/InstancedStaticMeshComponentSystem.h"
#include "Component/InstancedPointListComponentSystem.h"
#include <cstdio>
#include "Terrain/HeightmapChunk.h"
#include "Graphics/MeshPrimitives.h"
#include "Level/LevelManager.h"
#include "Sound/SoundClip.h"
#include "Sound/SoundEngine.h"
#include "Util/ThreadPool.h"
#ifdef AQ_DEBUG_IMGUI
#include "ECS/EntityDebugTag.h"
#endif


namespace app
{
	namespace aquadash
	{
		namespace
		{
			/** メニュー項目 (ResultScreen の表示順と一致させる) */
			static constexpr int MENU_RETRY = 0;
			static constexpr int MENU_NEXT  = 1;
			static constexpr int MENU_TITLE = 2;

			// スティック上下をカーソル移動として拾うしきい値。
			static constexpr float STICK_EDGE = 0.5f;

			// ローディング表示の最低時間 (演出用) とワールド生成前の描画フレーム数。
			static constexpr float MIN_LOADING_SEC   = 0.6f;
			static constexpr int   WARMUP_FRAME_COUNT = 2;

			// アセットパス。
			static const char* STAGE_LIST_PATH  = "Assets/Stages/StageList.json";
			static const char* DECISION_SE_PATH = "Assets/Sound/Decision.wav";
			static const char* PLAYER_MODEL_PATH = "Assets/unityChan.tkm";
			static const char* PLAYER_IDLE_ANIM  = "Assets/animData/idle.tka";

			// unityChan.tkm はメートル基準でない (素のままだと約 6m)。世界は 1m=1.0 なので縮めて使う。
			static constexpr float PLAYER_MODEL_SCALE = 0.25f;

			// 路面タイルをベイクするときの XZ セルの 1 辺 [m]。
			// 小さいほど判定は細かくなるが、セル数 (判定回数) が増える。
			static constexpr float ROAD_TILE_CELL_SIZE = 32.0f;

			// コイン取得エフェクト (常駐エミッタを移動+Restart で使い回す)。
			static const char* COLLECT_FX_PATH = "Assets/Particle/FX_Explosion.particle";

			// リングコイン (トーラス) の寸法。外径 0.9m = (中心半径 + 管半径) × 2。
			// 寸法はメッシュへ焼き込むので、インスタンス点の scale は等倍で使う。
			static constexpr float COIN_RING_CENTER_RADIUS = 0.36f;   // 中心円の半径 [m]
			static constexpr float COIN_RING_TUBE_RADIUS   = 0.09f;   // 管の半径 [m]
			static constexpr int   COIN_RING_SEGMENT_COUNT = 24;      // 中心円まわりの分割数
			static constexpr int   COIN_RING_SIDE_COUNT    = 12;      // 管断面の分割数
			static constexpr float COIN_RING_TWO_PI        = 6.28318530718f;

			// 草 (P12-2: 見た目と風の確認用に 1000 本程度。散布規則の作り込みは P12-3)。
			static constexpr uint32_t GRASS_QUAD_COUNT      = 3;        // 房を構成する交差クアッドの枚数
			static constexpr float GRASS_HEIGHT             = 0.5f;     // 房の高さ [m]
			static constexpr float GRASS_WIDTH              = 0.12f;    // 房の根元の幅 [m]
			static constexpr float GRASS_CELL_SIZE          = 32.0f;    // ベイクする XZ セルの 1 辺 [m]
			static constexpr float GRASS_DRAW_DISTANCE      = 120.0f;   // セル AABB までの最大描画距離 [m]
			static constexpr int   GRASS_TARGET_COUNT       = 1000;     // 置きたいおおよその本数
			static constexpr int   GRASS_PER_STEP_COUNT     = 3;        // スプラインの 1 ステップあたりの本数
			static constexpr float GRASS_BAND_HALF_WIDTH    = 40.0f;    // コース中心からの横方向の帯 [m]
			static constexpr float GRASS_ROAD_MARGIN        = 2.0f;     // 路面端からさらに空ける余白 [m]

			// 風。強め (0.06m) にすると 0.5m の房でも先端の動きがはっきり見える。
			static constexpr float GRASS_WIND_STRENGTH      = 0.06f;
			static constexpr float GRASS_WIND_FREQUENCY     = 1.7f;

			// 基準の緑。per-instance で明度と色相を少しずつ散らす。
			static constexpr float GRASS_BASE_R             = 0.22f;
			static constexpr float GRASS_BASE_G             = 0.46f;
			static constexpr float GRASS_BASE_B             = 0.16f;

			// ハッシュ乱数のシードを作るときの XZ 量子化 (1m あたりの分割数)。
			static constexpr float GRASS_SEED_QUANTIZE      = 4.0f;
			static constexpr float GRASS_TWO_PI             = 6.28318530718f;


			// 整数座標から 0-1 の決定的な擬似乱数を作る (SplatmapPainter の HashNoise と同じ式)。
			// 位置をシードにするので、再ロードしても同じ見た目になる。
			float HashNoise(const int x, const int y)
			{
				uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
				h = (h ^ (h >> 13)) * 1274126177u;
				h ^= h >> 16;
				return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
			}


			// 決定 SE を再生する (クリップは GameFlow::Update で先読み済み)。
			void PlayDecisionSE()
			{
				if (aq::sound::SoundEngine::IsAvailable()) {
					auto clip = aq::res::ResourceManager::Get().Load<aq::sound::SoundClip>(DECISION_SE_PATH);
					aq::sound::SoundEngine::Get().Play(clip, aq::sound::SoundBusId::SE);
				}
			}


			// リングコインのトーラスメッシュを生成する。XY 平面のリングで穴が Z を向く
			// (旧・箱コインと同じ姿勢なので、CoinComponent の baseRotation をそのまま使える)。
			// UV は使わないため、シームは頂点を複製した (24+1)x(12+1) 格子で単純に作る。
			void BuildCoinRingMesh(std::vector<aq::graphics::VertexData>& outVertices,
			                       std::vector<uint32_t>& outIndices)
			{
				const int ringCount = COIN_RING_SEGMENT_COUNT + 1;
				const int sideCount = COIN_RING_SIDE_COUNT + 1;

				outVertices.clear();
				outVertices.reserve(static_cast<size_t>(ringCount) * static_cast<size_t>(sideCount));
				for (int i = 0; i < ringCount; ++i)
				{
					const float theta    = COIN_RING_TWO_PI * static_cast<float>(i) / static_cast<float>(COIN_RING_SEGMENT_COUNT);
					const float cosTheta = cosf(theta);
					const float sinTheta = sinf(theta);
					for (int j = 0; j < sideCount; ++j)
					{
						const float phi    = COIN_RING_TWO_PI * static_cast<float>(j) / static_cast<float>(COIN_RING_SIDE_COUNT);
						const float cosPhi = cosf(phi);
						const float sinPhi = sinf(phi);
						const float radius = COIN_RING_CENTER_RADIUS + COIN_RING_TUBE_RADIUS * cosPhi;

						aq::graphics::VertexData vertex;
						vertex.position.Set(radius * cosTheta, radius * sinTheta, COIN_RING_TUBE_RADIUS * sinPhi);
						vertex.normal.Set(cosPhi * cosTheta, cosPhi * sinTheta, sinPhi);
						vertex.uv.Set(0.0f, 0.0f);
						vertex.tangent.Set(0.0f, 0.0f, 0.0f, 0.0f);
						outVertices.push_back(vertex);
					}
				}

				// 巻き順は (中心円方向, 管断面方向) の外積が外向き法線と同符号になる並び = 表面。
				outIndices.clear();
				outIndices.reserve(static_cast<size_t>(COIN_RING_SEGMENT_COUNT) * static_cast<size_t>(COIN_RING_SIDE_COUNT) * 6);
				for (int i = 0; i < COIN_RING_SEGMENT_COUNT; ++i)
				{
					for (int j = 0; j < COIN_RING_SIDE_COUNT; ++j)
					{
						const uint32_t v00 = static_cast<uint32_t>(i * sideCount + j);
						const uint32_t v10 = static_cast<uint32_t>((i + 1) * sideCount + j);
						const uint32_t v11 = static_cast<uint32_t>((i + 1) * sideCount + j + 1);
						const uint32_t v01 = static_cast<uint32_t>(i * sideCount + j + 1);

						outIndices.push_back(v00);
						outIndices.push_back(v10);
						outIndices.push_back(v01);
						outIndices.push_back(v10);
						outIndices.push_back(v11);
						outIndices.push_back(v01);
					}
				}
			}


			// 地形 + プレイヤー + 自動カメラを生成する (ロード完了時に一度だけ)。
			// 生成した Entity はタイトル復帰時の破棄用に context.stageEntities へ積む。
			/** コースの XZ 範囲 (スプラインを 10m 刻みで粗くサンプリング)。地面サイズとミニマップ正規化に使う */
			struct CourseExtents
			{
				float minX = 0.0f, maxX = 0.0f, minZ = 0.0f, maxZ = 0.0f;
			};

			CourseExtents ComputeCourseExtents(const stage::StageData& stageData)
			{
				CourseExtents e;
				const float total = stageData.spline.GetTotalLength();
				for (float d = 0.0f; d <= total; d += 10.0f) {
					const auto p = stageData.spline.Evaluate(d).position;
					if (p.x < e.minX) { e.minX = p.x; }
					if (p.x > e.maxX) { e.maxX = p.x; }
					if (p.z < e.minZ) { e.minZ = p.z; }
					if (p.z > e.maxZ) { e.maxZ = p.z; }
				}
				return e;
			}

			// 地面の外周マージン。原点と terrainSize の式はベイク画像の座標系と対になっているので変えないこと。
			constexpr float TERRAIN_MARGIN = 60.0f;

			/**
			 * 地形 Desc を組む。ステージが terrain を指定していればベイク済みのハイトマップ地形、
			 * 未指定なら従来どおりの平坦 grass になる。
			 * 文字列ポインタは stageData の std::string / 静的リテラルを指すので、stageData が生きている間だけ有効。
			 * ワーカースレッド側の PrepareCpuData とメインスレッド側の SetDesc の両方から同じ Desc を得るために分離した。
			 */
			aq::terrain::HeightmapChunk::Desc MakeTerrainDesc(const stage::StageData& stageData, const CourseExtents& ext)
			{
				const float extentX = ext.maxX - ext.minX + TERRAIN_MARGIN * 2.0f;
				const float extentZ = ext.maxZ - ext.minZ + TERRAIN_MARGIN * 2.0f;

				aq::terrain::HeightmapChunk::Desc desc;
				desc.heightmapPath = "Assets/Terrain/heightmap.png";
				desc.splatmapPath  = "Assets/Terrain/splatmap.png";
				desc.layerPaths[0] = "Assets/Terrain/grass.DDS";
				desc.layerPaths[1] = "Assets/Terrain/snow.DDS";
				desc.layerPaths[2] = "Assets/Terrain/rock.DDS";
				desc.resolution    = stageData.terrainResolution;
				desc.heightScale   = stageData.terrainHeightScale;   // 0 なら平坦
				desc.terrainSize   = extentX > extentZ ? extentX : extentZ;
				desc.layerTiling   = desc.terrainSize / 5.0f;

				if (!stageData.terrainHeightmapPath.empty()) {
					desc.heightmapPath = stageData.terrainHeightmapPath.c_str();
				}
				if (!stageData.terrainSplatmapPath.empty()) {
					desc.splatmapPath = stageData.terrainSplatmapPath.c_str();
				}
				return desc;
			}

			/**
			 * ステージワールドを生成する (メインスレッド)。
			 * preparedTerrain はワーカーで PrepareCpuData 済みの地形データ。null なら同期で作る
			 * (画像デコード+頂点生成が乗るので Debug では 200ms 超のヒッチになる)。
			 */
			void CreateStageWorld(GameFlow& flow, const std::shared_ptr<stage::StageData>& stageData,
			                      aq::terrain::HeightmapChunk::CpuData* preparedTerrain)
			{
				auto& ctx     = aq::ecs::EntityContext::Get();
				auto& context = flow.Context();
				context.stageEntities.clear();

				const CourseExtents ext = ComputeCourseExtents(*stageData);
				const float minX = ext.minX, maxX = ext.maxX, minZ = ext.minZ, maxZ = ext.maxZ;

				// 草の高さ引きに使う地形の原点とチャンク (地面ブロックで確定する)。
				aq::math::Vector3              terrainOrigin(0.0f, 0.0f, 0.0f);
				aq::terrain::HeightmapChunk*   terrainChunk = nullptr;

				// 地面。
				{
					const aq::terrain::HeightmapChunk::Desc desc = MakeTerrainDesc(*stageData, ext);

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent, aq::ecs::HierarchicalTransformComponent, aq::ecs::TerrainComponent>();
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					// Y はステージ指定のオフセット (R=0 の高さ)。ベイク済みハイトマップは
					// 路面の沈み (スプラインの y<0) を offset 起点の正値で表現している。
					tc->position.Set(minX - TERRAIN_MARGIN, stageData->terrainHeightOffset, minZ - TERRAIN_MARGIN);
					auto* terrain = entity.GetComponent<aq::ecs::TerrainComponent>();
					if (preparedTerrain != nullptr) {
						terrain->SetDesc(desc, std::move(*preparedTerrain));
					} else {
						terrain->SetDesc(desc);
					}
					terrain->GetChunk()->SetReceiveShadow(true);
					// GetHeight は地形ローカル XZ を取り、エンティティの Y オフセットを含まない高さを返す。
					// 草の配置で使うので、原点 (= TransformComponent の位置) とチャンクを控えておく。
					terrainOrigin = tc->position;
					terrainChunk  = terrain->GetChunk();
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("StageGround");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}
				aq::StartupMark("[load]   terrain entity done");

				// メインカメラの基本設定。
				{
					aq::Camera* const mainCamera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
					mainCamera->SetNear(0.1f);
					mainCamera->SetFar(3000.0f);   // 長距離コースの見通し (既定 1000 だと途中で切れる)
					mainCamera->SetViewportSize(
						static_cast<float>(aq::Engine::Get().GetRenderWidth()),
						static_cast<float>(aq::Engine::Get().GetRenderHeight()));
				}

				// ミニマップの正規化パラメータ (コース XZ 範囲 → 0-1 への変換に使う)。
				{
					const float extentX = maxX - minX;
					const float extentZ = maxZ - minZ;
					context.minimapCenterXZ   = aq::math::Vector2((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
					context.minimapHalfExtent = (extentX > extentZ ? extentX : extentZ) * 0.5f + 40.0f;
				}

				// 路面タイル (スプラインに沿った薄い箱)。走行時の路面の見た目と、
				// ミニマップ (俯瞰) に映るコース形状を兼ねる。ループでもタイル姿勢が路面に追従する。
				{
					// 全タイルを 1 エンティティのインスタンス描画にまとめる (約360枚=1ドロー)。
					// per-instance フラスタムカリングは gather 側 (RenderSystem) が行う。
					// 20m 間隔 (約360枚) はミニマップ形状とのバランスで維持。
					constexpr float TILE_SPACING = 20.0f;
					auto* roadMesh = aq::ecs::BoxStaticMeshComponent::RegisterInstancedMesh("RoadTile");

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::InstancedStaticMeshComponent,
						aq::ecs::InstancedPointListComponent>();
					entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh("RoadTile");

					auto* pointList = entity.GetComponent<aq::ecs::InstancedPointListComponent>();
					const float total = stageData->spline.GetTotalLength();
					pointList->ReserveInstancePoints(static_cast<size_t>(total / TILE_SPACING) + 1);
					for (float d = 0.0f; d < total; d += TILE_SPACING)
					{
						const auto frame = stageData->spline.Evaluate(d);
						aq::ecs::InstancePoint p;
						// 地形面 (y=0) より上面がわずかに出るよう -0.10 (厚み 0.3 → 上面 +0.05)。
						// 深く沈めると平坦地形に埋まって見えなくなる。
						p.position = frame.position - frame.up * 0.1f;
						p.rotation = frame.ToRotation();
						p.scale.Set(stageData->width, 0.3f, TILE_SPACING * 1.02f);
						// 路面は青みグレー (仮アセット。専用モデル導入までの色分け)。
						p.color = aq::math::Vector4(0.30f, 0.34f, 0.42f, 1.0f);
						pointList->AddInstancePoint(p);
					}

					// 路面タイルは配置後に一切動かない静的オブジェクトなので、行列を焼き込んで
					// セル単位でカリングする「ベイク経路」に載せる (ベイク経路の検証ケースも兼ねる)。
					// エンティティは既定変換のままなので、織り込むワールド行列は単位行列でよい。
					// maxDrawDistance は既定 (無制限) のまま — ミニマップのコース形状に遠方タイルが要る。
					if (roadMesh != nullptr) {
						pointList->BakeStatic(aq::math::Matrix4x4::Identity, roadMesh->GetLocalBounds(),
						                      ROAD_TILE_CELL_SIZE);
					}
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("RoadTiles");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}
				aq::StartupMark("[load]   road tiles done");

				// 草 (手続き生成の房 + 風揺れ)。この段はコース沿いに 1000 本程度置いて見た目と
				// 風の動きを確認するだけで、散布規則 (スプラットマスク/傾斜除外/ワーカー/10万本) は次段。
				{
					// 房メッシュはここで一度だけ生成する。同名が登録済みならそれが返るので、
					// RETRY やステージ再入場で作り直しにはならない。
					std::vector<aq::graphics::VertexData> grassVertices;
					std::vector<uint32_t>                 grassIndices;
					aq::graphics::BuildGrassTuftMesh(grassVertices, grassIndices,
					                                 GRASS_QUAD_COUNT, GRASS_HEIGHT, GRASS_WIDTH);

					auto* grassMesh = aq::graphics::InstancedStaticMesh::RegisterFromData(
						"Grass",
						grassVertices.data(), static_cast<uint32_t>(grassVertices.size()), sizeof(aq::graphics::VertexData),
						grassIndices.data(),  static_cast<uint32_t>(grassIndices.size()),
						aq::graphics::StaticMesh::ShaderType::InstancedGrass);
					if (grassMesh != nullptr) {
						// ローカル AABB が無いとセル AABB が点になりカリングが一切効かないので必ず持たせる。
						grassMesh->SetLocalBounds(aq::math::AABB(
							aq::math::Vector3(0.0f, GRASS_HEIGHT * 0.5f, 0.0f),
							aq::math::Vector3(GRASS_WIDTH * 0.5f, GRASS_HEIGHT * 0.5f, GRASS_WIDTH * 0.5f)));
						grassMesh->SetWindParams(GRASS_WIND_STRENGTH, GRASS_WIND_FREQUENCY,
						                         aq::math::Vector3(1.0f, 0.0f, 0.35f));
					}

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::InstancedStaticMeshComponent,
						aq::ecs::InstancedPointListComponent>();
					entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh("Grass");

					auto* pointList = entity.GetComponent<aq::ecs::InstancedPointListComponent>();
					pointList->ReserveInstancePoints(static_cast<size_t>(GRASS_TARGET_COUNT));

					// スプラインを一定間隔で歩き、1 ステップにつき数本を帯の中の乱数横位置へ置く。
					// 路面上 (|lateral| < 路面半幅 + マージン) に当たったぶんは捨てる。
					const float total         = stageData->spline.GetTotalLength();
					const int   stepCount     = (GRASS_TARGET_COUNT >= GRASS_PER_STEP_COUNT)
					                          ? GRASS_TARGET_COUNT / GRASS_PER_STEP_COUNT : 1;
					const float step          = total / static_cast<float>(stepCount);
					const float roadHalfWidth = stageData->width * 0.5f + GRASS_ROAD_MARGIN;
					for (int s = 0; s < stepCount; ++s)
					{
						const auto frame = stageData->spline.Evaluate(step * static_cast<float>(s));
						for (int k = 0; k < GRASS_PER_STEP_COUNT; ++k)
						{
							const float lateral = (HashNoise(s, k) * 2.0f - 1.0f) * GRASS_BAND_HALF_WIDTH;
							if (fabsf(lateral) < roadHalfWidth) { continue; }   // 路面の上には生やさない

							// 高さは地形から取る。GetHeight は地形ローカル XZ を受け、エンティティの
							// Y オフセットを含まない高さを返すので、原点 Y を足して戻す。
							const aq::math::Vector3 base = frame.position + frame.right * lateral;
							const float worldX = base.x;
							const float worldZ = base.z;
							const float worldY = terrainOrigin.y
							                   + ((terrainChunk != nullptr)
							                      ? terrainChunk->GetHeight(worldX - terrainOrigin.x, worldZ - terrainOrigin.z)
							                      : 0.0f);

							// 回転・スケール・色は「量子化したワールド XZ」のハッシュから作る。
							// 位置が同じなら常に同じ値になるので、再ロードで見た目が揺れない。
							const int ix = static_cast<int>(floorf(worldX * GRASS_SEED_QUANTIZE));
							const int iz = static_cast<int>(floorf(worldZ * GRASS_SEED_QUANTIZE));
							const float rotRand    = HashNoise(ix, iz);
							const float scaleRand  = HashNoise(ix + 131, iz);
							const float brightRand = HashNoise(ix, iz + 197);
							const float hueRand    = HashNoise(ix + 263, iz + 311);

							// 明度と色相 (黄寄り / 青寄り) を少し散らして、群れの単調さを消す。
							const float bright = 0.75f + 0.40f * brightRand;
							const float hue    = (hueRand - 0.5f) * 0.10f;

							aq::ecs::InstancePoint p;
							p.position.Set(worldX, worldY, worldZ);
							p.rotation.SetRotation(aq::math::Vector3(0.0f, 1.0f, 0.0f), rotRand * GRASS_TWO_PI);
							p.scale.Set(0.8f + 0.5f * scaleRand);
							p.color = aq::math::Vector4((GRASS_BASE_R + hue)        * bright,
							                            GRASS_BASE_G               * bright,
							                            (GRASS_BASE_B - hue * 0.5f) * bright,
							                            1.0f);
							pointList->AddInstancePoint(p);
						}
					}

					// 路面タイルと同じく、配置後に動かない静的オブジェクトなのでベイク経路に載せる。
					// エンティティは既定変換のままなので、織り込むワールド行列は単位行列でよい。
					if (grassMesh != nullptr) {
						pointList->BakeStatic(aq::math::Matrix4x4::Identity, grassMesh->GetLocalBounds(),
						                      GRASS_CELL_SIZE);
					}
					// 草は小さいので遠景では見えない。路面と違いミニマップにも要らないので距離で切る。
					pointList->SetMaxDrawDistance(GRASS_DRAW_DISTANCE);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Grass");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}
				aq::StartupMark("[load]   grass done");

				// プレイヤー。
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::SkeletalMeshComponent,
						aq::ecs::AnimationComponent,
						app::ecs::SpeedCharacterComponent,
						app::ecs::PlayerInputComponent,
						app::ecs::PlayerScoreComponent>();

					auto* character = entity.GetComponent<app::ecs::SpeedCharacterComponent>();
					character->distance = stageData->spawnDistance;
					character->lateral  = stageData->spawnLanes.empty() ? 0.0f : stageData->spawnLanes[0];

					const auto spawnFrame = stageData->spline.Evaluate(character->distance);
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					tc->position = spawnFrame.position + spawnFrame.right * character->lateral;
					tc->scale.Set(PLAYER_MODEL_SCALE);

					auto* skelComp = entity.GetComponent<aq::ecs::SkeletalMeshComponent>();
					skelComp->SetShaderType(aq::graphics::SkeletalMesh::ShaderType::SkeletalPBRLit);
					skelComp->SetModelPath(PLAYER_MODEL_PATH);
					skelComp->GetSkeletalMesh()->SetCastShadow(true);
					skelComp->GetSkeletalMesh()->SetReceiveShadow(true);
					skelComp->GetSkeletalMesh()->SetReceivesDecal(false);

					auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
					animComp->AddAnimation(aqHash32("idle"), PLAYER_IDLE_ANIM);
					animComp->Play(aqHash32("idle"), true);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("SpeedPlayer");
#endif
					context.playerHandle = entity.GetHandle();
					context.stageEntities.push_back(entity.GetHandle());
				}
				flow.SetPlayerHandle(context.playerHandle);   // 影の注視点用
				aq::StartupMark("[load]   player done");

				// 自動カメラ。
				{
					auto entity = ctx.CreateEntity<app::ecs::AutoCameraComponent>();
					auto* autoCam = entity.GetComponent<app::ecs::AutoCameraComponent>();
					autoCam->targetHandle = context.playerHandle;
					autoCam->cameraType   = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("AutoCamera");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}

				// コイン (スプライン座標 → ワールドへ焼き込み。判定と回転は CoinSystem)。
				// メッシュは持たせず、描画は下の Coins エンティティのインスタンス点として出す。
				for (const auto& placement : stageData->coins)
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						app::ecs::CoinComponent>();

					const auto frame = stageData->spline.Evaluate(placement.distance);
					auto* tc = entity.GetComponent<aq::ecs::TransformComponent>();
					tc->position = frame.position + frame.right * placement.lateral + frame.up * placement.height;

					auto* coin = entity.GetComponent<app::ecs::CoinComponent>();
					coin->distance     = placement.distance;
					coin->baseRotation = frame.ToRotation();
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Coin");
#endif
					context.stageEntities.push_back(entity.GetHandle());
				}

				aq::StartupMarkf("[load]   coin entities done (%zu)", stageData->coins.size());

				// コインの描画をまとめる 1 エンティティ。点の中身は CoinSystem が毎フレーム再構築する
				// (取得済みを除いたぶんだけ積むので、取得すれば次の再構築で消える)。
				{
					// リング形状はここで一度だけ生成する。同名が登録済みならそれが返るので、
					// RETRY やステージ再入場で作り直しにはならない。
					std::vector<aq::graphics::VertexData> ringVertices;
					std::vector<uint32_t>                 ringIndices;
					BuildCoinRingMesh(ringVertices, ringIndices);

					auto* ringMesh = aq::graphics::InstancedStaticMesh::RegisterFromData(
						"CoinRing",
						ringVertices.data(), static_cast<uint32_t>(ringVertices.size()), sizeof(aq::graphics::VertexData),
						ringIndices.data(),  static_cast<uint32_t>(ringIndices.size()),
						aq::graphics::StaticMesh::ShaderType::InstancedSimple);
					if (ringMesh != nullptr) {
						// 寸法をメッシュへ焼き込んでいるので、カリング用 AABB も実寸で持たせる。
						ringMesh->SetLocalBounds(aq::math::AABB(
							aq::math::Vector3(0.0f, 0.0f, 0.0f),
							aq::math::Vector3(0.45f, 0.45f, 0.09f)));
					}

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::InstancedStaticMeshComponent,
						aq::ecs::InstancedPointListComponent>();
					entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh("CoinRing");
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Coins");
#endif
					context.coinInstancesHandle = entity.GetHandle();
					context.stageEntities.push_back(entity.GetHandle());
				}
				aq::StartupMark("[load]   coin ring mesh done");

				// コイン取得エフェクトの常駐エミッタ (取得時に移動して Restart する)。
				{
					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::ParticleEmitterComponent>();
					auto* emitter = entity.GetComponent<aq::ecs::ParticleEmitterComponent>();
					emitter->SetAsset(COLLECT_FX_PATH);
					emitter->SetPlaying(false);   // 生成直後に鳴らない
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("CollectFX");
#endif
					context.collectFxHandle = entity.GetHandle();
					context.stageEntities.push_back(entity.GetHandle());
				}
			}


			// プレイヤーをスポーン状態へ戻す (「もう一度」のロードなし再開用)。
			void ResetPlayers(GameFlow& flow)
			{
				auto& context = flow.Context();
				const auto stageData = context.activeStage;
				if (!stageData) { return; }

				auto& ctx = aq::ecs::EntityContext::Get();
				if (ctx.IsValid(context.playerHandle)) {
					if (auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandle)) {
						character->distance         = stageData->spawnDistance;
						character->lateral          = stageData->spawnLanes.empty() ? 0.0f : stageData->spawnLanes[0];
						character->height           = 0.0f;
						character->speed            = 0.0f;
						character->verticalVelocity = 0.0f;
						character->grounded         = true;
						character->fallen           = false;
						character->worldVelocity    = aq::math::Vector3(0.0f, 0.0f, 0.0f);
					}
					if (auto* score = ctx.GetComponent<app::ecs::PlayerScoreComponent>(context.playerHandle)) {
						score->coinCount = 0;
						score->fallCount = 0;
					}
				}
				context.playResult = PlayResult();

				// コインを全復活させる (取得済みフラグと表示を戻す)。
				app::ecs::CoinSystem::ReactivateAll();

				// カメラは次フレームでスナップし直す。
				aq::ecs::Foreach<app::ecs::AutoCameraComponent>(
					[](const aq::ecs::Entity&, app::ecs::AutoCameraComponent* autoCam)
					{
						autoCam->initialized = false;
					});
			}


			// 生成したステージワールドと Level を破棄する (タイトル復帰時)。
			void DestroyStageWorld(GameFlow& flow)
			{
				auto& ctx     = aq::ecs::EntityContext::Get();
				auto& context = flow.Context();
				for (const auto& handle : context.stageEntities) {
					if (ctx.IsValid(handle)) {
						ctx.RequestDestroyEntity(handle);
					}
				}
				context.stageEntities.clear();
				context.playerHandle        = aq::ecs::EntityHandle();
				context.collectFxHandle     = aq::ecs::EntityHandle();
				context.coinInstancesHandle = aq::ecs::EntityHandle();
				context.activeStage.reset();

				if (flow.LoadHandle().IsValid()) {
					aq::level::LevelManager::Get().Unload(flow.LoadHandle().GetLevelId());
					flow.SetLoadHandle(aq::level::LevelLoadHandle());
				}
			}
		}


		/**
		 * タイトル
		 */
		void TitleState::OnEnter(GameFlow& flow)
		{
			// ステージ一覧は初回のみ読む (小さな JSON なので同期でよい)。
			if (flow.Context().stageList.empty()) {
				flow.Context().stageList = stage::StageRegistry::LoadList(STAGE_LIST_PATH);
			}

			// 選択中ステージ名をタイトルへ反映する。
			const auto& list = flow.Context().stageList;
			if (!list.empty()) {
				const int index = aq::math::Clamp(flow.Context().selectedStageIndex, 0, static_cast<int>(list.size()) - 1);
				if (auto* screen = static_cast<TitleScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "STAGE %02d    %s", index + 1, list[index].name.c_str());
					screen->SetStageName(buf);
				}
			}
		}


		void TitleState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			if (!GameInput::Get().IsTriggered(GameAction::Confirm)) { return; }
			if (flow.Context().stageList.empty()) { return; }   // 一覧が無ければ開始できない

			PlayDecisionSE();

			flow.Context().playResult = PlayResult();
			aq::ui::UIContext::Get().Screens().Replace("Loading");
			flow.ChangeState(std::make_unique<LoadingState>());
		}


		/************************************/




		/**
		 * ローディング
		 */
		void LoadingState::OnEnter(GameFlow& flow)
		{
			phase_        = Phase::WarmUp;
			warmupFrames_ = 0;
			timer_        = 0.0f;

			const auto& context = flow.Context();
			const int index = context.selectedStageIndex;
			stagePath_ = context.stageList[index >= 0 && index < static_cast<int>(context.stageList.size()) ? index : 0].stagePath;
			aq::StartupMarkf("[load] LoadingState enter (%s)", stagePath_.c_str());
		}


		void LoadingState::OnUpdate(GameFlow& flow, const float dt)
		{
			timer_ += dt;

			switch (phase_)
			{
			case Phase::WarmUp:
				// ローディング画面を数フレーム描画してから重い処理へ (ドットアニメを止めないため)。
				if (++warmupFrames_ >= WARMUP_FRAME_COUNT) { phase_ = Phase::ParseStage; }
				break;

			case Phase::ParseStage:
			{
				// ステージ定義のパースと地形の CPU 前計算 (画像デコード/頂点生成/画素変換) はワーカースレッドで行う。
				// GPU リソース生成だけをメインスレッド (CreateStageWorld) に残す。
				const std::string path = stagePath_;
				stageFuture_ = aq::util::ThreadPool::Get().Submit([path]()
					{
						StageLoadResult result;
						result.stage = stage::StageData::LoadFromFile(path.c_str());
						aq::StartupMark("[load] stage json parsed (worker)");
						if (result.stage) {
							const CourseExtents ext = ComputeCourseExtents(*result.stage);
							result.terrainCpu = aq::terrain::HeightmapChunk::PrepareCpuData(MakeTerrainDesc(*result.stage, ext));
							aq::StartupMark("[load] terrain cpu data prepared (worker)");
						}
						return result;
					});
				phase_ = Phase::WaitStage;
				break;
			}

			case Phase::WaitStage:
			{
				if (stageFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { break; }

				StageLoadResult result = stageFuture_.get();
				auto stageData = result.stage;
				EngineAssertMsg(stageData != nullptr, "ステージ定義の読み込みに失敗");
				if (!stageData) {
					// 読めない場合はタイトルへ戻す。
					aq::ui::UIContext::Get().Screens().Replace("AquaDashTitle");
					flow.ChangeState(std::make_unique<TitleState>());
					break;
				}

				flow.Context().activeStage = stageData;
				aq::StartupMark("[load] worker result received");
				CreateStageWorld(flow, stageData, &result.terrainCpu);   // GPU 生成のみ (CPU 前計算はワーカー済み)
				aq::StartupMark("[load] CreateStageWorld done (terrain/road/player/coins, sync)");

				// 見た目 Level の非同期ロードを開始する。
				if (!stageData->levelPath.empty()) {
					flow.SetLoadHandle(aq::level::LevelManager::Get().LoadAsync(stageData->levelPath));
				}
				phase_ = Phase::Streaming;
				break;
			}

			case Phase::Streaming:
				if (flow.LoadHandle().IsDone() && !levelDoneLogged_)
				{
					levelDoneLogged_ = true;
					aq::StartupMark("[load] level LoadAsync done");
				}
				if (flow.LoadHandle().IsDone() && timer_ >= MIN_LOADING_SEC)
				{
					aq::StartupMarkf("[load] -> InGame (loading %.2f s)", timer_);
					aq::ui::UIContext::Get().Screens().Replace("AquaDashInGame");
					flow.ChangeState(std::make_unique<InGameState>());
				}
				break;
			}
		}


		/************************************/




		/**
		 * インゲーム
		 */
		void InGameState::OnEnter(GameFlow& flow)
		{
			elapsed_ = 0.0f;
			flow.Context().gameplayPaused = false;
			ResetPlayers(flow);

			// ミニマップ: コース形状をスプラインから等間隔サンプリングし、UI の点列として描く。
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				std::vector<aq::math::Vector2> uvPoints;
				const auto& context   = flow.Context();
				const auto  stageData = context.activeStage;
				if (stageData && stageData->spline.IsValid() && context.minimapHalfExtent > 1.0f)
				{
					constexpr int SAMPLE_COUNT = 160;
					const float span  = context.minimapHalfExtent * 2.0f;
					const float total = stageData->spline.GetTotalLength();
					uvPoints.reserve(SAMPLE_COUNT + 1);
					for (int i = 0; i <= SAMPLE_COUNT; ++i)
					{
						const auto position =
							stageData->spline.Evaluate(total * static_cast<float>(i) / SAMPLE_COUNT).position;
						uvPoints.push_back(aq::math::Vector2(
							0.5f + (position.x - context.minimapCenterXZ.x) / span,
							0.5f - (position.z - context.minimapCenterXZ.y) / span));
					}
				}
				screen->SetMinimapCourse(uvPoints);
			}
		}


		void InGameState::OnUpdate(GameFlow& flow, const float dt)
		{
			elapsed_ += dt;

			auto& context = flow.Context();
			const auto stageData = context.activeStage;
			if (!stageData) { return; }

			auto& ctx = aq::ecs::EntityContext::Get();
			if (!ctx.IsValid(context.playerHandle)) { return; }
			const auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(context.playerHandle);
			if (!character) { return; }

			// HUD 更新 (時間 / コイン / 速度 / ミニマップマーカー)。
			const auto* score    = ctx.GetComponent<app::ecs::PlayerScoreComponent>(context.playerHandle);
			const auto* playerTc = ctx.GetComponent<aq::ecs::TransformComponent>(context.playerHandle);
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				screen->SetHUD(elapsed_, score ? score->coinCount : 0, character->speed * 3.6f);

				// 俯瞰カメラは 画面右=+X / 画面上=+Z。UI の v は下+なので Z を反転する。
				if (playerTc && context.minimapHalfExtent > 1.0f) {
					const float span = context.minimapHalfExtent * 2.0f;
					const float u = 0.5f + (playerTc->position.x - context.minimapCenterXZ.x) / span;
					const float v = 0.5f - (playerTc->position.z - context.minimapCenterXZ.y) / span;
					screen->SetMinimapMarker(u, v);
				}
			}

			// ゴール / 落下判定。
			// 落下は「路面相対 height がしきい値未満」または「ループ脱落後に地面高さまで落ちた」。
			const bool  goal     = character->distance >= stageData->goalDistance;
			const bool  fall     = character->height < stageData->fallHeight
			                    || (character->fallen && playerTc && playerTc->position.y < 0.5f);
			if (!goal && !fall) { return; }

			PlayResult& result  = context.playResult;
			result.cleared      = goal;
			result.clearTimeSec = elapsed_;
			result.coinCount    = score ? score->coinCount : 0;

			aq::ui::UIContext::Get().Screens().Replace("AquaDashResult");
			flow.ChangeState(std::make_unique<ResultState>());
		}


		/************************************/




		/**
		 * リザルト
		 */
		void ResultState::OnEnter(GameFlow& flow)
		{
			cursor_     = 0;
			prevStickY_ = 0.0f;

			// 走行と判定を停止する。描画/アニメ/カメラは動き続けるため背景は生きたまま。
			flow.Context().gameplayPaused = true;

			// Replace 済みの最前面がリザルト画面。結果と初期カーソルを反映する。
			if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				const auto&       context = flow.Context();
				const PlayResult& result  = context.playResult;

				// ランクはクリア時のみ (設計 03: ゲームオーバーはランクなし)。
				std::string rank;
				if (result.cleared && context.activeStage) {
					rank = context.activeStage->CalcRank(result.coinCount, result.clearTimeSec);
				}
				screen->SetResult(result.cleared, result.clearTimeSec, result.coinCount, rank.c_str());
				screen->SetCursor(cursor_);
			}
		}


		void ResultState::OnUpdate(GameFlow& flow, const float /*dt*/)
		{
			auto& input = GameInput::Get();

			// カーソル移動: W/S・D-Pad 上下のトリガ + 左スティック上下のエッジ検出。
			int move = 0;
			if (input.IsTriggered(GameAction::MoveBackward)) { move = +1; }
			if (input.IsTriggered(GameAction::MoveForward))  { move = -1; }

			const float stickY = input.GetStick(GameAction::Move).y;
			if (prevStickY_ < STICK_EDGE && stickY >= STICK_EDGE) {
				move = -1;   // 上入力
			} else if (prevStickY_ > -STICK_EDGE && stickY <= -STICK_EDGE) {
				move = +1;   // 下入力
			}
			prevStickY_ = stickY;

			if (move != 0)
			{
				cursor_ = (cursor_ + move + RESULT_MENU_COUNT) % RESULT_MENU_COUNT;
				if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					screen->SetCursor(cursor_);
				}
			}

			if (!input.IsTriggered(GameAction::Confirm)) { return; }

			PlayDecisionSE();

			auto& screens = aq::ui::UIContext::Get().Screens();
			switch (cursor_)
			{
			case MENU_RETRY:
				// ロードなしで即再開 (InGameState::OnEnter がリセットする)。
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_NEXT:
				// ステージが 1 つの間は同じステージを再プレイ (複数化したら次インデックスをロードする)。
				screens.Replace("AquaDashInGame");
				flow.ChangeState(std::make_unique<InGameState>());
				break;

			case MENU_TITLE:
				DestroyStageWorld(flow);
				flow.Context().gameplayPaused = false;
				screens.Replace("AquaDashTitle");
				flow.ChangeState(std::make_unique<TitleState>());
				break;
			}
		}
	}
}
