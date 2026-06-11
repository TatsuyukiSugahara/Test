#pragma once
#include "IBroadphase.h"
#include "IGhostNarrowphase.h"
#include <memory>
#include <vector>


namespace engine
{
	namespace physics
	{
		class GhostBody;


		/**
		 * GhostBody を一括管理し、毎フレーム衝突判定を行うクラス。
		 *
		 * 衝突通知:
		 *   RegisterCallback() で std::function を登録すると、
		 *   当たっているペア (a, b) が検出されるたびに呼ばれる。
		 *
		 * アルゴリズムの差し替え:
		 *   Broadphase  : IBroadphase  を継承して SetBroadphase()  に渡す
		 *   Narrowphase : IGhostNarrowphase を継承して SetNarrowphase() に渡す
		 *   デフォルト: BulletDbvtBroadphase + BulletGhostNarrowphase
		 */
		class GhostBodyManager
		{
		public:
			using PairCallback = std::function<void(GhostBody* a, GhostBody* b)>;


		private:
			std::unique_ptr<IBroadphase>      broadphase_;
			std::unique_ptr<IGhostNarrowphase> narrowphase_;
			std::vector<GhostBody*>            bodyList_;
			PairCallback                       pairCallback_;

			GhostBodyManager();
			~GhostBodyManager();


		public:
			/** 毎フレーム呼ぶ。移動検知 → Broadphase → Narrowphase を一括処理 */
			void Update();

			void AddBody   (GhostBody* body);
			void RemoveBody(GhostBody* body);

			void RegisterCallback(const PairCallback& cb) { pairCallback_ = cb; }
			void ClearCallback()                           { pairCallback_ = nullptr; }

			/** Broadphase アルゴリズムの差し替え */
			void SetBroadphase (std::unique_ptr<IBroadphase>      bp) { broadphase_  = std::move(bp); }
			/** Narrowphase アルゴリズムの差し替え */
			void SetNarrowphase(std::unique_ptr<IGhostNarrowphase> np) { narrowphase_ = std::move(np); }


		private:
			void ProcessCollisionPair(GhostBody* a, GhostBody* b);


			static GhostBodyManager* instance_;


		public:
			/**
			 * broadphase / narrowphase が nullptr の場合は
			 * BulletDbvtBroadphase / BulletGhostNarrowphase をデフォルト使用
			 */
			static void              Initialize(std::unique_ptr<IBroadphase>       broadphase  = nullptr,
			                                    std::unique_ptr<IGhostNarrowphase>  narrowphase = nullptr);
			static void              Finalize();
			static GhostBodyManager& Get();
			static bool              IsInitialized() { return instance_ != nullptr; }
		};
	}
}
