#pragma once
#ifdef AQ_DEBUG_IMGUI
#include <vector>

namespace aq
{
	class IDebugRenderable;

	/**
	 * IDebugRenderable を集約して engine が一括呼び出しするレジストリ。
	 * ECS System とは独立したデバッグ UI の登録・管理に使う。
	 *
	 * 使い方:
	 *   1. DebugUI::Get().Register(panel)   で登録
	 *   2. DebugUI::Get().Unregister(panel) で解除（OnFinalize 等で明示的に呼ぶ）
	 */
	class DebugUI
	{
	private:
		std::vector<IDebugRenderable*> items_;

		static DebugUI* instance_;

		DebugUI()  = default;
		~DebugUI() = default;

	public:
		static void    Initialize();
		static DebugUI& Get();
		static bool    IsAvailable();
		static void    Finalize();

		void Register(IDebugRenderable* item);
		void Unregister(IDebugRenderable* item);

		void DebugRenderMenuAll();
		void DebugRenderAll();
	};
}
#endif
