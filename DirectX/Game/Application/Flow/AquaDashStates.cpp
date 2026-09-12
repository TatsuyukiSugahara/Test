#include "stdafx.h"
#include "AquaDashStates.h"
#include "Application.h"
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

			// 路面リボン (スプライン追従の連続メッシュ)。断面ピッチが細かいほどカーブが滑らかになるが頂点が増える。
			static constexpr float ROAD_SECTION_STEP  = 2.0f;    // 断面の間隔 [m]
			static constexpr float ROAD_THICKNESS     = 0.3f;    // 路面の厚み [m]
			// 地形面 (y=0) より上面がわずかに出るよう断面中心を 0.1 下げる (厚み 0.3 → 上面 +0.05)。
			// 深く沈めると平坦地形に埋まって見えなくなる。
			static constexpr float ROAD_SINK          = 0.1f;    // 断面中心をスプラインから下げる量 [m]
			static constexpr float ROAD_UV_LENGTH     = 20.0f;   // UV の v が 1 進む距離 [m]

			// ライン装飾 (テクスチャ経路を使わずメッシュを分けて色で出す)。
			static constexpr float ROAD_MARKING_LIFT  = 0.02f;   // 路面上面から浮かせる量 [m] (z-fight 回避)
			static constexpr float ROAD_EDGE_INSET    = 0.35f;   // 路面端からエッジライン中心までの距離 [m]
			static constexpr float ROAD_EDGE_WIDTH    = 0.25f;   // エッジラインの幅 [m]
			static constexpr float ROAD_DASH_WIDTH    = 0.15f;   // センター破線の幅 [m]
			static constexpr float ROAD_DASH_ON       = 4.0f;    // 破線の描き [m]
			static constexpr float ROAD_DASH_OFF      = 4.0f;    // 破線の空き [m]

			// ブーストパッド (P21)。路面ライン装飾と同じく、テクスチャを使わずメッシュを分けて色で出す。
			static constexpr float BOOST_PAD_LENGTH      = 9.0f;    // パッドの進行方向の長さ [m]
			static constexpr float BOOST_PAD_LIFT        = 0.03f;   // 路面上面から浮かせる量 [m] (ライン装飾より上)
			static constexpr float BOOST_ARROW_LIFT      = 0.05f;   // 矢印はさらに上へ (パッドとの z-fight 回避)
			static constexpr int   BOOST_ARROW_COUNT     = 3;       // 1 パッドに描く矢印の数
			static constexpr float BOOST_ARROW_LENGTH    = 1.8f;    // 矢印 1 個の長さ [m]
			static constexpr float BOOST_ARROW_WIDTH_RATE = 0.72f;  // 矢印の幅 / パッド幅

			// ジャンプ台 (P22)。ブーストパッドと同じ「平らな板 + 矢印」方式。楔形の台を置くと
			// プレイヤーの height が路面相対で台を登らないため、メッシュを突き抜けてしまう。
			// 矢印はブーストパッド (前向きの三角) と区別できるよう、上向きの台形にする。
			static constexpr float RAMP_PAD_LENGTH        = 9.0f;    // 台の進行方向の長さ [m]
			static constexpr int   RAMP_ARROW_COUNT       = 3;       // 1 台に描く矢印の数
			static constexpr float RAMP_ARROW_LENGTH      = 1.8f;    // 矢印 1 個の長さ [m]
			static constexpr float RAMP_ARROW_WIDTH_RATE  = 0.72f;   // 矢印の根元の幅 / 台の幅
			static constexpr float RAMP_ARROW_TIP_RATE    = 0.30f;   // 矢印の先端の幅 / 根元の幅 (台形)

			// コイン取得エフェクト (常駐エミッタを移動+Restart で使い回す)。
			static const char* COLLECT_FX_PATH = "Assets/Particle/FX_Explosion.particle";

			// リングコイン (トーラス) の寸法。外径 0.9m = (中心半径 + 管半径) × 2。
			// 寸法はメッシュへ焼き込むので、インスタンス点の scale は等倍で使う。
			static constexpr float COIN_RING_CENTER_RADIUS = 0.36f;   // 中心円の半径 [m]
			static constexpr float COIN_RING_TUBE_RADIUS   = 0.09f;   // 管の半径 [m]
			static constexpr int   COIN_RING_SEGMENT_COUNT = 24;      // 中心円まわりの分割数
			static constexpr int   COIN_RING_SIDE_COUNT    = 12;      // 管断面の分割数
			static constexpr float COIN_RING_TWO_PI        = 6.28318530718f;

			// 草 (P20: コース沿いに 40 万本。散布とベイクはローディングのワーカースレッドで行う)。
			static constexpr uint32_t GRASS_BLADE_COUNT     = 4;        // 房を構成する葉の枚数 (1 株で覆う面積を増やす)
			static constexpr float GRASS_HEIGHT             = 0.5f;     // 房の高さ [m]
			static constexpr float GRASS_WIDTH              = 0.14f;    // 房の根元の幅 [m]
			static constexpr float GRASS_CELL_SIZE          = 32.0f;    // ベイクする XZ セルの 1 辺 [m]
			static constexpr float GRASS_DRAW_DISTANCE      = 120.0f;   // セル AABB までの最大描画距離 [m]
			static constexpr int   GRASS_TARGET_COUNT       = 1800000;  // 置きたいおおよその本数
			// コース中心からの横方向の帯 [m]。端で草がぱったり途切れると境界線が見えるので、帯自体は広く取り、
			// 下の GRASS_BAND_FALLOFF_EXPONENT で手前へ寄せて外側は裾を引かせる。
			static constexpr float GRASS_BAND_HALF_WIDTH    = 45.0f;
			// 横位置の分布 (lateral = roadHalf + u^この値 × (band - roadHalf)、u は一様乱数)。
			// 大きいほど路肩へ集まり、外側は疎な裾になる。3.0 だと約 8 割が 26m 以内に入る。
			static constexpr float GRASS_BAND_FALLOFF_EXPONENT = 3.0f;
			static constexpr float GRASS_ROAD_MARGIN        = 0.6f;     // 路面端からさらに空ける余白 [m] (路肩の地肌を減らす)

			// クラスタ散布 (疎密を作るのが目的)。1 ステップにクラスタ中心を数個置き、その周りへ
			// 中心寄りの分布で草を固めることで、均等にばら撒いたときの「等間隔」な見え方を消す。
			static constexpr int   GRASS_CLUSTER_PER_STEP_COUNT = 7;       // 1 ステップに置くクラスタ中心の数
			static constexpr int   GRASS_CLUSTER_MIN_COUNT      = 5;       // 1 クラスタの本数の下限
			static constexpr int   GRASS_CLUSTER_MAX_COUNT      = 16;      // 1 クラスタの本数の上限
			static constexpr float GRASS_CLUSTER_MIN_RADIUS     = 0.6f;    // クラスタ半径の下限 [m]
			static constexpr float GRASS_CLUSTER_MAX_RADIUS     = 1.5f;    // クラスタ半径の上限 [m] (広げすぎると 1 株が薄まって地肌が出る)
			// 半径の分布指数 (r = R * u^この値)。1 より大きいほど中心へ寄り、株のように固まる。
			static constexpr float GRASS_CLUSTER_RADIUS_BIAS    = 1.5f;
			// クラスタごとの密度倍率。平均が 1.0 になるよう対称に取り、まばらな所と密な所を混在させる。
			static constexpr float GRASS_CLUSTER_DENSITY_MIN    = 0.55f;
			static constexpr float GRASS_CLUSTER_DENSITY_MAX    = 1.45f;
			// 1 ステップあたりの期待本数 (歩幅の逆算に使う。密度倍率の平均が 1.0 なので本数の平均だけでよい)。
			static constexpr int   GRASS_PER_STEP_COUNT =
				GRASS_CLUSTER_PER_STEP_COUNT * ((GRASS_CLUSTER_MIN_COUNT + GRASS_CLUSTER_MAX_COUNT) / 2);

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

			// 姿勢。同じメッシュの繰り返しに見せないため、Y 回転に加えて傾きと非一様スケールを散らす。
			static constexpr float GRASS_TILT_MAX_RADIAN    = 0.34906585f;   // ランダム方位への傾きの上限 (20 度)
			static constexpr float GRASS_SCALE_Y_MIN        = 0.70f;         // 縦スケールの下限 (低すぎる株を無くす)
			static constexpr float GRASS_SCALE_Y_MAX        = 1.60f;         // 縦スケールの上限
			static constexpr float GRASS_SCALE_XZ_MIN       = 0.80f;         // 横スケールの下限
			static constexpr float GRASS_SCALE_XZ_MAX       = 1.20f;         // 横スケールの上限
			// 背丈の勾配。路面に近いほど低く (踏まれた路肩)、離れるほど高い (伸びた野原)。
			static constexpr float GRASS_HEIGHT_NEAR_SCALE  = 0.70f;
			static constexpr float GRASS_HEIGHT_FAR_SCALE   = 1.30f;

			// 色むらの両端。むら値 0 = 深緑 (日陰) / 1 = 黄緑 (日向) で補間する。
			// 現行 (0.22, 0.46, 0.16) より彩度を上げ、地形テクスチャの草色へ寄せる。
			static constexpr float GRASS_COLOR_DARK_R       = 0.16f;
			static constexpr float GRASS_COLOR_DARK_G       = 0.40f;
			static constexpr float GRASS_COLOR_DARK_B       = 0.11f;
			static constexpr float GRASS_COLOR_LIGHT_R      = 0.42f;
			static constexpr float GRASS_COLOR_LIGHT_G      = 0.66f;
			static constexpr float GRASS_COLOR_LIGHT_B      = 0.18f;

			// 色むらの値ノイズ。長い波長で野原全体の帯を作り、短い波長でその縁を崩す。
			static constexpr float GRASS_COLOR_NOISE_WAVELENGTH0 = 22.0f;   // 第 1 オクターブの波長 [m]
			static constexpr float GRASS_COLOR_NOISE_WAVELENGTH1 = 7.0f;    // 第 2 オクターブの波長 [m]
			static constexpr float GRASS_COLOR_NOISE_WEIGHT1     = 0.35f;   // 第 2 オクターブの寄与
			// per-instance のゆらぎ幅 (±4%)。これ以上乗せると白色ノイズに戻り、まとまりが消える。
			static constexpr float GRASS_COLOR_JITTER            = 0.04f;

			// 花 (クラスタ単位で選んだ「花クラスタ」にだけ置いて群生させる。均等散布では人工物感が出る)。
			static constexpr uint32_t FLOWER_PETAL_COUNT    = 5;        // 花弁の枚数
			// 花冠中心のインスタンス原点からの高さ [m]。草は GRASS_HEIGHT 0.5m × 縦スケール 0.70〜1.60 =
			// 最大 0.8m あるので、それより低いと草に埋もれて見えない。穂先に並ぶ高さにする。
			static constexpr float FLOWER_HEIGHT            = 0.58f;
			static constexpr float FLOWER_RADIUS            = 0.11f;    // 花弁先端までの半径 [m]
			static constexpr float FLOWER_DRAW_DISTANCE     = 90.0f;    // 草 (120m) より小さいので手前で切る [m]
			// 花クラスタに選ぶクラスタの割合。草の本数 × この割合 × FLOWER_PER_GRASS_RATIO が
			// 花の本数になる (1,800,000 × 0.110 × 0.25 で約 50,000 本)。
			// 12,000 本では 7.2km に散るため視界内に数本しか入らず、実質見えなかった。
			static constexpr float FLOWER_CLUSTER_RATIO     = 0.110f;
			static constexpr float FLOWER_PER_GRASS_RATIO   = 0.25f;    // 花クラスタ内で草 1 本あたりに置く花の本数
			static constexpr float FLOWER_SCALE_MIN         = 0.80f;    // 花のスケールの下限
			static constexpr float FLOWER_SCALE_MAX         = 1.30f;    // 花のスケールの上限
			static constexpr float FLOWER_TILT_MAX_RADIAN   = 0.17453293f;   // 傾きの上限 (10 度。草より立たせる)
			static constexpr float FLOWER_COLOR_JITTER      = 0.06f;    // クラスタ内での色のゆらぎ幅 (±6%)
			// 風は草 (0.06 / 1.7) より弱く速め。軽い花冠が細かく震えるように見せる。
			static constexpr float FLOWER_WIND_STRENGTH     = 0.035f;
			static constexpr float FLOWER_WIND_FREQUENCY    = 2.6f;

			/** 花の色パレット (白 / 黄 / 桃 / 淡青)。クラスタ単位で 1 色を選び、クラスタ内は同色にする */
			static constexpr int   FLOWER_PALETTE_COUNT     = 4;
			static constexpr float FLOWER_PALETTE[FLOWER_PALETTE_COUNT][3] =
			{
				{ 0.96f, 0.96f, 0.92f },   // 白
				{ 0.97f, 0.84f, 0.28f },   // 黄
				{ 0.95f, 0.58f, 0.72f },   // 桃
				{ 0.68f, 0.82f, 0.96f },   // 淡青
			};

			// ハッシュ乱数のシードを作るときの XZ 量子化 (1m あたりの分割数)。
			// クラスタ散布は 0.25m 格子より近くへ草を固めるので、P12-3 の 4 分割から上げて種の重複を避ける。
			static constexpr float GRASS_SEED_QUANTIZE      = 16.0f;
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


			/**
			 * 格子点の HashNoise を双一次補間する値ノイズ (0-1)。
			 * 白色ノイズと違い隣り合う点が近い値になるので、野原のようなまとまった色むらが作れる。
			 * @param x    ワールド X を波長で割った「格子単位」の座標
			 * @param z    ワールド Z を波長で割った「格子単位」の座標
			 * @param seed 格子を Z 方向へずらすオフセット (オクターブごとに変えて相関を切る)
			 * @return 0-1 の連続したノイズ値
			 */
			float ValueNoise2D(const float x, const float z, const int seed)
			{
				const float fx = floorf(x);
				const float fz = floorf(z);
				const int   ix = static_cast<int>(fx);
				const int   iz = static_cast<int>(fz) + seed;

				// smoothstep で補間して、一次補間の折れ (格子の継ぎ目) が筋に見えるのを防ぐ。
				const float tx = x - fx;
				const float tz = z - fz;
				const float sx = tx * tx * (3.0f - 2.0f * tx);
				const float sz = tz * tz * (3.0f - 2.0f * tz);

				const float n00 = HashNoise(ix,     iz);
				const float n10 = HashNoise(ix + 1, iz);
				const float n01 = HashNoise(ix,     iz + 1);
				const float n11 = HashNoise(ix + 1, iz + 1);
				const float a = n00 + (n10 - n00) * sx;
				const float b = n01 + (n11 - n01) * sx;
				return a + (b - a) * sz;
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


			// 頂点を 1 個積む (路面系メッシュ共通)。接線はシェーダが使わないのでゼロのまま。
			void PushRoadVertex(std::vector<aq::graphics::VertexData>& outVertices,
			                    const aq::math::Vector3& position, const aq::math::Vector3& normal,
			                    const float u, const float v)
			{
				aq::graphics::VertexData vertex;
				vertex.position = position;
				vertex.normal   = normal;
				vertex.uv.Set(u, v);
				vertex.tangent.Set(0.0f, 0.0f, 0.0f, 0.0f);
				outVertices.push_back(vertex);
			}


			// 四角形 1 枚を三角形 2 枚のインデックスへ展開する。頂点は
			// 「手前左 → 奥左 → 奥右 → 手前右」の順 (左 = -right / 奥 = +tangent) で渡す。
			// この並びなら (b-a)×(d-a) が面法線と同じ向きになるので表面として描かれる。
			void EmitRoadQuad(std::vector<uint32_t>& outIndices,
			                  const uint32_t a, const uint32_t b, const uint32_t c, const uint32_t d)
			{
				outIndices.push_back(a);
				outIndices.push_back(b);
				outIndices.push_back(d);
				outIndices.push_back(b);
				outIndices.push_back(c);
				outIndices.push_back(d);
			}


			// 頂点列からローカル AABB を作る。インスタンス点は原点・無回転・等倍で置くので、
			// これがそのままカリング用のワールド AABB になる。
			aq::math::AABB ComputeRoadMeshBounds(const std::vector<aq::graphics::VertexData>& vertices)
			{
				aq::math::AABBBuilder builder;
				for (const auto& vertex : vertices) {
					builder.Add(vertex.position);
				}
				return builder.Build();
			}


			// 断面を取る距離を刻む。終端は端数ぶんの断面を 1 枚足して、コース末端まで隙間なく閉じる。
			// 路面本体とエッジラインで同じ列を使うので、両者の断面がずれない。
			void BuildRoadSectionDistances(const float totalLength, std::vector<float>& outDistances)
			{
				outDistances.clear();
				if (totalLength <= 0.0f) { return; }

				outDistances.reserve(static_cast<size_t>(totalLength / ROAD_SECTION_STEP) + 2);
				for (float d = 0.0f; d < totalLength; d += ROAD_SECTION_STEP) {
					outDistances.push_back(d);
				}
				if (outDistances.back() < totalLength - 0.001f) {
					outDistances.push_back(totalLength);
				}
			}


			// 路面本体。断面ごとに上面 / 底面 / 左右側面ぶんの法線を分けた 8 頂点を積み、
			// 隣り合う断面を 4 枚の面で閉じる。閉じ断面なのでループを巻いても裏面欠けが出ない。
			// UV は将来のテクスチャ対応用に焼いておく (u = 横位置 0-1 / v = 距離 / ROAD_UV_LENGTH)。
			void BuildRoadRibbonMesh(const stage::StageData& stageData, const std::vector<float>& distances,
			                         std::vector<aq::graphics::VertexData>& outVertices,
			                         std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();
				if (distances.size() < 2) { return; }

				const float halfWidth     = stageData.width * 0.5f;
				const float halfThickness = ROAD_THICKNESS * 0.5f;

				outVertices.reserve(distances.size() * 8);
				for (const float d : distances)
				{
					const auto frame = stageData.spline.Evaluate(d);
					const aq::math::Vector3 up    = frame.up;
					const aq::math::Vector3 down  = frame.up * -1.0f;
					const aq::math::Vector3 right = frame.right;
					const aq::math::Vector3 left  = frame.right * -1.0f;

					const aq::math::Vector3 center      = frame.position - up * ROAD_SINK;
					const aq::math::Vector3 topLeft     = center + up   * halfThickness + left  * halfWidth;
					const aq::math::Vector3 topRight    = center + up   * halfThickness + right * halfWidth;
					const aq::math::Vector3 bottomLeft  = center + down * halfThickness + left  * halfWidth;
					const aq::math::Vector3 bottomRight = center + down * halfThickness + right * halfWidth;
					const float v = d / ROAD_UV_LENGTH;

					PushRoadVertex(outVertices, topLeft,     up,    0.0f, v);   // +0 上面
					PushRoadVertex(outVertices, topRight,    up,    1.0f, v);   // +1
					PushRoadVertex(outVertices, bottomLeft,  down,  0.0f, v);   // +2 底面
					PushRoadVertex(outVertices, bottomRight, down,  1.0f, v);   // +3
					PushRoadVertex(outVertices, topLeft,     left,  1.0f, v);   // +4 左側面
					PushRoadVertex(outVertices, bottomLeft,  left,  0.0f, v);   // +5
					PushRoadVertex(outVertices, topRight,    right, 1.0f, v);   // +6 右側面
					PushRoadVertex(outVertices, bottomRight, right, 0.0f, v);   // +7
				}

				outIndices.reserve((distances.size() - 1) * 24);
				for (size_t i = 0; i + 1 < distances.size(); ++i)
				{
					const uint32_t s = static_cast<uint32_t>(i * 8);
					const uint32_t n = static_cast<uint32_t>((i + 1) * 8);

					EmitRoadQuad(outIndices, s + 0, n + 0, n + 1, s + 1);   // 上面 (+up)
					EmitRoadQuad(outIndices, s + 3, n + 3, n + 2, s + 2);   // 底面 (-up)
					EmitRoadQuad(outIndices, s + 5, n + 5, n + 4, s + 4);   // 左側面 (-right)
					EmitRoadQuad(outIndices, s + 6, n + 6, n + 7, s + 7);   // 右側面 (+right)
				}
			}


			// 左右のエッジライン。路面上面から少し浮かせた薄いストリップ 2 本を 1 メッシュにまとめる。
			void BuildRoadEdgeLineMesh(const stage::StageData& stageData, const std::vector<float>& distances,
			                           std::vector<aq::graphics::VertexData>& outVertices,
			                           std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();
				if (distances.size() < 2) { return; }

				const float lift     = ROAD_THICKNESS * 0.5f + ROAD_MARKING_LIFT - ROAD_SINK;
				const float lineMid  = stageData.width * 0.5f - ROAD_EDGE_INSET;
				const float halfLine = ROAD_EDGE_WIDTH * 0.5f;

				outVertices.reserve(distances.size() * 4);
				for (const float d : distances)
				{
					const auto frame = stageData.spline.Evaluate(d);
					const aq::math::Vector3 base = frame.position + frame.up * lift;
					const float v = d / ROAD_UV_LENGTH;

					// 横位置は -right 側から順に積む (EmitRoadQuad の「手前左」の並びに合わせる)。
					PushRoadVertex(outVertices, base + frame.right * (-lineMid - halfLine), frame.up, 0.0f, v);   // +0 左ライン外
					PushRoadVertex(outVertices, base + frame.right * (-lineMid + halfLine), frame.up, 1.0f, v);   // +1 左ライン内
					PushRoadVertex(outVertices, base + frame.right * ( lineMid - halfLine), frame.up, 0.0f, v);   // +2 右ライン内
					PushRoadVertex(outVertices, base + frame.right * ( lineMid + halfLine), frame.up, 1.0f, v);   // +3 右ライン外
				}

				outIndices.reserve((distances.size() - 1) * 12);
				for (size_t i = 0; i + 1 < distances.size(); ++i)
				{
					const uint32_t s = static_cast<uint32_t>(i * 4);
					const uint32_t n = static_cast<uint32_t>((i + 1) * 4);

					EmitRoadQuad(outIndices, s + 0, n + 0, n + 1, s + 1);   // 左ライン
					EmitRoadQuad(outIndices, s + 2, n + 2, n + 3, s + 3);   // 右ライン
				}
			}


			// センター破線。描き区間だけを独立したクアッド列として同一メッシュへ積む
			// (空き区間には頂点を作らないので、1 ドローのまま破線に見える)。
			void BuildRoadCenterDashMesh(const stage::StageData& stageData,
			                             std::vector<aq::graphics::VertexData>& outVertices,
			                             std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();

				const float total = stageData.spline.GetTotalLength();
				if (total <= 0.0f) { return; }

				const float lift     = ROAD_THICKNESS * 0.5f + ROAD_MARKING_LIFT - ROAD_SINK;
				const float halfDash = ROAD_DASH_WIDTH * 0.5f;
				const float period   = ROAD_DASH_ON + ROAD_DASH_OFF;

				for (float start = 0.0f; start < total; start += period)
				{
					const float end = (start + ROAD_DASH_ON < total) ? start + ROAD_DASH_ON : total;
					const float span = end - start;
					if (span < 0.01f) { break; }

					// 描き区間の中もカーブに沿わせたいので、断面ピッチで分割する。
					const int stepCount = static_cast<int>(span / ROAD_SECTION_STEP) + 1;
					const uint32_t base = static_cast<uint32_t>(outVertices.size());

					for (int k = 0; k <= stepCount; ++k)
					{
						const float d     = start + span * static_cast<float>(k) / static_cast<float>(stepCount);
						const auto  frame = stageData.spline.Evaluate(d);
						const aq::math::Vector3 center = frame.position + frame.up * lift;
						const float v = d / ROAD_UV_LENGTH;

						PushRoadVertex(outVertices, center + frame.right * -halfDash, frame.up, 0.0f, v);
						PushRoadVertex(outVertices, center + frame.right *  halfDash, frame.up, 1.0f, v);
					}
					for (int k = 0; k < stepCount; ++k)
					{
						const uint32_t s = base + static_cast<uint32_t>(k * 2);
						EmitRoadQuad(outIndices, s + 0, s + 2, s + 3, s + 1);
					}
				}
			}


			/**
			 * ブーストパッドの下地メッシュ (パッドごとに 1 枚の帯)。
			 * 路面ライン装飾と同じく、パッド中心の distance を挟む区間をスプラインに沿わせて張る。
			 * @param stageData   コーススプラインと boostPads
			 * @param outVertices 生成した頂点
			 * @param outIndices  生成したインデックス
			 */
			void BuildBoostPadPlateMesh(const stage::StageData& stageData,
			                            std::vector<aq::graphics::VertexData>& outVertices,
			                            std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();

				const float total = stageData.spline.GetTotalLength();
				if (total <= 0.0f) { return; }

				const float lift = ROAD_THICKNESS * 0.5f + BOOST_PAD_LIFT - ROAD_SINK;

				for (const auto& pad : stageData.boostPads)
				{
					const float start = pad.distance - BOOST_PAD_LENGTH * 0.5f;
					const float end   = pad.distance + BOOST_PAD_LENGTH * 0.5f;
					if (end <= 0.0f || start >= total) { continue; }

					const float halfWidth = pad.width * 0.5f;
					const int   stepCount = static_cast<int>(BOOST_PAD_LENGTH / ROAD_SECTION_STEP) + 1;
					const uint32_t base   = static_cast<uint32_t>(outVertices.size());

					for (int k = 0; k <= stepCount; ++k)
					{
						const float d = start + (end - start) * static_cast<float>(k) / static_cast<float>(stepCount);
						const auto  frame = stageData.spline.Evaluate(d);
						const aq::math::Vector3 center = frame.position + frame.up * lift
						                               + frame.right * pad.lateral;
						const float v = d / ROAD_UV_LENGTH;

						PushRoadVertex(outVertices, center + frame.right * -halfWidth, frame.up, 0.0f, v);
						PushRoadVertex(outVertices, center + frame.right *  halfWidth, frame.up, 1.0f, v);
					}
					for (int k = 0; k < stepCount; ++k)
					{
						const uint32_t q = base + static_cast<uint32_t>(k * 2);
						EmitRoadQuad(outIndices, q + 0, q + 2, q + 3, q + 1);
					}
				}
			}


			/**
			 * ブーストパッドの矢印メッシュ (進行方向を指す三角形を等間隔に並べる)。
			 * 走行中に一瞬しか見えないので、形は「前を向いた三角形」まで単純化する。
			 * 裏面も張るのは、ループの反転区間に置かれても欠けないようにするため。
			 * @param stageData   コーススプラインと boostPads
			 * @param outVertices 生成した頂点
			 * @param outIndices  生成したインデックス
			 */
			void BuildBoostPadArrowMesh(const stage::StageData& stageData,
			                            std::vector<aq::graphics::VertexData>& outVertices,
			                            std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();

				const float total = stageData.spline.GetTotalLength();
				if (total <= 0.0f) { return; }

				const float lift  = ROAD_THICKNESS * 0.5f + BOOST_ARROW_LIFT - ROAD_SINK;
				// 矢印 1 個ぶんの取り分。パッド長を等分し、その中の前寄りへ矢印を置く。
				const float pitch = BOOST_PAD_LENGTH / static_cast<float>(BOOST_ARROW_COUNT);

				for (const auto& pad : stageData.boostPads)
				{
					const float halfWidth = pad.width * 0.5f * BOOST_ARROW_WIDTH_RATE;
					const float start     = pad.distance - BOOST_PAD_LENGTH * 0.5f;

					for (int i = 0; i < BOOST_ARROW_COUNT; ++i)
					{
						const float rearD = start + pitch * static_cast<float>(i);
						const float tipD  = rearD + BOOST_ARROW_LENGTH;
						if (tipD <= 0.0f || rearD >= total) { continue; }

						const auto rearFrame = stageData.spline.Evaluate(rearD);
						const auto tipFrame  = stageData.spline.Evaluate(tipD);
						const aq::math::Vector3 rearCenter = rearFrame.position + rearFrame.up * lift
						                                   + rearFrame.right * pad.lateral;
						const aq::math::Vector3 tipCenter  = tipFrame.position + tipFrame.up * lift
						                                   + tipFrame.right * pad.lateral;

						const uint32_t base = static_cast<uint32_t>(outVertices.size());
						PushRoadVertex(outVertices, rearCenter + rearFrame.right * -halfWidth, rearFrame.up, 0.0f, 0.0f);
						PushRoadVertex(outVertices, rearCenter + rearFrame.right *  halfWidth, rearFrame.up, 1.0f, 0.0f);
						PushRoadVertex(outVertices, tipCenter,                                 tipFrame.up,  0.5f, 1.0f);

						// 表 + 巻き順を反転した裏。頂点は共有するので頂点数は増えない。
						outIndices.push_back(base + 0);
						outIndices.push_back(base + 2);
						outIndices.push_back(base + 1);
						outIndices.push_back(base + 0);
						outIndices.push_back(base + 1);
						outIndices.push_back(base + 2);
					}
				}
			}


			/**
			 * ジャンプ台の下地メッシュ (台ごとに 1 枚の帯)。形はブーストパッドと同じで、
			 * 色だけ変えて区別する (色は CreateRoadMeshEntity の呼び出し側で与える)。
			 * @param stageData   コーススプラインと ramps
			 * @param outVertices 生成した頂点
			 * @param outIndices  生成したインデックス
			 */
			void BuildRampPadPlateMesh(const stage::StageData& stageData,
			                           std::vector<aq::graphics::VertexData>& outVertices,
			                           std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();

				const float total = stageData.spline.GetTotalLength();
				if (total <= 0.0f) { return; }

				const float lift = ROAD_THICKNESS * 0.5f + BOOST_PAD_LIFT - ROAD_SINK;

				for (const auto& ramp : stageData.ramps)
				{
					const float start = ramp.distance - RAMP_PAD_LENGTH * 0.5f;
					const float end   = ramp.distance + RAMP_PAD_LENGTH * 0.5f;
					if (end <= 0.0f || start >= total) { continue; }

					const float halfWidth = ramp.width * 0.5f;
					const int   stepCount = static_cast<int>(RAMP_PAD_LENGTH / ROAD_SECTION_STEP) + 1;
					const uint32_t base   = static_cast<uint32_t>(outVertices.size());

					for (int k = 0; k <= stepCount; ++k)
					{
						const float d = start + (end - start) * static_cast<float>(k) / static_cast<float>(stepCount);
						const auto  frame = stageData.spline.Evaluate(d);
						const aq::math::Vector3 center = frame.position + frame.up * lift
						                               + frame.right * ramp.lateral;
						const float v = d / ROAD_UV_LENGTH;

						PushRoadVertex(outVertices, center + frame.right * -halfWidth, frame.up, 0.0f, v);
						PushRoadVertex(outVertices, center + frame.right *  halfWidth, frame.up, 1.0f, v);
					}
					for (int k = 0; k < stepCount; ++k)
					{
						const uint32_t q = base + static_cast<uint32_t>(k * 2);
						EmitRoadQuad(outIndices, q + 0, q + 2, q + 3, q + 1);
					}
				}
			}


			/**
			 * ジャンプ台の矢印メッシュ (上向きを表す台形を等間隔に並べる)。
			 * ブーストパッドの矢印は「前を向いた三角形」なので、こちらは先端を細めた台形にして
			 * 一瞬でも別物と分かるようにする。裏面も張るのはブーストパッドと同じ理由。
			 * @param stageData   コーススプラインと ramps
			 * @param outVertices 生成した頂点
			 * @param outIndices  生成したインデックス
			 */
			void BuildRampPadArrowMesh(const stage::StageData& stageData,
			                           std::vector<aq::graphics::VertexData>& outVertices,
			                           std::vector<uint32_t>& outIndices)
			{
				outVertices.clear();
				outIndices.clear();

				const float total = stageData.spline.GetTotalLength();
				if (total <= 0.0f) { return; }

				const float lift  = ROAD_THICKNESS * 0.5f + BOOST_ARROW_LIFT - ROAD_SINK;
				const float pitch = RAMP_PAD_LENGTH / static_cast<float>(RAMP_ARROW_COUNT);

				for (const auto& ramp : stageData.ramps)
				{
					const float baseHalf = ramp.width * 0.5f * RAMP_ARROW_WIDTH_RATE;
					const float tipHalf  = baseHalf * RAMP_ARROW_TIP_RATE;
					const float start    = ramp.distance - RAMP_PAD_LENGTH * 0.5f;

					for (int i = 0; i < RAMP_ARROW_COUNT; ++i)
					{
						const float rearD = start + pitch * static_cast<float>(i);
						const float tipD  = rearD + RAMP_ARROW_LENGTH;
						if (tipD <= 0.0f || rearD >= total) { continue; }

						const auto rearFrame = stageData.spline.Evaluate(rearD);
						const auto tipFrame  = stageData.spline.Evaluate(tipD);
						const aq::math::Vector3 rearCenter = rearFrame.position + rearFrame.up * lift
						                                   + rearFrame.right * ramp.lateral;
						const aq::math::Vector3 tipCenter  = tipFrame.position + tipFrame.up * lift
						                                   + tipFrame.right * ramp.lateral;

						const uint32_t q = static_cast<uint32_t>(outVertices.size());
						PushRoadVertex(outVertices, rearCenter + rearFrame.right * -baseHalf, rearFrame.up, 0.0f, 0.0f);
						PushRoadVertex(outVertices, tipCenter  + tipFrame.right  * -tipHalf,  tipFrame.up,  0.0f, 1.0f);
						PushRoadVertex(outVertices, tipCenter  + tipFrame.right  *  tipHalf,  tipFrame.up,  1.0f, 1.0f);
						PushRoadVertex(outVertices, rearCenter + rearFrame.right *  baseHalf, rearFrame.up, 1.0f, 0.0f);

						// 表 (手前左 → 奥左 → 奥右 → 手前右) + 巻き順を反転した裏。
						EmitRoadQuad(outIndices, q + 0, q + 1, q + 2, q + 3);
						EmitRoadQuad(outIndices, q + 0, q + 3, q + 2, q + 1);
					}
				}
			}


			/**
			 * 手続き生成した路面メッシュを登録し、インスタンス点 1 個のエンティティとして置く。
			 * 形状はワールド座標で焼き込んであるので、点は原点・無回転・等倍でよい。
			 * メッシュ名が登録済みならそれが再利用される (RETRY やステージ再入場で作り直しにならない)。
			 * @param meshName 登録名 兼 デバッグ表示名
			 * @param color    per-instance color (InstancedSimple はこれで色分けする)
			 */
			void CreateRoadMeshEntity(GameFlow& flow, const char* meshName,
			                          const std::vector<aq::graphics::VertexData>& vertices,
			                          const std::vector<uint32_t>& indices,
			                          const aq::math::Vector4& color)
			{
				if (vertices.empty() || indices.empty()) { return; }

				auto* mesh = aq::graphics::InstancedStaticMesh::RegisterFromData(
					meshName,
					vertices.data(), static_cast<uint32_t>(vertices.size()), sizeof(aq::graphics::VertexData),
					indices.data(),  static_cast<uint32_t>(indices.size()),
					aq::graphics::StaticMesh::ShaderType::InstancedSimple);

				// ローカル AABB が無いとセル AABB が点になりカリングが一切効かないので必ず持たせる。
				const aq::math::AABB bounds = ComputeRoadMeshBounds(vertices);
				if (mesh != nullptr) {
					mesh->SetLocalBounds(bounds);
				}

				auto& ctx = aq::ecs::EntityContext::Get();
				auto entity = ctx.CreateEntity<
					aq::ecs::TransformComponent,
					aq::ecs::HierarchicalTransformComponent,
					aq::ecs::InstancedStaticMeshComponent,
					aq::ecs::InstancedPointListComponent>();
				entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh(meshName);

				auto* pointList = entity.GetComponent<aq::ecs::InstancedPointListComponent>();
				aq::ecs::InstancePoint p;
				p.color = color;
				pointList->AddInstancePoint(p);

				// 配置後に一切動かない静的オブジェクトなのでベイク経路に載せる。点は 1 個しかないため
				// セルサイズは 0 (= 全点を 1 セルにまとめる指定) にして、コース全長の AABB 1 個で判定する。
				// maxDrawDistance は既定 (無制限) のまま — ミニマップの俯瞰ベイクに路面を映すのに要る。
				pointList->BakeStatic(aq::math::Matrix4x4::Identity, bounds, 0.0f);
#ifdef AQ_DEBUG_IMGUI
				entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName(meshName);
#endif
				flow.StageEntities().push_back(entity.GetHandle());
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
			 * 頂点列からローカル AABB を作る。
			 * メッシュ登録 (メインスレッド) とセル AABB の算出 (ワーカー) の両方で同じ関数を通し、
			 * 値がずれてカリングが破綻するのを防ぐ。
			 * @param vertices 手続き生成したメッシュの頂点列
			 * @return 全頂点を含む AABB
			 */
			aq::math::AABB MakeMeshLocalBounds(const std::vector<aq::graphics::VertexData>& vertices)
			{
				aq::math::AABBBuilder builder;
				for (const auto& v : vertices) { builder.Add(v.position); }
				return builder.Build();
			}


			/**
			 * 草メッシュのローカル AABB。
			 * 葉の根元オフセットと傾きで XZ が房幅 (GRASS_WIDTH * 0.5) より広がるため、
			 * 寸法からの手計算ではなく実際に生成したメッシュの頂点から求める。
			 * 数十頂点しかないのでワーカーから呼んでも軽い。
			 */
			aq::math::AABB MakeGrassLocalBounds()
			{
				std::vector<aq::graphics::VertexData> vertices;
				std::vector<uint32_t>                 indices;
				aq::graphics::BuildGrassTuftMesh(vertices, indices, GRASS_BLADE_COUNT, GRASS_HEIGHT, GRASS_WIDTH);
				return MakeMeshLocalBounds(vertices);
			}


			/** 花メッシュのローカル AABB (草と同じ理由で実メッシュから求める) */
			aq::math::AABB MakeFlowerLocalBounds()
			{
				std::vector<aq::graphics::VertexData> vertices;
				std::vector<uint32_t>                 indices;
				aq::graphics::BuildFlowerMesh(vertices, indices, FLOWER_PETAL_COUNT, FLOWER_HEIGHT, FLOWER_RADIUS);
				return MakeMeshLocalBounds(vertices);
			}


			/** 草と花の散布結果。1 回のスプライン走査で両方を作るのでまとめて返す */
			struct GrassBakeResult
			{
				aq::ecs::BakedData grass;
				aq::ecs::BakedData flower;
			};


			/**
			 * 草と花の配置をベイク済みデータとして作る (純 CPU・ワーカースレッドから呼ぶ)。
			 * コーススプラインを一定間隔で歩き、1 ステップにつき GRASS_CLUSTER_PER_STEP_COUNT 個の
			 * クラスタ中心を路肩寄りの帯へ置き、その周りへ中心寄りの分布で草を固める (疎密を作るのが目的)。
			 * クラスタ単位で FLOWER_CLUSTER_RATIO を花クラスタに選び、そこにだけ花を群生させる。
			 * スプラットの草重みと傾斜による棄却は 1 本ごとに行う。
			 * 高さ・重みは HeightmapChunk::CpuData から直接引くので、チャンク生成前でも呼べる。
			 * @param stageData   コーススプラインと路面幅
			 * @param terrainDesc 地形 Desc (terrainSize / heightScale をサンプリングに使う)
			 * @param terrainCpu  PrepareCpuData 済みの地形データ
			 * @param ext         コースの XZ 範囲 (地形原点の算出に使う)
			 * @return セル分けしたベイク済み配置 (草と花。そのまま SetBakedData へ move できる)
			 */
			GrassBakeResult BuildGrassBakedData(const stage::StageData& stageData,
			                                       const aq::terrain::HeightmapChunk::Desc& terrainDesc,
			                                       const aq::terrain::HeightmapChunk::CpuData& terrainCpu,
			                                       const CourseExtents& ext)
			{
				GrassBakeResult result;
				const float total = stageData.spline.GetTotalLength();
				if (!stageData.spline.IsValid() || total <= 0.0f) { return result; }

				// 棄却されるぶんを見込んで候補を多めに歩く。歩幅が 0 以下だと無限ループになるので弾く。
				const int candidateCount = static_cast<int>(GRASS_TARGET_COUNT * GRASS_CANDIDATE_OVERSAMPLE);
				const int stepCount      = (candidateCount >= GRASS_PER_STEP_COUNT)
				                         ? candidateCount / GRASS_PER_STEP_COUNT : 1;
				const float step         = total / static_cast<float>(stepCount);
				if (step <= 0.0f) { return result; }

				// 地形エンティティの原点 (CreateStageWorld の地面ブロックと同じ式)。
				// SampleHeight は地形ローカル XZ を受け、原点 Y を含まない高さを返す。
				const aq::math::Vector3 terrainOrigin(ext.minX - TERRAIN_MARGIN,
				                                      stageData.terrainHeightOffset,
				                                      ext.minZ - TERRAIN_MARGIN);

				const float roadHalf = stageData.width * 0.5f + GRASS_ROAD_MARGIN;
				const float bandHalf = GRASS_BAND_HALF_WIDTH;
				// 背丈の勾配で |lateral| を 0-1 へ正規化するときの分母 (0 除算よけ)。
				const float lateralRange = (bandHalf > roadHalf) ? (bandHalf - roadHalf) : 1.0f;

				std::vector<aq::ecs::InstancePoint> grassPoints;
				std::vector<aq::ecs::InstancePoint> flowerPoints;
				grassPoints.reserve(static_cast<size_t>(GRASS_TARGET_COUNT));
				// 花は「花クラスタの割合 × 草 1 本あたりの本数」。5 割の余裕を見て一度だけ確保する。
				flowerPoints.reserve(static_cast<size_t>(GRASS_TARGET_COUNT * FLOWER_CLUSTER_RATIO
				                                         * FLOWER_PER_GRASS_RATIO * 1.5f));

				// 棄却の内訳。目標本数に届かないときに、しきい値と水増し率のどちらを直すかの判断材料になる。
				int candidateCounted = 0;
				int rejectedSplat    = 0;
				int rejectedSlope    = 0;
				int clusterCount     = 0;

				for (int s = 0; s < stepCount && static_cast<int>(grassPoints.size()) < GRASS_TARGET_COUNT; ++s)
				{
					const auto frame = stageData.spline.Evaluate(step * static_cast<float>(s));
					for (int c = 0; c < GRASS_CLUSTER_PER_STEP_COUNT; ++c)
					{
						// クラスタ中心の横位置は路面端から外へ u^GRASS_BAND_FALLOFF_EXPONENT で伸ばす。
						// プレイヤーが見るのは主に路肩なので、遠いほど疎になる分布にしてクラスタを手前へ寄せ、
						// 帯の端は裾を引いて途切れ目が見えないようにする。
						const float rand01        = HashNoise(s, c);
						const float side          = (rand01 < 0.5f) ? -1.0f : 1.0f;
						const float u             = (rand01 < 0.5f) ? rand01 * 2.0f : (rand01 - 0.5f) * 2.0f;
						const float centerLateral = side * (roadHalf
						                          + powf(u, GRASS_BAND_FALLOFF_EXPONENT) * (bandHalf - roadHalf));

						const aq::math::Vector3 center = frame.position + frame.right * centerLateral;

						// クラスタの属性は「量子化した中心のワールド XZ」のハッシュから作る。
						// ステップ添字ではなく位置を種にするので、再ロードでも同じ場所は同じ株になる。
						const int cx = static_cast<int>(floorf(center.x * GRASS_SEED_QUANTIZE));
						const int cz = static_cast<int>(floorf(center.z * GRASS_SEED_QUANTIZE));
						const float countRand   = HashNoise(cx + 17,  cz + 41);
						const float radiusRand  = HashNoise(cx + 59,  cz + 83);
						const float densityRand = HashNoise(cx + 107, cz + 149);
						const float flowerRand  = HashNoise(cx + 181, cz + 223);
						const float paletteRand = HashNoise(cx + 271, cz + 337);

						// 本数へ密度倍率を掛け、まばらな株と密な株を混在させる (疎密の主因)。
						const float density  = GRASS_CLUSTER_DENSITY_MIN
						                     + (GRASS_CLUSTER_DENSITY_MAX - GRASS_CLUSTER_DENSITY_MIN) * densityRand;
						const float rawCount = (static_cast<float>(GRASS_CLUSTER_MIN_COUNT)
						                     + static_cast<float>(GRASS_CLUSTER_MAX_COUNT - GRASS_CLUSTER_MIN_COUNT) * countRand)
						                     * density;
						int bladeCount = static_cast<int>(rawCount + 0.5f);
						if (bladeCount < 1) { bladeCount = 1; }

						const float clusterRadius = GRASS_CLUSTER_MIN_RADIUS
						                          + (GRASS_CLUSTER_MAX_RADIUS - GRASS_CLUSTER_MIN_RADIUS) * radiusRand;

						// 花はクラスタ単位で選ぶ。色もクラスタ単位で 1 色に固定して群生に見せる。
						const bool isFlowerCluster = (flowerRand < FLOWER_CLUSTER_RATIO);
						int paletteIndex = static_cast<int>(paletteRand * static_cast<float>(FLOWER_PALETTE_COUNT));
						if (paletteIndex >= FLOWER_PALETTE_COUNT) { paletteIndex = FLOWER_PALETTE_COUNT - 1; }
						++clusterCount;

						for (int b = 0; b < bladeCount; ++b)
						{
							// 角度は一様、半径は中心寄り (r = R * u^1.5)。中心が濃く縁がほどける株になる。
							const float angleRand  = HashNoise(cx + b * 3 + 1,  cz - b * 5 + 7);
							const float radialRand = HashNoise(cx - b * 7 + 13, cz + b * 11 + 19);
							const float angle      = angleRand * GRASS_TWO_PI;
							const float radius     = clusterRadius * powf(radialRand, GRASS_CLUSTER_RADIUS_BIAS);
							const float offsetX    = cosf(angle) * radius;
							const float offsetZ    = sinf(angle) * radius;

							const float worldX = center.x + offsetX;
							const float worldZ = center.z + offsetZ;
							const float localX = worldX - terrainOrigin.x;
							const float localZ = worldZ - terrainOrigin.z;
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

						const float worldY = terrainOrigin.y
						                   + aq::terrain::HeightmapChunk::SampleHeight(terrainCpu, terrainDesc, localX, localZ);

							// 姿勢・スケール・色のゆらぎは「量子化したワールド XZ」のハッシュから作る。
							// 位置が同じなら常に同じ値になるので、RETRY や再入場で見た目が揺れない。
						const int ix = static_cast<int>(floorf(worldX * GRASS_SEED_QUANTIZE));
						const int iz = static_cast<int>(floorf(worldZ * GRASS_SEED_QUANTIZE));
							const float yawRand     = HashNoise(ix,       iz);
							const float tiltDirRand = HashNoise(ix + 131, iz);
							const float tiltRand    = HashNoise(ix,       iz + 197);
							const float scaleYRand  = HashNoise(ix + 263, iz + 311);
							const float scaleXZRand = HashNoise(ix + 353, iz + 401);
							const float jitterRand  = HashNoise(ix + 449, iz + 503);

							// Y 回転のあとランダム方位へ倒す。直立ばかりだと同じメッシュの繰り返しが目立つ。
							aq::math::Quaternion yaw;
							aq::math::Quaternion tilt;
							yaw.SetRotation(aq::math::Vector3(0.0f, 1.0f, 0.0f), yawRand * GRASS_TWO_PI);
							const float tiltAzimuth = tiltDirRand * GRASS_TWO_PI;
							tilt.SetRotation(aq::math::Vector3(cosf(tiltAzimuth), 0.0f, sinf(tiltAzimuth)),
							                 tiltRand * GRASS_TILT_MAX_RADIAN);

							// 背丈の勾配。路面に近いほど低く、離れるほど高い。
							const float lateral = centerLateral + offsetX * frame.right.x + offsetZ * frame.right.z;
							float lateralT = (fabsf(lateral) - roadHalf) / lateralRange;
							if (lateralT < 0.0f) {
								lateralT = 0.0f;
							} else if (lateralT > 1.0f) {
								lateralT = 1.0f;
							}
							const float heightGradient = GRASS_HEIGHT_NEAR_SCALE
							                           + (GRASS_HEIGHT_FAR_SCALE - GRASS_HEIGHT_NEAR_SCALE) * lateralT;

							const float scaleY  = (GRASS_SCALE_Y_MIN
							                    + (GRASS_SCALE_Y_MAX - GRASS_SCALE_Y_MIN) * scaleYRand) * heightGradient;
							const float scaleXZ = GRASS_SCALE_XZ_MIN
							                    + (GRASS_SCALE_XZ_MAX - GRASS_SCALE_XZ_MIN) * scaleXZRand;

							// 低周波の値ノイズ 2 オクターブでまとまった色むらを作り、深緑 ↔ 黄緑を補間する。
							// per-instance のゆらぎは ±4% だけ (それ以上は白色ノイズに戻りまとまりが消える)。
							const float n0 = ValueNoise2D(worldX / GRASS_COLOR_NOISE_WAVELENGTH0,
							                              worldZ / GRASS_COLOR_NOISE_WAVELENGTH0, 0);
							const float n1 = ValueNoise2D(worldX / GRASS_COLOR_NOISE_WAVELENGTH1,
							                              worldZ / GRASS_COLOR_NOISE_WAVELENGTH1, 977);
							const float shade  = (n0 + n1 * GRASS_COLOR_NOISE_WEIGHT1) / (1.0f + GRASS_COLOR_NOISE_WEIGHT1);
							const float jitter = 1.0f + (jitterRand - 0.5f) * 2.0f * GRASS_COLOR_JITTER;

						aq::ecs::InstancePoint p;
						p.position.Set(worldX, worldY, worldZ);
							p.rotation = yaw * tilt;
							p.scale.Set(scaleXZ, scaleY, scaleXZ);
							p.color = aq::math::Vector4(
								(GRASS_COLOR_DARK_R + (GRASS_COLOR_LIGHT_R - GRASS_COLOR_DARK_R) * shade) * jitter,
								(GRASS_COLOR_DARK_G + (GRASS_COLOR_LIGHT_G - GRASS_COLOR_DARK_G) * shade) * jitter,
								(GRASS_COLOR_DARK_B + (GRASS_COLOR_LIGHT_B - GRASS_COLOR_DARK_B) * shade) * jitter,
						                            1.0f);
							grassPoints.push_back(p);

							// 花クラスタなら、草 1 本あたり FLOWER_PER_GRASS_RATIO の割合で花を添える。
							// 草と同じ位置を使うので、スプラット/傾斜の棄却をもう一度やる必要はない。
							if (isFlowerCluster && HashNoise(ix + 577, iz + 631) < FLOWER_PER_GRASS_RATIO)
							{
								const float flowerScaleRand   = HashNoise(ix + 683,  iz + 743);
								const float flowerTiltRand    = HashNoise(ix + 811,  iz + 877);
								const float flowerTiltDirRand = HashNoise(ix + 941,  iz + 997);
								const float flowerJitterRand  = HashNoise(ix + 1069, iz + 1151);

								aq::math::Quaternion flowerTilt;
								const float flowerAzimuth = flowerTiltDirRand * GRASS_TWO_PI;
								flowerTilt.SetRotation(aq::math::Vector3(cosf(flowerAzimuth), 0.0f, sinf(flowerAzimuth)),
								                       flowerTiltRand * FLOWER_TILT_MAX_RADIAN);

								const float flowerScale  = FLOWER_SCALE_MIN
								                         + (FLOWER_SCALE_MAX - FLOWER_SCALE_MIN) * flowerScaleRand;
								const float flowerJitter = 1.0f + (flowerJitterRand - 0.5f) * 2.0f * FLOWER_COLOR_JITTER;

								aq::ecs::InstancePoint f;
								f.position.Set(worldX, worldY, worldZ);
								f.rotation = yaw * flowerTilt;
								f.scale.Set(flowerScale);
								f.color = aq::math::Vector4(FLOWER_PALETTE[paletteIndex][0] * flowerJitter,
								                            FLOWER_PALETTE[paletteIndex][1] * flowerJitter,
								                            FLOWER_PALETTE[paletteIndex][2] * flowerJitter,
								                            1.0f);
								flowerPoints.push_back(f);
							}

							if (static_cast<int>(grassPoints.size()) >= GRASS_TARGET_COUNT) { break; }
						}
						if (static_cast<int>(grassPoints.size()) >= GRASS_TARGET_COUNT) { break; }
					}
				}

				aq::StartupMarkf("[load] grass scatter: %d placed / %d candidates / %d clusters"
				                 " (rejected splat %d, slope %d), flowers %d",
				                 static_cast<int>(grassPoints.size()), candidateCounted, clusterCount,
				                 rejectedSplat, rejectedSlope, static_cast<int>(flowerPoints.size()));

				// エンティティは既定変換のままなので、織り込むワールド行列は単位行列でよい。
				// 変換元は 56B/本 と大きいので、ベイクし終えたら容量ごと解放してから次へ進む。
				result.grass = aq::ecs::InstancedPointListComponent::BuildBakedData(
					grassPoints, aq::math::Matrix4x4::Identity, MakeGrassLocalBounds(), GRASS_CELL_SIZE);
				std::vector<aq::ecs::InstancePoint>().swap(grassPoints);

				result.flower = aq::ecs::InstancedPointListComponent::BuildBakedData(
					flowerPoints, aq::math::Matrix4x4::Identity, MakeFlowerLocalBounds(), GRASS_CELL_SIZE);
				std::vector<aq::ecs::InstancePoint>().swap(flowerPoints);
				return result;
			}


			/**
			 * ステージワールドを生成する (メインスレッド)。
			 * preparedTerrain はワーカーで PrepareCpuData 済みの地形データ。null なら同期で作る
			 * (画像デコード+頂点生成が乗るので Debug では 200ms 超のヒッチになる)。
			 * preparedGrass はワーカーで BuildGrassBakedData 済みの草と花の配置。null なら同期で作る
			 * (地形データの作り直し + 40 万本の散布/ベイクが乗るので同じくヒッチになる)。
			 */
			void CreateStageWorld(GameFlow& flow, const std::shared_ptr<stage::StageData>& stageData,
			                      aq::terrain::HeightmapChunk::CpuData* preparedTerrain,
			                      GrassBakeResult* preparedGrass)
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

				// 路面 (スプラインに追従する連続リボン + ライン装飾)。走行時の路面の見た目と、
				// ミニマップ (俯瞰) に映るコース形状を兼ねる。ループでも断面が路面に追従する。
				{
					std::vector<float> sectionDistances;
					BuildRoadSectionDistances(stageData->spline.GetTotalLength(), sectionDistances);

					std::vector<aq::graphics::VertexData> roadVertices;
					std::vector<uint32_t>                 roadIndices;

					// 本体は青みグレー (仮アセット。路面テクスチャ導入までの色分け)。
					BuildRoadRibbonMesh(*stageData, sectionDistances, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "RoadRibbon", roadVertices, roadIndices,
					                     aq::math::Vector4(0.30f, 0.34f, 0.42f, 1.0f));
					const size_t ribbonVertexCount = roadVertices.size();

					// 装飾は色を変えたいだけなので、テクスチャではなくメッシュを分けて出す。
					BuildRoadEdgeLineMesh(*stageData, sectionDistances, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "RoadEdgeLines", roadVertices, roadIndices,
					                     aq::math::Vector4(0.75f, 0.95f, 1.00f, 1.0f));

					BuildRoadCenterDashMesh(*stageData, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "RoadCenterDashes", roadVertices, roadIndices,
					                     aq::math::Vector4(0.95f, 0.97f, 1.00f, 1.0f));

					// ブーストパッド (P21)。踏むと一定時間だけ最高速が上がる。下地と矢印で色を分ける。
					BuildBoostPadPlateMesh(*stageData, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "BoostPads", roadVertices, roadIndices,
					                     aq::math::Vector4(0.10f, 0.55f, 0.75f, 1.0f));

					BuildBoostPadArrowMesh(*stageData, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "BoostPadArrows", roadVertices, roadIndices,
					                     aq::math::Vector4(0.70f, 1.00f, 1.00f, 1.0f));

					// ジャンプ台 (P22)。ブーストパッドと区別できるようオレンジ系にする。
					BuildRampPadPlateMesh(*stageData, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "RampPads", roadVertices, roadIndices,
					                     aq::math::Vector4(0.85f, 0.45f, 0.08f, 1.0f));

					BuildRampPadArrowMesh(*stageData, roadVertices, roadIndices);
					CreateRoadMeshEntity(flow, "RampPadArrows", roadVertices, roadIndices,
					                     aq::math::Vector4(1.00f, 0.92f, 0.70f, 1.0f));

					aq::StartupMarkf("[load]   road ribbon %zu sections / %zu vertices"
					                 " / boost pads %zu / ramps %zu",
					                 sectionDistances.size(), ribbonVertexCount,
					                 stageData->boostPads.size(), stageData->ramps.size());
				}
				aq::StartupMark("[load]   road mesh done");

				// 草と花 (手続き生成のメッシュ + 風揺れ)。散布とベイクはワーカー (BuildGrassBakedData) 側で
				// 終えてあり、ここは GPU メッシュの登録・エンティティ生成・ベイク結果の move だけを行う。
				GrassBakeResult fallbackBake;
				GrassBakeResult* scattered = preparedGrass;
				if (scattered == nullptr) {
					// フォールバック: ワーカーの成果物なしで呼ばれた場合 (この関数の単体利用)。
					// 地形 CPU データの作り直しと 40 万本の散布/ベイクがメインスレッドに乗るのでヒッチする。
					const aq::terrain::HeightmapChunk::Desc grassTerrainDesc = MakeTerrainDesc(*stageData, ext);
					fallbackBake = BuildGrassBakedData(
						*stageData, grassTerrainDesc,
						aq::terrain::HeightmapChunk::PrepareCpuData(grassTerrainDesc), ext);
					scattered = &fallbackBake;
				}

				// 草。
				{
					// 房メッシュはここで一度だけ生成する。同名が登録済みならそれが返るので、
					// RETRY やステージ再入場で作り直しにはならない。
					std::vector<aq::graphics::VertexData> grassVertices;
					std::vector<uint32_t>                 grassIndices;
					aq::graphics::BuildGrassTuftMesh(grassVertices, grassIndices,
					                                 GRASS_BLADE_COUNT, GRASS_HEIGHT, GRASS_WIDTH);

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
					pointList->SetBakedData(std::move(scattered->grass));
					// 草は小さいので遠景では見えない。路面と違いミニマップにも要らないので距離で切る。
					pointList->SetMaxDrawDistance(GRASS_DRAW_DISTANCE);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Grass");
#endif
					flow.StageEntities().push_back(entity.GetHandle());
				}

				// 花 (メッシュが違うので別エンティティ + 別 InstancedPointList。
				// シェーダは per-instance 色 1 色で塗れる草と同じ InstancedGrass を流用する)。
				{
					std::vector<aq::graphics::VertexData> flowerVertices;
					std::vector<uint32_t>                 flowerIndices;
					aq::graphics::BuildFlowerMesh(flowerVertices, flowerIndices,
					                              FLOWER_PETAL_COUNT, FLOWER_HEIGHT, FLOWER_RADIUS);

					auto* flowerMesh = aq::graphics::InstancedStaticMesh::RegisterFromData(
						"Flower",
						flowerVertices.data(), static_cast<uint32_t>(flowerVertices.size()), sizeof(aq::graphics::VertexData),
						flowerIndices.data(),  static_cast<uint32_t>(flowerIndices.size()),
						aq::graphics::StaticMesh::ShaderType::InstancedGrass);
					if (flowerMesh != nullptr) {
						// ベイク時のセル AABB と同じ値にするため MakeFlowerLocalBounds から取る。
						flowerMesh->SetLocalBounds(MakeFlowerLocalBounds());
						// 風は草より弱く速め。茎の無い軽い花冠が細かく震えるように見せる。
						flowerMesh->SetWindParams(FLOWER_WIND_STRENGTH, FLOWER_WIND_FREQUENCY,
						                          aq::math::Vector3(1.0f, 0.0f, 0.35f));
					}

					auto entity = ctx.CreateEntity<
						aq::ecs::TransformComponent,
						aq::ecs::HierarchicalTransformComponent,
						aq::ecs::InstancedStaticMeshComponent,
						aq::ecs::InstancedPointListComponent>();
					entity.GetComponent<aq::ecs::InstancedStaticMeshComponent>()->SetMesh("Flower");

					auto* pointList = entity.GetComponent<aq::ecs::InstancedPointListComponent>();
					pointList->SetBakedData(std::move(scattered->flower));
					// 花は草よりさらに小さいので、草 (120m) より手前で切る。
					pointList->SetMaxDrawDistance(FLOWER_DRAW_DISTANCE);
#ifdef AQ_DEBUG_IMGUI
					entity.GetComponent<aq::ecs::EntityDebugTag>()->SetName("Flowers");
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
						// ブースト状態 (P21)。prevDistance を戻さないと「巨大な前フレーム距離」が残り、
						// 跨ぎ判定が成立せず前半のパッドが全て無視される。
						character->boostTimer       = 0.0f;
						character->prevDistance     = character->distance;
						// エアトリック (P22)。特に trickRoll を残すと機体が傾いたまま復帰する。
						character->trickSpinTimer      = 0.0f;
						character->trickPendingCount   = 0;
						character->trickCompletedCount = 0;
						character->trickRoll           = 0.0f;
					}
					if (auto* score = ctx.GetComponent<app::ecs::PlayerScoreComponent>(session->playerHandle)) {
						score->coinCount  = 0;
						score->fallCount  = 0;
						// コンボとスコア (P21)。CoinSystem::ReactivateAll は取得フラグしか戻さない。
						score->comboCount = 0;
						score->comboTimer = 0.0f;
						score->bestCombo  = 0;
						score->score      = 0;
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

				// 破棄するエンティティ (地形など) は自前の GPU バッファを持つ。在フライトの
				// 描画フレームがそれらを参照したまま解放すると device removed でクラッシュするため、
				// レンダースレッドを完全にドレインして GPU アイドルにしてから、遅延コマンドを
				// 即時フラッシュして破棄を確定させる (この時点で参照は自分だけなので安全に解放できる)。
				if (app::Application::IsAvailable()) {
					app::Application::Get().WaitForRenderIdle();
				}
				aq::ecs::EntityContext::Get().FlushPendingCommands();
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

			// 選択中ステージ名とサムネイルをタイトルへ反映する。
			const auto& list = flow.StageList();
			if (!list.empty()) {
				const int index = aq::math::Clamp(flow.SelectedStageIndex(), 0, static_cast<int>(list.size()) - 1);
				if (auto* screen = static_cast<TitleScreen*>(aq::ui::UIContext::Get().Screens().Top())) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "STAGE %02d    %s", index + 1, list[index].name.c_str());
					screen->SetStageName(buf);
					screen->SetStageThumbnail(list[index].thumbnailPath.c_str());
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

							// 草と花の散布とベイク。地形の高さ/スプラットを引くので地形データの後に行う。
							GrassBakeResult baked = BuildGrassBakedData(*result.stage, terrainDesc, result.terrainCpu, ext);
							aq::StartupMarkf("[load] grass scattered and baked (worker): %zu instances / %zu cells"
							                 " (flowers %zu instances / %zu cells)",
							                 baked.grass.instances.size(),  baked.grass.cells.size(),
							                 baked.flower.instances.size(), baked.flower.cells.size());
							result.grassBaked  = std::move(baked.grass);
							result.flowerBaked = std::move(baked.flower);
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
				// ワーカーが別々に持ってきた草と花のベイク結果をまとめ直してから渡す。
				GrassBakeResult scattered;
				scattered.grass  = std::move(result.grassBaked);
				scattered.flower = std::move(result.flowerBaked);
				CreateStageWorld(flow, stageData, &result.terrainCpu, &scattered);   // GPU 生成のみ (CPU 前計算はワーカー済み)
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

			// ミニマップ: 真上からの正射影でシーンを RT へ 1 回だけベイクし、その画像を貼る。
			// 構図はマーカーの UV 式と同じ minimapCenterXZ ± minimapHalfExtent。
			// up を +Z に取ると RT は「画面右=+X / 画面上=+Z」になり、
			// SetMinimapMarker の u=+X / v=-Z (下+) と一致する。
			auto* screen = static_cast<InGameScreen*>(aq::ui::UIContext::Get().Screens().Top());
			const bool minimapReady = session->minimapHalfExtent > 1.0f && app::Application::IsAvailable();
			if (minimapReady)
			{
				// 俯瞰は正射影なので高さは構図に影響しない。地形 (最大約 31m) とループを
				// 確実に前後クリップの内側へ収められる値にする。
				constexpr float CAMERA_HEIGHT = 2000.0f;
				constexpr float CAMERA_NEAR   = 1.0f;
				constexpr float CAMERA_FAR    = 4000.0f;

				const float centerX = session->minimapCenterXZ.x;
				const float centerZ = session->minimapCenterXZ.y;
				const float span    = session->minimapHalfExtent * 2.0f;

				aq::Camera* const camera = aq::CameraManager::Get().GetCamera(aq::CameraType::Offscreen);
				camera->SetPosition(aq::math::Vector3(centerX, CAMERA_HEIGHT, centerZ));
				camera->SetTarget(aq::math::Vector3(centerX, 0.0f, centerZ));
				camera->SetUp(aq::math::Vector3(0.0f, 0.0f, 1.0f));
				camera->SetNear(CAMERA_NEAR);
				camera->SetFar(CAMERA_FAR);
				camera->SetOrthographic(span, span);

				// RETRY / ステージ再入場でもここを通るので毎回ベイクし直す。
				app::Application& application = app::Application::Get();
				application.RequestMinimapBake();

				if (screen) {
					screen->SetMinimapTexture(aq::graphics::GraphicsDevice::Get()
						.GetRenderTargetSRVShared(application.GetMinimapRT()));
				}
			}
			else if (screen)
			{
				screen->SetMinimapTexture(nullptr);
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
				// コンボ (P21)。倍率 1 のときは HUD 側が非表示にする。
				const uint32_t comboMultiplier =
					app::ecs::CoinSystem::CalcMultiplier(score ? score->comboCount : 0);
				const float comboWindow = app::ecs::CoinSystem::GetComboWindowSec();
				const float comboRate   = (score && comboWindow > 0.0f)
				                        ? aq::math::Clamp01(score->comboTimer / comboWindow) : 0.0f;
				// エアトリック (P22)。滞空中に回転しているか、回し終えて着地待ちの間だけ出す。
				const bool trickActive = !character->grounded
				                      && (character->trickSpinTimer > 0.0f
				                          || character->trickCompletedCount > 0);
				screen->SetHUD(elapsed_, score ? score->coinCount : 0, character->speed * 3.6f,
				               comboMultiplier, comboRate,
				               character->trickCompletedCount, trickActive);

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
			bool        goal     = character->distance >= stageData->goalDistance;
			const bool  fall     = character->height < stageData->fallHeight
			                    || (character->fallen && playerTc && playerTc->position.y < 0.5f);
#ifdef _DEBUG
			// デバッグ: G で即クリア (リザルト遷移や BACK TO TITLE のデバッグを走り切らずに試す)。
			if (aq::hid::IsKeyTriggered(aq::hid::KeyBoardType::G)) { goal = true; }
#endif
			if (!goal && !fall) { return; }

			PlayResult& result  = flow.PlayResult();
			result.cleared      = goal;
			result.clearTimeSec = elapsed_;
			result.coinCount    = score ? score->coinCount : 0;
			result.score        = score ? score->score     : 0;
			result.bestCombo    = score ? score->bestCombo : 0;

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
					// ランクはコイン枚数ではなくスコア (コンボ倍率込み) で判定する (P21)。
					rank = session->activeStage->CalcRank(result.score, result.clearTimeSec);
				}
				screen->SetResult(result.cleared, result.clearTimeSec, result.coinCount,
				                  result.score, result.bestCombo, rank.c_str());
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
