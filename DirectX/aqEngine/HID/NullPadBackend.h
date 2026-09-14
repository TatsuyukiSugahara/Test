#pragma once
#include "HID/IPadBackend.h"

namespace aq
{
	namespace hid
	{
		// 入力の無いパッドバックエンド(常に未接続)。
		// 各プラットフォームの実装が揃うまでの足場として使う
		// (Mac / iOS はいずれも HID/Apple/GameControllerPadBackend へ移行済み)。
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
