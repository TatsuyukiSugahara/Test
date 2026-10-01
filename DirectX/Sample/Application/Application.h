#pragma once
#include "Core/AppHost.h"

namespace sample
{
	/**
	 * 最小のゲームアプリケーション。
	 *
	 * aq::IAppModule を実装し、必要なフックだけを override する(AppHost が所有して動かす)。
	 * ここでは OnInitialize だけを使い、箱を 1 個置いている。
	 */
	class Application : public aq::IAppModule
	{
	public:
		const char* GetName() const override { return "Sample"; }

	protected:
		bool OnInitialize(aq::AppHost& host) override;
	};
}
