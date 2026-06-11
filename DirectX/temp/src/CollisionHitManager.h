/**
 * CollisionHitManager.h
 * 衝突ヒット管理
 *
 * ペアの状態遷移:
 *   無衝突 → Enter(触れた瞬間) → Stay(継続中) → Exit(離れた瞬間) → 無衝突
 */
#pragma once


class GhostBody;


/**
 * ペアの衝突状態
 */
enum class CollisionPairState
{
	Enter,	// 今フレームで初めて接触
	Stay,	// 前フレームから継続して接触中
	Exit,	// 今フレームで離れた
};


/**
 * 衝突ヒット管理クラス
 *
 * Enter/Stay/Exit の3状態を管理し、
 * 「一度だけの処理」「継続的な処理」「離脱時の処理」を区別する。
 */
class CollisionHitManager
{
public:
	struct Pair
	{
		GhostBody* a = nullptr;
		GhostBody* b = nullptr;
		//
		Pair(GhostBody* bodyA, GhostBody* bodyB)
			: a(bodyA), b(bodyB)
		{
		}
	};


private:
	/**
	 * ペアキー
	 * ポインタを正規化して (a,b) と (b,a) を同一視する
	 */
	struct PairKey
	{
		GhostBody* a;
		GhostBody* b;

		PairKey(GhostBody* bodyA, GhostBody* bodyB)
			: a(bodyA < bodyB ? bodyA : bodyB)
			, b(bodyA < bodyB ? bodyB : bodyA)
		{
		}

		bool operator==(const PairKey& other) const
		{
			return a == other.a && b == other.b;
		}
	};

	struct PairKeyHash
	{
		size_t operator()(const PairKey& key) const
		{
			size_t h1 = std::hash<GhostBody*>()(key.a);
			size_t h2 = std::hash<GhostBody*>()(key.b);
			return h1 ^ (h2 * 2654435761u);
		}
	};

	/**
	 * ペアの状態情報
	 */
	struct PairInfo
	{
		CollisionPairState state;
		int frameCount;
		bool flaggedThisFrame;

		PairInfo()
			: state(CollisionPairState::Enter)
			, frameCount(0)
			, flaggedThisFrame(false)
		{
		}
	};


private:
	/** 今フレームのヒットペアリスト（RegisterHitPairで蓄積） */
	std::vector<Pair> hitPairList_;

	/** アクティブペアの状態マップ */
	std::unordered_map<PairKey, PairInfo, PairKeyHash> activePairs_;


private:
	CollisionHitManager();
	~CollisionHitManager();


public:
	/** 更新（毎フレーム呼ぶ） */
	void Update();

	/** 衝突ペア登録（Narrowphase判定後に呼ばれる） */
	void RegisterHitPair(GhostBody* a, GhostBody* b);

	/**
	 * ボディ削除通知
	 * GhostBody破棄時にダングリングポインタを防ぐため、
	 * 関連するペアを全て除去する。
	 */
	void OnBodyRemoved(GhostBody* body);

	/* コリジョンがヒットしたときに流すSE */
	void UpdateTakeHitSound();
	void UpdateAttackHitSound();

private:
	/** Enter: 衝突した瞬間の処理 : 一度だけ実行したい処理 */
	void OnCollisionEnter(GhostBody* a, GhostBody* b);
	/** Stay: 継続衝突の処理 : 継続的に実行したい処理 */
	void OnCollisionStay(GhostBody* a, GhostBody* b, int frameCount);
	/** Exit: 離脱した瞬間の処理 : 離脱時に一度だけ実行したい処理 */
	void OnCollisionExit(GhostBody* a, GhostBody* b);

	/* プレイヤーの攻撃 */
	bool ContainsPlayerNormalAttackPair(const Pair& hitPair);	//!< 通常攻撃のフラグ取得
	void UpdatePlayerNormalAttackPair(Pair& hitPair);			//!< 通常攻撃の更新
	bool ContainsPlayerSkillAttackPair(const Pair& hitPair);	//!< スキル攻撃のフラグ取得
	void UpdatePlayerSkillAttackPair(Pair& hitPair);			//!< スキル攻撃の更新


	/* ボスの攻撃 */
	bool ContainsBossAttackPair(const Pair& hitPair);	//!< 通常攻撃のフラグ取得
	void UpdateBossAttackPair(Pair& hitPair);			//!< 通常攻撃の更新
	bool ContainsBossHitStampPair(const Pair& hitPair);	//!< ヒットスタンプ攻撃のフラグ取得
	void UpdateBossHitStampPair(Pair& hitPair);			//!< ヒットスタンプ攻撃の更新
	bool ContainsBossSpinPair(const Pair& hitPair);	//!< 回転攻撃のフラグ取得
	void UpdateBossSpinPair(Pair& hitPair);			//!< 回転攻撃の更新
	bool ContainsBossThrowRockPair(const Pair& hitPair);	//!< 岩を投げるのフラグ取得
	void UpdateBossThrowRockPair(Pair& hitPair);			//!< 岩を投げるの更新
	bool ContainsBossLaserWeakPair(const Pair& hitPair);	//!< 弱いレーザーのフラグ取得
	void UpdateBossLaserWeakPair(Pair& hitPair);			//!< 弱いレーザーの更新
	bool ContainsBossLaserStrongPair(const Pair& hitPair);	//!< 強いレーザーのフラグ取得
	void UpdateBossLaserStrongPair(Pair& hitPair);			//!< 強いレーザーの更新

	/* キャラクターの誰かの攻撃 */
	bool ContainsCharacterLandminePlayerPair(const Pair& hitPair); // 地雷(対プレイヤー)のフラグ取得
	void UpdateCharacterLandminePlayerPair(Pair& hitPair); // 地雷(対プレイヤー)の更新
	bool ContainsCharacterLandmineBossPair(const Pair& hitPair); // 地雷(対ボス)のフラグ取得
	void UpdateCharacterLandmineBossPair(Pair& hitPair);// 地雷(対ボス)の更新

	/**
	 * シングルトン関連
	 */
private:
	static CollisionHitManager* instance_;


public:
	static void Initialize()
	{
		if (!instance_) {
			instance_ = new CollisionHitManager();
		}
	}
	static CollisionHitManager& Get() { return *instance_; }
	static bool IsAvailable() { return instance_ != nullptr; }
	static void Finalize()
	{
		if (instance_) {
			delete instance_;
			instance_ = nullptr;
		}
	}
};


