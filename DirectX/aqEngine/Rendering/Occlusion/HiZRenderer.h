#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "Rendering/RenderTargetHandle.h"
#include "Math/Matrix.h"
#include "IOcclusionTester.h"
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#endif

namespace aq
{
	namespace graphics { class IShader; class IConstantBuffer; }

	namespace rendering
	{
		class RenderCommandList;
		struct RenderFrame;

		/** Hi-Z 再構成シェーダー用 定数バッファ (b0)。HiZReconstruct.fx の HiZCB と一致させること。 */
		struct HiZCBData
		{
			math::Matrix4x4 viewProj;  // view * projection
			float           nearZ = 0.1f;
			float           farZ  = 1000.0f;
			float           pad0  = 0.0f;
			float           pad1  = 0.0f;
		};
		static_assert(sizeof(HiZCBData) == 80, "HiZCBData must match HiZReconstruct.fx cbuffer layout");


		/**
		 * Hi-Z (階層 Z) ピラミッド生成器。
		 *
		 * GBuffer2 の worldPos からクリップ空間深度を再構成し、max-reduction の
		 * ミップ連鎖 (各レベル = 最遠深度) を compute で構築する。
		 * オクリュージョンテスト (後続フェーズ) と デバッグ可視化に使う。
		 *
		 * レベル0 = フル解像度の 1/2。以降は各軸 1/2 ずつ。
		 */
		class HiZRenderer : public IOcclusionTester
		{
		public:
			static constexpr uint32_t kMaxLevels = 8;

			bool Initialize(uint32_t fullWidth, uint32_t fullHeight);

			/**
			 * リサイズ(設計書/エンジン課題解消設計.md P4)。インスタンスは作り直さない
			 * (AppHost が SetOcclusionTester で生ポインタ登録しているため)。ミップ連鎖(段数と各段の RT)だけを入れ替える。
			 * ゲームスレッドで、レンダースレッド停止中(WaitIdle 済み)にだけ呼ぶ。
			 *   PrepareResize: 新寸法のミップ連鎖を一時領域へ作る(旧はそのまま)。失敗時は自己清掃して false
			 *   CommitResize : 旧ミップ連鎖を解放して一時領域と入れ替える。CPU リードバックも破棄する(旧寸法のため)
			 *   AbortResize  : 一時領域を解放する(何度呼んでも安全)
			 */
			bool PrepareResize(uint32_t fullWidth, uint32_t fullHeight);
			void CommitResize();
			void AbortResize();

			/**
			 * Hi-Z チェーン構築 + リードバックコマンドを outList に積む。
			 * GBuffer パスの後 (worldPos 確定後) に呼ぶこと。
			 * gbufferWorldPos = DeferredRenderer::GetGBuffer2Handle()。
			 */
			void BuildCommandList(const RenderFrame& frame, RenderCommandList& outList,
			                      RenderTargetHandle gbufferWorldPos);

			uint32_t           GetLevelCount() const { return levels_.count; }
			RenderTargetHandle GetLevelHandle(uint32_t i) const { return levels_.handles[i]; }
			void GetLevelSize(uint32_t i, uint32_t& w, uint32_t& h) const { w = levels_.w[i]; h = levels_.h[i]; }

			/** CPU リードバック用に選んだレベル (粗いミップ)。 */
			uint32_t           GetReadbackLevel()  const { return levels_.readbackLevel; }
			RenderTargetHandle GetReadbackHandle() const { return levels_.handles[levels_.readbackLevel]; }

			/** レンダースレッドから読み戻した Hi-Z を格納する (HiZReadbackCommand が呼ぶ)。 */
			void StoreReadback(std::vector<float>&& data, uint32_t w, uint32_t h);

			// IOcclusionTester
			bool IsOccluded(const math::AABB& worldBox,
			                const math::Matrix4x4& viewProj,
			                float nearZ, float farZ) const override;

			/** デバッグ: CPU リードバック Hi-Z の受信状況と統計。 */
			struct ReadbackInfo
			{
				bool     hasData = false;
				uint32_t width   = 0;
				uint32_t height  = 0;
				float    minV    = 0.0f;
				float    maxV    = 0.0f;
			};
			ReadbackInfo GetReadbackInfo() const;

			/**
			 * デバッグ: 遮蔽テストの数式を端から端まで検証する自己テスト。
			 * 読み戻した Hi-Z 上で実深度を持つ画素を選び、その点に「手前」「奥」の小 AABB を
			 * 逆投影で作り、IsOccluded が near=false / far=true を返すか確認する。
			 * シーンに遮蔽が無くても判定ロジックの正しさを確認できる。
			 */
			struct SelfTestResult
			{
				bool  valid       = false;  // 有効な検証画素が見つかったか
				bool  pass        = false;  // near=可視 かつ far=遮蔽
				float sampleDepth = 0.0f;   // 検証に使った Hi-Z 線形深度
				bool  nearOccluded = false;
				bool  farOccluded  = false;
			};
			SelfTestResult SelfTest(const math::Matrix4x4& viewProj,
			                        const math::Vector3& camPos,
			                        const math::Vector3& camForward,
			                        float nearZ, float farZ) const;

#ifdef AQ_DEBUG_IMGUI
			std::unique_ptr<IDebugRenderable> CreateDebugPanel();
#endif

		private:
			/** ミップ連鎖(寸法で段数と各段の RT が決まる) */
			struct LevelSet
			{
				RenderTargetHandle handles[kMaxLevels];
				uint32_t           w[kMaxLevels] = {};
				uint32_t           h[kMaxLevels] = {};
				uint32_t           count         = 0;
				uint32_t           readbackLevel = 0;
			};

			/** フル解像度 fullWidth × fullHeight からミップ連鎖を out へ作る。失敗したら作った分を解放して false */
			static bool CreateLevels(uint32_t fullWidth, uint32_t fullHeight, LevelSet& out);
			/** out の RT を解放して空にする(何度呼んでも安全) */
			static void ReleaseLevels(LevelSet& levels);


		private:
			std::shared_ptr<graphics::IShader>         reconstructShader_;
			std::shared_ptr<graphics::IShader>         downsampleShader_;
			std::unique_ptr<graphics::IConstantBuffer> hiZCB_;

			LevelSet levels_;
			/** PrepareResize が作った新ミップ連鎖(CommitResize で levels_ へ入れ替わる) */
			LevelSet pendingLevels_;

			// CPU リードバックした Hi-Z (レンダースレッド書き込み / ゲームスレッド読み取り)
			mutable std::mutex   cpuHiZMutex_;
			std::vector<float>   cpuHiZ_;
			uint32_t             cpuHiZW_ = 0;
			uint32_t             cpuHiZH_ = 0;
		};
	}
}
