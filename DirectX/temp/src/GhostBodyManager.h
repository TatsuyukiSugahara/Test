/**
 * GhostBodyManager.h
 * ゴーストボディ管理関連
 */
#pragma once
#include "BroadphaseInterface.h"



/**
 * ゴーストボディ管理クラス
 */
class GhostBodyManager
{
private:
	using RegisterPairCallback = std::function<void(GhostBody* a, GhostBody* b)>;


private:
	/** Broadphaseアルゴリズム（抽象化） */
	std::unique_ptr<IBroadphase> broadphase_;

	/** 管理リスト（Dirtyチェック用） */
	std::vector<GhostBody*> bodyList_;

	/** 衝突ペア登録コールバック */
	RegisterPairCallback registerPairCallback_ = nullptr;


private:
	GhostBodyManager();
	~GhostBodyManager();

public:
	void Update();

	void AddBody(GhostBody* body);
	void RemoveBody(GhostBody* body);

	void RegisterCallback(const RegisterPairCallback& callback) { registerPairCallback_ = callback; }
	void ClearCallback() { registerPairCallback_ = nullptr; }

private:
	/** 衝突ペアの処理 */
	void ProcessCollisionPair(GhostBody* a, GhostBody* b);

	/** Bulletを使った詳細判定 */
	bool CheckCollisionBullet(GhostBody* a, GhostBody* b);



	/**
	 * シングルトン関連
	 */
private:
	static GhostBodyManager* m_instance;


public:
	static void Initialize() { if (!m_instance) m_instance = new GhostBodyManager(); }
	static void Finalize() { if (m_instance) { delete m_instance; m_instance = nullptr; } }
	static GhostBodyManager& Get() { return *m_instance; }
	static bool IsAvailable() { return m_instance != nullptr; }
};


