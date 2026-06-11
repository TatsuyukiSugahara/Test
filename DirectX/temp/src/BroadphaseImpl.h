/**
 * BroadphaseImpl.h
 * 広域衝突判定の実装
 */
#pragma once
#include "BroadphaseInterface.h"
#include "btBulletDynamicsCommon.h"
#include "GhostBody.h"


/**
 * 実装: BulletのDynamic AABB Tree (btDbvt)
 */
class BulletDbvtBroadphase : public IBroadphase
{
private:
	/** btDbvt用の衝突コールバック */
	struct DbvtCollideCallback : btDbvt::ICollide
	{
		PairCallback userCallback;
		void Process(const btDbvtNode* n1, const btDbvtNode* n2)
		{
			GhostBody* b1 = static_cast<GhostBody*>(n1->data);
			GhostBody* b2 = static_cast<GhostBody*>(n2->data);
			userCallback(b1, b2);
		}
	};



private:
	/** btDbvtインスタンス */
	btDbvt* tree_ = nullptr;

	
public:
	BulletDbvtBroadphase()
	{
		tree_ = new btDbvt();
	}
	~BulletDbvtBroadphase()
	{
		delete tree_;
	}

	void Add(GhostBody* body) override
	{
		btVector3 minAabb, maxAabb;
		body->ComputeAabb(minAabb, maxAabb);
		btDbvtVolume volume = btDbvtVolume::FromMM(minAabb, maxAabb);
		// ハンドルとしてNodeポインタを渡す
		btDbvtNode* node = tree_->insert(volume, body);
		body->SetBroadphaseHandle(static_cast<void*>(node));
	}

	void Remove(GhostBody* body) override
	{
		btDbvtNode* node = static_cast<btDbvtNode*>(body->GetBroadphaseHandle());
		if (node) {
			tree_->remove(node);
			body->SetBroadphaseHandle(nullptr);
		}
	}

	void Update(GhostBody* body) override
	{
		btDbvtNode* node = static_cast<btDbvtNode*>(body->GetBroadphaseHandle());
		if (node) {
			btVector3 minAabb, maxAabb;
			body->ComputeAabb(minAabb, maxAabb);
			btDbvtVolume volume = btDbvtVolume::FromMM(minAabb, maxAabb);
			tree_->update(node, volume);
		}
	}

	void Perform(PairCallback callback) override
	{
		if (tree_) {
			DbvtCollideCallback cb;
			cb.userCallback = callback;
			tree_->collideTT(tree_->m_root, tree_->m_root, cb);
		}
	}
};