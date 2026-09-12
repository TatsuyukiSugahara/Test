#pragma once
#include "HID/ITouchBackend.h"

namespace aq
{
	namespace hid
	{
		// タッチの無い環境(Win32 / UWP / Mac)用。常に「触れていない」を返す。
		// これがあるおかげで、上位はタッチの有無で分岐せずに済む。
		class NullTouchBackend : public ITouchBackend
		{
		public:
			void Poll(TouchState& out) override { out = {}; }
		};
	}
}
