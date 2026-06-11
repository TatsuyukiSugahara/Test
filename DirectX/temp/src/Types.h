/**
 * Types.h
 * コリジョン関係の定義群
 */
#pragma once
#include "physics/CollisionAttr.h"
#include <cstdint>


struct CollisionAttribute
{
	enum Enum : uint32_t
	{
		Ground			= 1 << 0,
		Character		= 1 << 1,
		Boss			= 1 << 2,
	};
};


struct CollisionAttributeMask
{
	enum Enum : uint32_t
	{
		Ground			= CollisionAttribute::Character,
		Character		= CollisionAttribute::Ground,
		Enemy			= 1 << 2,
		All				= 0xFFFFFFFF,
	};
};


namespace ghost
{
	// 自分はだれか
	struct CollisionAttribute
	{
		enum Enum : uint32_t
		{
			None = 0,
			PlayerDef = 1 << 0,
			BossDef = 1 << 1,
			PlayerAtk = 1 << 2,
			BossAtk = 1 << 3,
			CharacterAtk = 1 << 4,
			PlayerAvoid = 1 << 5,
			BossAvoid = 1 << 6,
		};
	};

	// 相手は誰なのか
	struct CollisionAttributeMask
	{
		enum Enum : uint32_t
		{
			// 攻撃を受ける相手
			Character = CollisionAttribute::BossAtk | CollisionAttribute::PlayerAtk,// ボスもプレイヤーもダメージを受ける
			Player = CollisionAttribute::BossAtk | CollisionAttribute::CharacterAtk, // ボスの攻撃だけ受ける
			Boss = CollisionAttribute::PlayerAtk | CollisionAttribute::CharacterAtk, // プレイヤーの攻撃だけ受ける

			// 攻撃が当たる相手
			CharacterAtk = CollisionAttribute::PlayerDef | CollisionAttribute::BossDef, // プレイヤーもボスもダメージを与える
			PlayerAtk = CollisionAttribute::BossDef, // ボスのダメージ体にだけ当たる
			BossAtk = CollisionAttribute::PlayerDef, // プレイヤーのダメージ体にだけ当たる
			All				= 0xFFFFFFFF,
		};
	};
}