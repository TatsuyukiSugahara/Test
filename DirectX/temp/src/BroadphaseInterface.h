/**
 * BroadphaseInterface.h
 * 広域衝突判定のインターフェース
 */
#pragma once


class GhostBody;


/**
 * Broadphase（広域衝突判定）の抽象インターフェース
 */
class IBroadphase
{
protected:
	using PairCallback = std::function<void(GhostBody* a, GhostBody* b)>;


public:
	virtual ~IBroadphase() = default;

	virtual void Add(GhostBody* body) = 0;
	virtual void Remove(GhostBody* body) = 0;
	virtual void Update(GhostBody* body) = 0;

	virtual void Perform(PairCallback callback) = 0;
};