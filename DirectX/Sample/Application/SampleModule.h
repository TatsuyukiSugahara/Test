#pragma once
#include "Core/AppHost.h"

namespace aq { class AppModuleRegistry; }

namespace sample
{
	/**
	 * 最小のゲームアプリケーション。
	 *
	 * aq::IAppModule を実装し、必要なフックだけを override する(AppHost が所有して動かす)。
	 * ここでは OnInitialize だけを使い、箱を 1 個置いている。
	 */
	class SampleModule : public aq::IAppModule
	{
	public:
		const char* GetName() const override { return "Sample"; }

	protected:
		bool OnInitialize(aq::AppHost& host) override;
	};


	/**
	 * このプロジェクトのモジュールをレジストリへ登録する。エントリが Engine::Initialize より前に呼ぶ
	 */
	void RegisterAppModules(aq::AppModuleRegistry& registry);
}
