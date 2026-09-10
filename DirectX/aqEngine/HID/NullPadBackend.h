#pragma once
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		// 入力の無いパッドバックエンド(常に未接続)。
		// GameController.framework 実装が入るまでの Mac(P2〜P3)で使う。
		// TODO(P4): HID/Mac/GameControllerPadBackend へ差し替える。
		class NullPadBackend : public IPadBackend
		{
		public:
			void Poll(uint32_t /*index*/, PadState& out) override { out = {}; }
			void SetVibration(uint32_t /*index*/, float /*left*/, float /*right*/) override {}
			void SetTriggerResistance(uint32_t /*index*/, PadAxis /*trigger*/,
			                          float /*startPos*/, float /*strength*/) override {}
		};
	}
}
