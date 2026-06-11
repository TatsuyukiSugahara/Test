#pragma once


namespace engine
{
	namespace physics
	{
		class GhostBody;


		/**
		 * ゴーストボディのナローフェーズ（詳細判定）抽象インターフェース。
		 *
		 * Broadphase が候補ペアを絞り込んだ後、このインターフェースで
		 * 正確な重なり判定を行う。
		 *
		 * バックエンド実装例:
		 *   BulletGhostNarrowphase  ... Bullet の btCollisionAlgorithm を使用
		 *   PhysXGhostNarrowphase   ... PhysX の PxScene::overlap を使用（将来）
		 *
		 * 差し替え方:
		 *   GhostBodyManager::Initialize(broadphase, std::make_unique<MyNarrowphase>())
		 *   または
		 *   GhostBodyManager::Get().SetNarrowphase(std::make_unique<MyNarrowphase>())
		 */
		class IGhostNarrowphase
		{
		public:
			virtual ~IGhostNarrowphase() = default;

			/** ボディが追加されたときに呼ばれる（バックエンドリソースの確保） */
			virtual void OnBodyAdded  (GhostBody* body) = 0;

			/** ボディが削除されたときに呼ばれる（バックエンドリソースの解放） */
			virtual void OnBodyRemoved(GhostBody* body) = 0;

			/**
			 * 2 つのボディが実際に重なっているか判定する。
			 * Broadphase の候補に対して呼ばれる（a.ShapeType <= b.ShapeType が保証済み）。
			 */
			virtual bool CheckCollision(GhostBody* a, GhostBody* b) = 0;
		};
	}
}
