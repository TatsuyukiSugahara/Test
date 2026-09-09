#include "stdafx.h"
#include "AquaDashStates.h"
#include "UI/AquaDashScreens.h"
#include "GameInput.h"
#include "GameAction.h"
#include "ECS/SpeedCharacterComponentSystem.h"
#include "ECS/AutoCameraComponentSystem.h"
#include "ECS/CoinComponentSystem.h"
#include "ECS/SessionComponent.h"
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
			static const char* PLAYER_RUN_ANIM   = "Assets/animData/run.tka";
			static const char* PLAYER_JUMP_ANIM  = "Assets/animData/jump.tka";

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

			// 草 (P12-3: コース沿いに 10 万本。散布とベイクはローディングのワーカースレッドで行う)。
			static constexpr uint32_t GRASS_QUAD_COUNT      = 3;        // 房を構成する交差クアッドの枚数
			static constexpr float GRASS_HEIGHT             = 0.5f;     // 房の高さ [m]
			static constexpr float GRASS_WIDTH              = 0.12f;    // 房の根元の幅 [m]
			static constexpr float GRASS_CELL_SIZE          = 32.0f;    // ベイクする XZ セルの 1 辺 [m]
			static constexpr float GRASS_DRAW_DISTANCE      = 120.0f;   // セル AABB までの最大描画距離 [m]
			static constexpr int   GRASS_TARGET_COUNT       = 100000;   // 置きたいおおよその本数
			static constexpr int   GRASS_PER_STEP_COUNT     = 20;       // スプラインの 1 ステップあたりの本数
			static constexpr float GRASS_BAND_HALF_WIDTH    = 40.0f;    // コース中心からの横方向の帯 [m]
			static constexpr float GRASS_ROAD_MARGIN        = 2.0f;     // 路面端からさらに空ける余白 [m]

			// 棄却されるぶんを見込んだ候補の水増し率。歩幅はこの倍率から逆算するので、
			// 棄却率がこの余剰 (28%) に収まっていれば目標本数に届く。
			static constexpr float GRASS_CANDIDATE_OVERSAMPLE  = 1.4f;
			// スプラットの草重み (layer0 = R) がこれ未満なら岩/雪の領域とみなして生やさない。
			static constexpr float GRASS_SPLAT_MIN             = 0.5f;
			// 面法線の Y がこれ未満なら急斜面とみなして生やさない (0.75 ≒ 41 度)。
			static constexpr float GRASS_SLOPE_MIN_NORMAL_Y    = 0.75f;
			// 傾斜を中心差分で見るときの前後左右のサンプル距離 [m]。
			static constexpr float GRASS_SLOPE_SAMPLE_DISTANCE = 1.0f;

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


			// セッション状態を取り出す。生成は GameFlow::Initialize なので通常は非 null。
			// 状態クラスはメインスレッドで動くので、ここから得たポインタへは書き込んでよい。
			app::ecs::SessionComponent* GetSession()
			{
				return aq::ecs::EntityContext::Get().GetSingletonComponent<app::ecs::SessionComponent>();
			}


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
			// 生成した Entity はタイトル復帰時の破棄用に GameFlow の stageEntities へ積む。
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
			 * 草メッシュのローカル AABB。房の寸法はメッシュへ焼き込むので実寸で持つ。
			 * メッシュ登録 (メインスレッド) とセル AABB の算出 (ワーカー) の両方で使うため、
			 * 値がずれないようここへ一元化する。
			 */
			aq::math::AABB MakeGrassLocalBounds()
			{
				return aq::math::AABB(
					aq::math::Vector3(0.0f, GRASS_HEIGHT * 0.5f, 0.0f),
					aq::math::Vector3(GRASS_WIDTH * 0.5f, GRASS_HEIGHT * 0.5f, GRASS_WIDTH * 0.5f));
			}


			/**
			 * 草の配置をベイク済みデータとして作る (純 CPU・ワーカースレッドから呼ぶ)。
			 * コーススプラインを一定間隔で歩き、1 ステップにつき GRASS_PER_STEP_COUNT 本を
			 * 路肩寄りの帯へ置く。スプラットの草重みと傾斜で間引いてからセル分けしてベイクする。
			 * 高さ・重みは HeightmapChunk::CpuData から直接引くので、チャンク生成前でも呼べる。
			 * @param stageData   コーススプラインと路面幅
			 * @param terrainDesc 地形 Desc (terrainSize / heightScale をサンプリングに使う)
			 * @param terrainCpu  PrepareCpuData 済みの地形データ
			 * @param ext         コースの XZ 範囲 (地形原点の算出に使う)
			 * @return セル分けしたベイク済み配置 (そのまま SetBakedData へ move できる)
			 */
			aq::ecs::BakedData BuildGrassBakedData(const stage::StageData& stageData,
			                                       const aq::terrain::HeightmapChunk::Desc& terrainDesc,
			                                       const aq::terrain::HeightmapChunk::CpuData& terrainCpu,
			                                       const CourseExtents& ext)
			{
				const float total = stageData.spline.GetTotalLength();
				if (!stageData.spline.IsValid() || total <= 0.0f) { return aq::ecs::BakedData(); }

				// 棄却されるぶんを見込んで候補を多めに歩く。歩幅が 0 以下だと無限ループになるので弾く。
				const int candidateCount = static_cast<int>(GRASS_TARGET_COUNT * GRASS_CANDIDATE_OVERSAMPLE);
				const int stepCount      = (candidateCount >= GRASS_PER_STEP_COUNT)
				                         ? candidateCount / GRASS_PER_STEP_COUNT : 1;
				const float step         = total / static_cast<float>(stepCount);
				if (step <= 0.0f) { return aq::ecs::BakedData(); }

				// 地形エンティティの原点 (CreateStageWorld の地面ブロックと同じ式)。
				// SampleHeight は地形ローカル XZ を受け、原点 Y を含まない高さを返す。
				const aq::math::Vector3 terrainOrigin(ext.minX - TERRAIN_MARGIN,
				                                      stageData.terrainHeightOffset,
				                                      ext.minZ - TERRAIN_MARGIN);

				const float roadHalf = stageData.width * 0.5f + GRASS_ROAD_MARGIN;
				const float bandHalf = GRASS_BAND_HALF_WIDTH;

				std::vector<aq::ecs::InstancePoint> points;
				points.reserve(static_cast<size_t>(GRASS_TARGET_COUNT));

				// 棄却の内訳。目標本数に届かないときに、しきい値と水増し率のどちらを直すかの判断材料になる。
				int candidateCounted = 0;
				int rejectedSplat    = 0;
				int rejectedSlope    = 0;

				for (int s = 0; s < stepCount && static_cast<int>(points.size()) < GRASS_TARGET_COUNT; ++s)
				{
					const auto frame = stageData.spline.Evaluate(step * static_cast<float>(s));
					for (int k = 0; k < GRASS_PER_STEP_COUNT; ++k)
					{
						// 横位置は路面端から外へ u^2 で伸ばす。プレイヤーが見るのは主に路肩なので、
						// 遠いほど疎になる分布にして本数を手前へ寄せる。
						const float rand01  = HashNoise(s, k);
						const float side    = (rand01 < 0.5f) ? -1.0f : 1.0f;
						const float u       = (rand01 < 0.5f) ? rand01 * 2.0f : (rand01 - 0.5f) * 2.0f;
						const float lateral = side * (roadHalf + u * u * (bandHalf - roadHalf));

						const aq::math::Vector3 base = frame.position + frame.right * lateral;
						const float localX = base.x - terrainOrigin.x;
						const float localZ = base.z - terrainOrigin.z;
						++candidateCounted;

						// 岩/雪のスプラット領域には生やさない (x = layer0 = 草の重み)。
						const aq::math::Vector4 splat =
							aq::terrain::HeightmapChunk::SampleSplat(terrainCpu, terrainDesc, localX, localZ);
						if (splat.x < GRASS_SPLAT_MIN) { ++rejectedSplat; continue; }

						// 急斜面には生やさない。高さの中心差分から面法線を作り、その Y 成分で見る。
						const float d  = GRASS_SLOPE_SAMPLE_DISTANCE;
						const float hL = aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX - d, localZ);
						const float hR = aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX + d, localZ);
						const float hB = aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX, localZ - d);
						const float hF = aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX, localZ + d);
						aq::math::Vector3 normal(-(hR - hL) / (2.0f * d), 1.0f, -(hF - hB) / (2.0f * d));
						normal.Normalize();
						if (normal.y < GRASS_SLOPE_MIN_NORMAL_Y) { ++rejectedSlope; continue; }

						const float worldX = base.x;
						const float worldZ = base.z;
						const float worldY = terrainOrigin.y
						                   + aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX, localZ);

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
						points.push_back(p);
						if (static_cast<int>(points.size()) >= GRASS_TARGET_COUNT) { break; }
					}
				}

				aq::StartupMarkf("[load] grass scatter: %d placed / %d candidates (rejected splat %d, slope %d)",
				                 static_cast<int>(points.size()), candidateCounted, rejectedSplat, rejectedSlope);

				// エンティティは既定変換のままなので、織り込むワールド行列は単位行列でよい。
				return aq::ecs::InstancedPointListComponent::BuildBakedData(
					points, aq::math::Matrix4x4::Identity, MakeGrassLocalBounds(), GRASS_CELL_SIZE);
			}


			/**
			 * ステージワールドを生成する (メインスレッド)。
			 * preparedTerrain はワーカーで PrepareCpuData 済みの地形データ。null なら同期で作る
			 * (画像デコード+頂点生成が乗るので Debug では 200ms 超のヒッチになる)。
			 * preparedGrass はワーカーで BuildGrassBakedData 済みの草の配置。null なら同期で作る
			 * (地形データの作り直し + 10 万本の散布/ベイクが乗るので同じくヒッチになる)。
			 */
			void CreateStageWorld(GameFlow& flow, const std::shared_ptr<stage::StageData>& stageData,
			                      aq::terrain::HeightmapChunk::CpuData* preparedTerrain,
			                      aq::ecs::BakedData* preparedGrass)
			{
				auto& ctx     = aq::ecs::EntityContext::Get();
				auto* session = GetSession();
				if (!session) { return; }
				flow.StageEntities().clear();

				const CourseExtents ext = ComputeCourseExtents(*stageData);
				const float minX = ext.minX, maxX = ext.maxX, minZ = ext.minZ, maxZ = ext.maxZ;

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
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("StageGround");
#endif
					flow.StageEntities().push_back(entity.GetHandle());
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
					session->minimapCenterXZ   = aq::math::Vector2((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
					session->minimapHalfExtent = (extentX > extentZ ? extentX : extentZ) * 0.5f + 40.0f;
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
					flow.StageEntities().push_back(entity.GetHandle());
				}
				aq::StartupMark("[load]   road tiles done");

				// 草 (手続き生成の房 + 風揺れ)。散布とベイクはワーカー (BuildGrassBakedData) 側で終えてあり、
				// ここは GPU メッシュの登録・エンティティ生成・ベイク結果の move だけを行う。
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
						// ベイク時のセル AABB と同じ値にするため MakeGrassLocalBounds から取る。
						grassMesh->SetLocalBounds(MakeGrassLocalBounds());
						grassMesh->SetWindParams(GRASS_WIND_STRENGTH, GRASS_WIND_FREQUENCY,
						                         aq::math::Vector3(1.0f, 0.0f, 0.35f));
					}

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::InstancedStaticMeshComponent,
						aq::ecs::InstancedPointListComponent>();
					entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh("Grass");

					// 路面タイルと同じく、配置後に動かない静的オブジェクトなのでベイク経路に載せる。
					auto* pointList = entity.GetComponent<aq::ecs::InstancedPointListComponent>();
					if (preparedGrass != nullptr) {
						pointList->SetBakedData(std::move(*preparedGrass));
					} else {
						// フォールバック: ワーカーの成果物なしで呼ばれた場合 (この関数の単体利用)。
						// 地形 CPU データの作り直しと 10 万本の散布/ベイクがメインスレッドに乗るのでヒッチする。
						const aq::terrain::HeightmapChunk::Desc grassTerrainDesc = MakeTerrainDesc(*stageData, ext);
						pointList->SetBakedData(BuildGrassBakedData(
							*stageData, grassTerrainDesc,
							aq::terrain::HeightmapChunk::PrepareCpuData(grassTerrainDesc), ext));
					}
					// 草は小さいので遠景では見えない。路面と違いミニマップにも要らないので距離で切る。
					pointList->SetMaxDrawDistance(GRASS_DRAW_DISTANCE);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Grass");
#endif
					flow.StageEntities().push_back(entity.GetHandle());
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

					// 走行状態に応じた切り替えは SpeedCharacterSystem が行う。ここでは候補の登録だけ。
					auto* animComp = entity.GetComponent<aq::ecs::AnimationComponent>();
					animComp->AddAnimation(aqHash32("idle"), PLAYER_IDLE_ANIM);
					animComp->AddAnimation(aqHash32("run"),  PLAYER_RUN_ANIM);
					animComp->AddAnimation(aqHash32("jump"), PLAYER_JUMP_ANIM);
					animComp->Play(aqHash32("idle"), true);
					character->currentAnimHash = aqHash32("idle");
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("SpeedPlayer");
#endif
					session->playerHandle = entity.GetHandle();
					flow.StageEntities().push_back(entity.GetHandle());
				}
				flow.SetPlayerHandle(session->playerHandle);   // 影の注視点用
				aq::StartupMark("[load]   player done");

				// 自動カメラ。
				{
					auto entity = ctx.CreateEntity<app::ecs::AutoCameraComponent>();
					auto* autoCam = entity.GetComponent<app::ecs::AutoCameraComponent>();
					autoCam->targetHandle = session->playerHandle;
					autoCam->cameraType   = aq::CameraType::Main;
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("AutoCamera");
#endif
					flow.StageEntities().push_back(entity.GetHandle());
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
					flow.StageEntities().push_back(entity.GetHandle());
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
					session->coinInstancesHandle = entity.GetHandle();
					flow.StageEntities().push_back(entity.GetHandle());
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
					session->collectFxHandle = entity.GetHandle();
					flow.StageEntities().push_back(entity.GetHandle());
				}
			}


			// プレイヤーをスポーン状態へ戻す (「もう一度」のロードなし再開用)。
			void ResetPlayers(GameFlow& flow)
			{
				auto* session = GetSession();
				if (!session) { return; }
				const auto stageData = session->activeStage;
				if (!stageData) { return; }

				auto& ctx = aq::ecs::EntityContext::Get();
				if (ctx.IsValid(session->playerHandle)) {
					if (auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(session->playerHandle)) {
						character->distance         = stageData->spawnDistance;
						character->lateral          = stageData->spawnLanes.empty() ? 0.0f : stageData->spawnLanes[0];
						character->height           = 0.0f;
						character->speed            = 0.0f;
						character->verticalVelocity = 0.0f;
						character->grounded         = true;
						character->fallen           = false;
						character->worldVelocity    = aq::math::Vector3(0.0f, 0.0f, 0.0f);
					}
					if (auto* score = ctx.GetComponent<app::ecs::PlayerScoreComponent>(session->playerHandle)) {
						score->coinCount = 0;
						score->fallCount = 0;
					}
				}
				flow.PlayResult() = PlayResult();

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
				auto* session = GetSession();
				for (const auto& handle : flow.StageEntities()) {
					if (ctx.IsValid(handle)) {
						ctx.RequestDestroyEntity(handle);
					}
				}
				flow.StageEntities().clear();
				if (session) {
					session->playerHandle        = aq::ecs::EntityHandle();
					session->collectFxHandle     = aq::ecs::EntityHandle();
					session->coinInstancesHandle = aq::ecs::EntityHandle();
					session->activeStage.reset();
				}

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
			if (flow.StageList().empty()) {
				flow.StageList() = stage::StageRegistry::LoadList(STAGE_LIST_PATH);
			}

			// 選択中ステージ名をタイトルへ反映する。
			const auto& list = flow.StageList();
			if (!list.empty()) {
				const int index = aq::math::Clamp(flow.SelectedStageIndex(), 0, static_cast<int>(list.size()) - 1);
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
			if (flow.StageList().empty()) { return; }   // 一覧が無ければ開始できない

			PlayDecisionSE();

			flow.PlayResult() = PlayResult();
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

			const auto& list = flow.StageList();
			const int index = flow.SelectedStageIndex();
			stagePath_ = list[index >= 0 && index < static_cast<int>(list.size()) ? index : 0].stagePath;
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
							const aq::terrain::HeightmapChunk::Desc terrainDesc = MakeTerrainDesc(*result.stage, ext);
							result.terrainCpu = aq::terrain::HeightmapChunk::PrepareCpuData(terrainDesc);
							aq::StartupMark("[load] terrain cpu data prepared (worker)");

							// 草の散布とベイク。地形の高さ/スプラットを引くので地形データの後に行う。
							result.grassBaked = BuildGrassBakedData(*result.stage, terrainDesc, result.terrainCpu, ext);
							aq::StartupMarkf("[load] grass scattered and baked (worker): %zu instances / %zu cells",
							                 result.grassBaked.instances.size(), result.grassBaked.cells.size());
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

				if (auto* session = GetSession()) { session->activeStage = stageData; }
				aq::StartupMark("[load] worker result received");
				CreateStageWorld(flow, stageData, &result.terrainCpu, &result.grassBaked);   // GPU 生成のみ (CPU 前計算はワーカー済み)
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

			auto* session = GetSession();
			if (!session) { return; }
			session->gameplayPaused = false;
			ResetPlayers(flow);

			// ミニマップ: コース形状をスプラインから等間隔サンプリングし、UI の点列として描く。
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				std::vector<aq::math::Vector2> uvPoints;
				const auto stageData = session->activeStage;
				if (stageData && stageData->spline.IsValid() && session->minimapHalfExtent > 1.0f)
				{
					constexpr int SAMPLE_COUNT = 160;
					const float span  = session->minimapHalfExtent * 2.0f;
					const float total = stageData->spline.GetTotalLength();
					uvPoints.reserve(SAMPLE_COUNT + 1);
					for (int i = 0; i <= SAMPLE_COUNT; ++i)
					{
						const auto position =
							stageData->spline.Evaluate(total * static_cast<float>(i) / SAMPLE_COUNT).position;
						uvPoints.push_back(aq::math::Vector2(
							0.5f + (position.x - session->minimapCenterXZ.x) / span,
							0.5f - (position.z - session->minimapCenterXZ.y) / span));
					}
				}
				screen->SetMinimapCourse(uvPoints);
			}
		}


		void InGameState::OnUpdate(GameFlow& flow, const float dt)
		{
			elapsed_ += dt;

			auto* session = GetSession();
			if (!session) { return; }
			const auto stageData = session->activeStage;
			if (!stageData) { return; }

			auto& ctx = aq::ecs::EntityContext::Get();
			if (!ctx.IsValid(session->playerHandle)) { return; }
			const auto* character = ctx.GetComponent<app::ecs::SpeedCharacterComponent>(session->playerHandle);
			if (!character) { return; }

			// HUD 更新 (時間 / コイン / 速度 / ミニマップマーカー)。
			const auto* score    = ctx.GetComponent<app::ecs::PlayerScoreComponent>(session->playerHandle);
			const auto* playerTc = ctx.GetComponent<aq::ecs::TransformComponent>(session->playerHandle);
			if (auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				screen->SetHUD(elapsed_, score ? score->coinCount : 0, character->speed * 3.6f);

				// 俯瞰カメラは 画面右=+X / 画面上=+Z。UI の v は下+なので Z を反転する。
				if (playerTc && session->minimapHalfExtent > 1.0f) {
					const float span = session->minimapHalfExtent * 2.0f;
					const float u = 0.5f + (playerTc->position.x - session->minimapCenterXZ.x) / span;
					const float v = 0.5f - (playerTc->position.z - session->minimapCenterXZ.y) / span;
					screen->SetMinimapMarker(u, v);
				}
			}

			// ゴール / 落下判定。
			// 落下は「路面相対 height がしきい値未満」または「ループ脱落後に地面高さまで落ちた」。
			const bool  goal     = character->distance >= stageData->goalDistance;
			const bool  fall     = character->height < stageData->fallHeight
			                    || (character->fallen && playerTc && playerTc->position.y < 0.5f);
			if (!goal && !fall) { return; }

			PlayResult& result  = flow.PlayResult();
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
			auto* session = GetSession();
			if (session) { session->gameplayPaused = true; }

			// Replace 済みの最前面がリザルト画面。結果と初期カーソルを反映する。
			if (auto* screen = static_cast<ResultScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
				const PlayResult& result = flow.PlayResult();

				// ランクはクリア時のみ (設計 03: ゲームオーバーはランクなし)。
				std::string rank;
				if (result.cleared && session && session->activeStage) {
					rank = session->activeStage->CalcRank(result.coinCount, result.clearTimeSec);
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
				if (auto* session = GetSession()) { session->gameplayPaused = false; }
				screens.Replace("AquaDashTitle");
				flow.ChangeState(std::make_unique<TitleState>());
				break;
			}
		}
	}
}
