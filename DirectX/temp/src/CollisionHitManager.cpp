#include "stdafx.h"
#include "CollisionHitManager.h"
#include "src/Actor/ActorStatus.h"
#include "src/Actor/Character.h"
#include "src/Actor/BossCharacter.h"
#include "src/Actor/Player.h"
#include "src/Util/DamageCalculator.h"
#include "src/Actor/AttackObject.h"
#include "src/Core/ParameterManager.h"


namespace
{
	/* テンプレート */
	template <typename T>
	bool IsHitObject(const CollisionHitManager::Pair& pair, const uint32_t id)
	{
		if (pair.a->GetOwnerId() == id) {
			return true;
		}
		if (pair.b->GetOwnerId() == id) {
			return true;
		}
		return false;
	}

	template <typename T>
	T* GetHitObject(CollisionHitManager::Pair& pair, const uint32_t id)
	{
		if (pair.a->GetOwnerId() == id) {
			return static_cast<T*>(pair.a->GetOwner());
		}
		if (pair.b->GetOwnerId() == id) {
			return static_cast<T*>(pair.b->GetOwner());
		}
		return nullptr;
	}

	GhostBody* GetGhostBodyById(CollisionHitManager::Pair& pair, const uint32_t id)
	{
		if (pair.a->GetOwnerId() == id) return pair.a;
		if (pair.b->GetOwnerId() == id) return pair.b;
		return nullptr;
	}

	// 攻撃ボディ位置からボスのコリジョン表面座標を計算する
	// 攻撃体はヒット検出時点でボス表面付近に位置するため、その座標をそのまま使う
	Vector3 CalcBossSurfacePos(GhostBody* attackBody, GhostBody* bossBody)
	{
		return attackBody->GetPosition();
	}

	// ボス攻撃ボディからプレイヤーのGhostBodyにRayを飛ばしてヒット地点を返す
	Vector3 CalcPlayerHitPos(GhostBody* bossAtkBody, GhostBody* playerBody)
	{
		Vector3 start = bossAtkBody->GetPosition();
		Vector3 end   = playerBody->GetPosition();

		nsK2EngineLow::RaycastHit hit;
		bool isHit = PhysicsWorld::Get().Raycast(
			start, end, hit, 0xFFFFFFFF,
			[playerBody](const btCollisionObject& obj) {
				return &obj == playerBody->GetBulletObject();
			}
		);

		Vector3 effectPos = isHit ? hit.point : end;
		// 攻撃範囲が広い場合にY座標が高くなりすぎるのを防ぐ
		const float yCap = ParameterManager::Get().GetCollisionHitParam()->effectYCap;
		if (effectPos.y > yCap) {
			effectPos.y = yCap;
		}
		return effectPos;
	}
}


CollisionHitManager* CollisionHitManager::instance_ = nullptr;


CollisionHitManager::CollisionHitManager()
{
}


CollisionHitManager::~CollisionHitManager()
{
}


void CollisionHitManager::Update()
{
	// ---- Phase 1: 全ペアの今フレームフラグをリセット ----
	for (auto it = activePairs_.begin(); it != activePairs_.end(); ++it) {
		it->second.flaggedThisFrame = false;
	}

	// ---- Phase 2: 今フレームのヒットペアを処理 ----
	//   GhostBodyManager::Update() → RegisterHitPair() で蓄積済み
	for (size_t i = 0; i < hitPairList_.size(); ++i) {
		Pair& hitPair = hitPairList_[i];
		PairKey key(hitPair.a, hitPair.b);
		auto it = activePairs_.find(key);

		if (it == activePairs_.end()) {
			// ---- 新規ペア → Enter ----
			PairInfo info;
			info.state = CollisionPairState::Enter;
			info.frameCount = 1;
			info.flaggedThisFrame = true;
			activePairs_.insert(std::make_pair(key, info));

			OnCollisionEnter(hitPair.a, hitPair.b);
		}
		else {
			// ---- 既存ペア → Stay ----
			it->second.state = CollisionPairState::Stay;
			it->second.frameCount++;
			it->second.flaggedThisFrame = true;

			OnCollisionStay(hitPair.a, hitPair.b, it->second.frameCount);
		}
	}

	// ---- Phase 3: 今フレームで検出されなかったペア → Exit ----
	for (auto it = activePairs_.begin(); it != activePairs_.end(); ) {
		if (!it->second.flaggedThisFrame) {
			OnCollisionExit(it->first.a, it->first.b);
			it = activePairs_.erase(it);
		}
		else {
			++it;
		}
	}

	hitPairList_.clear();
}


void CollisionHitManager::RegisterHitPair(GhostBody* a, GhostBody* b)
{
	hitPairList_.push_back(Pair(a, b));
}


void CollisionHitManager::OnBodyRemoved(GhostBody* body)
{
	// activePairs_ から該当ボディを含むペアを全て除去
	// ※ Exit通知は行わない（ボディが既に無効なため）
	for (auto it = activePairs_.begin(); it != activePairs_.end(); ) {
		if (it->first.a == body || it->first.b == body) {
			it = activePairs_.erase(it);
		}
		else {
			++it;
		}
	}

	// hitPairList_ からも除去（まだUpdate前に溜まっている可能性がある）
	auto newEnd = std::remove_if(hitPairList_.begin(), hitPairList_.end(),
		[body](const Pair& p) {
			return p.a == body || p.b == body;
		});
	hitPairList_.erase(newEnd, hitPairList_.end());
}

void CollisionHitManager::UpdateTakeHitSound()
{
	SoundManager::Get().PlaySE(enSoundKind_Player_TakeHit);
}

void CollisionHitManager::UpdateAttackHitSound()
{
	SoundManager::Get().PlaySE(enSoundKind_Player_AttackHit);
}


// =====================================================================
// Enter / Stay / Exit コールバック
// =====================================================================

void CollisionHitManager::OnCollisionEnter(GhostBody* a, GhostBody* b)
{
	// --- 一度だけ実行したい処理をここに書く ---
	Pair pair(a, b);

	// プレイヤーの攻撃
	{
		// 通常攻撃
		if (ContainsPlayerNormalAttackPair(pair)) {
			UpdatePlayerNormalAttackPair(pair);
		}
		// スキル攻撃
		if (ContainsPlayerSkillAttackPair(pair)) {
			UpdatePlayerSkillAttackPair(pair);
		}
	}

	// ボスの攻撃
	{
		// 通常攻撃
		if (ContainsBossAttackPair(pair)) {
			UpdateBossAttackPair(pair);
		}
		// ヒットスタンプ
		if (ContainsBossHitStampPair(pair)) {
			UpdateBossHitStampPair(pair);
		}
		// 回転攻撃
		if (ContainsBossSpinPair(pair)) {
			UpdateBossSpinPair(pair);
		}
		// 岩を投げる
		if (ContainsBossThrowRockPair(pair)) {
			UpdateBossThrowRockPair(pair);
		}
		// レーザー
		if (ContainsBossLaserWeakPair(pair)) {
			UpdateBossLaserWeakPair(pair);
		}
		if (ContainsBossLaserStrongPair(pair)) {
			UpdateBossLaserStrongPair(pair);
		}
	}

	// キャラクター全体
	{
		if (ContainsCharacterLandminePlayerPair(pair)) {
			UpdateCharacterLandminePlayerPair(pair);
		}
		if (ContainsCharacterLandmineBossPair(pair)) {
			UpdateCharacterLandmineBossPair(pair);
		}
	}
}


void CollisionHitManager::OnCollisionStay(GhostBody* a, GhostBody* b, int frameCount)
{
	// --- 継続的に実行したい処理をここに書く ---

	// TODO: 継続ダメージなどの例
	// 例: 毒沼の継続ダメージ（Nフレームごとに適用）
	//
	// const int DAMAGE_INTERVAL = 60; // 60フレームごと
	// if (frameCount % DAMAGE_INTERVAL == 0) {
	//     ApplyContinuousDamage(a, b);
	// }
}


void CollisionHitManager::OnCollisionExit(GhostBody* a, GhostBody* b)
{
	// --- 離脱時に一度だけ実行したい処理をここに書く ---

	// TODO: バフ解除、エリア退出通知 など
}


//// =====================================================================
//// 既存のペア判定ロジック（変更なし）
//// =====================================================================


/* ============================================ */
/* プレイヤーが攻撃 */
/* ============================================ */

bool CollisionHitManager::ContainsPlayerNormalAttackPair(const Pair& hitPair)
{
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerNormalAtkID())) {
		return false;
	}
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossID())) {
		return false;
	}
	return true;
}
void CollisionHitManager::UpdatePlayerNormalAttackPair(Pair& hitPair)
{
	// 通常攻撃
	auto* playerCharacter = GetHitObject<Player>(hitPair, CharacterID::PlayerNormalAtkID());
	auto* bossCharacter = GetHitObject<BossCharacter>(hitPair, CharacterID::BossID());
	if (!bossCharacter || !playerCharacter || !playerCharacter->GetStatus() || !bossCharacter->GetStatus()) return;


	auto playerStatus = playerCharacter->GetStatus()->As<PlayerStatus>();

	if (bossCharacter->GetStatus()) {
		// ダメージ計算
		float motion = playerStatus->GetSkillMotionValues("NormalAttack");
		int damage = Calculate(playerStatus, motion);
		bossCharacter->GetStatus()->Damage(damage);
		UpdateAttackHitSound(); // 攻撃が当たったSEを流す
	}

	// エフェクト: 攻撃判定からボスのコリジョン表面座標を求めて再生
	GhostBody* attackBody = GetGhostBodyById(hitPair, CharacterID::PlayerNormalAtkID());
	GhostBody* bossBody   = GetGhostBodyById(hitPair, CharacterID::BossID());
	Vector3 effectPos = CalcBossSurfacePos(attackBody, bossBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->playerNormalAttackEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitBoss, effectPos, Quaternion::Identity, Vector3(scale));
}



bool CollisionHitManager::ContainsPlayerSkillAttackPair(const Pair& hitPair)
{
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerSkillAtkID())) {
		return false;
	}
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossID())) {
		return false;
	}
	return true;
}
void CollisionHitManager::UpdatePlayerSkillAttackPair(Pair& hitPair)
{
	// スキル攻撃
	auto* playerCharacter = GetHitObject<Player>(hitPair, CharacterID::PlayerSkillAtkID());
	auto* bossCharacter = GetHitObject<BossCharacter>(hitPair, CharacterID::BossID());
	if (!bossCharacter || !playerCharacter || !playerCharacter->GetStatus() || !bossCharacter->GetStatus()) return;


	auto* playerStatus = playerCharacter->GetStatus()->As<PlayerStatus>();

	if (bossCharacter->GetStatus()) {
		// "SpecialAttack" スロットの威力を取得してダメージ計算
		float motion = playerStatus->GetSkillMotionValues("SpecialAttack");
		int damage = Calculate(playerStatus, motion);
		bossCharacter->GetStatus()->Damage(damage);
		UpdateAttackHitSound(); // 攻撃が当たったSEを流す
	}

	// エフェクト: 攻撃判定からボスのコリジョン表面座標を求めて再生
	GhostBody* attackBody = GetGhostBodyById(hitPair, CharacterID::PlayerSkillAtkID());
	GhostBody* bossBody   = GetGhostBodyById(hitPair, CharacterID::BossID());
	Vector3 effectPos = CalcBossSurfacePos(attackBody, bossBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->playerSkillAttackEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitBoss, effectPos, Quaternion::Identity, Vector3(scale));
}



/* ============================================ */
/* ボスが攻撃 */
/* ============================================ */

bool CollisionHitManager::ContainsBossAttackPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossNormalAtkID())) {
		return false;
	}
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}
void CollisionHitManager::UpdateBossAttackPair(Pair& hitPair)
{
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossNormalAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();
	

	// プレイヤーがいるか、プレイヤーのステータスがあるか
	if (player == nullptr || player->GetStatus() == nullptr) { return; }
	// プレイヤーが無敵フラグを持っているか
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	if (status && status->IsInvincible()) { return; }

	// ボスのステータスがあるならダメージ処理
	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("NormalAttack");
		float vibrationTime = bossStatus->GetVibrationTimeValues("NormalAttack");
		float vibrationForce = bossStatus->GetVibrationForceValues("NormalAttack");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage);
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossNormalAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->bossNormalAttackEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(scale));
}



bool CollisionHitManager::ContainsBossHitStampPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossHitStampAtkID())) {
		return false;
	}
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}
void CollisionHitManager::UpdateBossHitStampPair(Pair& hitPair)
{
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossHitStampAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();

	// プレイヤーがいるか、もしくはプレイヤーのステータスがあるか
	if (player == nullptr || player->GetStatus() == nullptr) { return; }
	// プレイヤーが無敵フラグを持っているか
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	if (status && status->IsInvincible()){ return; }

	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("HitStamp");
		float vibrationTime = bossStatus->GetVibrationTimeValues("HitStamp");
		float vibrationForce = bossStatus->GetVibrationForceValues("HitStamp");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage); // ダメージを与える
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossHitStampAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->bossHitStampEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(scale));
}



bool CollisionHitManager::ContainsBossSpinPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossSpinAtkID())) {
		return false;
	}
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}
void CollisionHitManager::UpdateBossSpinPair(Pair& hitPair)
{
	// Pairの二人のクラスを取得
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossSpinAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();

	// プレイヤーがいるかつプレイヤーのステータスがあるかつボスがいいなら続ける
	if (player == nullptr || player->GetStatus() == nullptr || boss == nullptr) { return; }
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	// プレイヤーが無敵フラグを持っているか
	if (status && status->IsInvincible()){ return; }

	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("SpinAttack");
		float vibrationTime = bossStatus->GetVibrationTimeValues("SpinAttack");
		float vibrationForce = bossStatus->GetVibrationForceValues("SpinAttack");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage);
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}
	

	// 当たった二人の座標を取得
	Vector3 playerPos = player->GetTransformPosition();
	Vector3 bossPos = boss->GetTransformPosition();

	// 方向を取得
	Vector3 dir= playerPos - bossPos;
	dir.Normalize();

	const auto* hitParam = ParameterManager::Get().GetCollisionHitParam();

	// ノックバック
	Vector3 knockBackVelocity = dir * hitParam->spinKnockBack;

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossSpinAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(hitParam->bossSpinEffectScale));
}

bool CollisionHitManager::ContainsBossThrowRockPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossThrowRockAtkID())) {
		return false;
	}
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}

void CollisionHitManager::UpdateBossThrowRockPair(Pair& hitPair)
{
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossThrowRockAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();

	// プレイヤーがいるか、もしくはプレイヤーのステータスがあるか
	if (player == nullptr || player->GetStatus() == nullptr) { return; }
	// プレイヤーが無敵フラグを持っているか
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	if (status && status->IsInvincible()) { return; }

	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("ThrowRock");
		float vibrationTime = bossStatus->GetVibrationTimeValues("ThrowRock");
		float vibrationForce = bossStatus->GetVibrationForceValues("ThrowRock");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage); // ダメージを与える
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossThrowRockAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->bossThrowRockEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(scale));
}

bool CollisionHitManager::ContainsBossLaserWeakPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossLaserWeakAtkID())) {
		return false;
	}
	if (!IsHitObject<Character>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}

void CollisionHitManager::UpdateBossLaserWeakPair(Pair& hitPair)
{
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossLaserWeakAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();

	// プレイヤーがいるか、もしくはプレイヤーのステータスがあるか
	if (player == nullptr || player->GetStatus() == nullptr) { return; }
	// プレイヤーが無敵フラグを持っているか
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	if (status && status->IsInvincible()) { return; }

	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("LaserWeak");
		float vibrationTime = bossStatus->GetVibrationTimeValues("LaserWeak");
		float vibrationForce = bossStatus->GetVibrationForceValues("LaserWeak");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage); // ダメージを与える
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossLaserWeakAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->bossLaserWeakEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(scale));
}

bool CollisionHitManager::ContainsBossLaserStrongPair(const Pair& hitPair)
{
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossLaserStrongAtkID())) {
		return false;
	}
	if (!IsHitObject<Character>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}

void CollisionHitManager::UpdateBossLaserStrongPair(Pair& hitPair)
{
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossLaserStrongAtkID());
	auto bossStatus = boss->GetStatus()->As<BossStatus>();

	// プレイヤーがいるか、もしくはプレイヤーのステータスがあるか
	if (player == nullptr || player->GetStatus() == nullptr) { return; }
	// プレイヤーが無敵フラグを持っているか
	PlayerStatus* status = player->GetStatus()->As<PlayerStatus>();
	if (status && status->IsInvincible()) { return; }

	if (boss->GetStatus()) {
		float motion = bossStatus->GetSkillMotionValues("LaserStrong");
		float vibrationTime = bossStatus->GetVibrationTimeValues("LaserStrong");
		float vibrationForce = bossStatus->GetVibrationForceValues("LaserStrong");
		int damage = Calculate(bossStatus, motion);
		player->GetStatus()->Damage(damage); // ダメージを与える
		UpdateTakeHitSound(); // 攻撃が当たったSEを流す
		g_pad[0]->SetVibration(vibrationTime, vibrationForce);
	}

	// エフェクト
	GhostBody* bossAtkBody = GetGhostBodyById(hitPair, CharacterID::BossLaserStrongAtkID());
	GhostBody* playerBody  = GetGhostBodyById(hitPair, CharacterID::PlayerID());
	Vector3 effectPos = CalcPlayerHitPos(bossAtkBody, playerBody);
	float scale = ParameterManager::Get().GetCollisionHitParam()->bossLaserStrongEffectScale;
	EffectManager::Get().PlayEffect(enEffectKind_AttackHitPlayer, effectPos, Quaternion::Identity, Vector3(scale));
}



/* ============================================ */
/* キャラクターの誰かが全員に対して攻撃 */
/* ============================================ */

bool CollisionHitManager::ContainsCharacterLandminePlayerPair(const Pair& hitPair)
{
	if (!IsHitObject<LandmineObject>(hitPair, CharacterID::CharaLandmineAtkID())) {
		return false;
	}
	if (!IsHitObject<Player>(hitPair, CharacterID::PlayerID())) {
		return false;
	}
	return true;
}

void CollisionHitManager::UpdateCharacterLandminePlayerPair(Pair& hitPair)
{
	// ownerはCharacterとして取得 → 攻撃者はどちらかわからないため
	auto* owner = GetHitObject<Character>(hitPair, CharacterID::CharaLandmineAtkID());
	auto* player = GetHitObject<Player>(hitPair, CharacterID::PlayerID());

	if (!owner || !player || !owner->GetStatus() || !player->GetStatus()) { return; }

	// プレイヤーの無敵チェック
	PlayerStatus* playerStatus = player->GetStatus()->As<PlayerStatus>();
	if (playerStatus && playerStatus->IsInvincible()) { return; }

	auto* ownerStatus = owner->GetStatus()->As<PlayerStatus>();
	float motion = ownerStatus->GetSkillMotionValues("SpecialAttack");
	float vibrationTime = ownerStatus->GetVibrationTimeValues("SpecialAttack");
	float vibrationForce = ownerStatus->GetVibrationForceValues("SpecialAttack");
	int damage = Calculate(ownerStatus, motion);
	player->GetStatus()->Damage(damage);
	UpdateTakeHitSound();
	g_pad[0]->SetVibration(vibrationTime, vibrationForce);
}

bool CollisionHitManager::ContainsCharacterLandmineBossPair(const Pair& hitPair)
{
	if (!IsHitObject<LandmineObject>(hitPair, CharacterID::CharaLandmineAtkID())) {
		return false;
	}
	if (!IsHitObject<BossCharacter>(hitPair, CharacterID::BossID())) {
		return false;
	}
	return true;
}

void CollisionHitManager::UpdateCharacterLandmineBossPair(Pair& hitPair)
{
	// ownerはCharacterとして取得 → 攻撃者はどちらかわからないため
	auto* owner = GetHitObject<Character>(hitPair, CharacterID::CharaLandmineAtkID());
	auto* boss = GetHitObject<BossCharacter>(hitPair, CharacterID::BossID());

	auto* ownerStatus = owner->GetStatus()->As<PlayerStatus>();
	float motion = ownerStatus->GetSkillMotionValues("SpecialAttack");
	int damage = Calculate(ownerStatus, motion);
	boss->GetStatus()->Damage(damage);
	UpdateAttackHitSound();
}


