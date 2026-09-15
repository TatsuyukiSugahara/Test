#pragma once
#include "Core/Application.h"

namespace sample
{
	/**
	 * 最小のゲームアプリケーション。
	 *
	 * aq::Application を継承し、必要なフックだけを override する。
	 * ここでは OnInitialize だけを使い、箱を 1 個置いている。
	 */
	class Application : public aq::Application
	{
	protected:
		bool OnInitialize() override;
	};
}
