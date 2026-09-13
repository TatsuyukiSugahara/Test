#pragma once
// ImGui 無効構成では中身を空にして、既存ビルドに一切影響させない。
// プラットフォームガードは付けない(マウス抽象しか見ないので、タッチでも
// 物理マウスでも同じ経路で動く)。
#if defined(AQ_IMGUI)


namespace aq
{
	namespace platform
	{
		/**
		 * ImGui のポインタ入力シム(imgui_impl_win32 のうち入力だけに当たる)。
		 *
		 * 描画は各グラフィックス API の ImGui バックエンドが持つので、ここが見るのは
		 * フレーム頭の DisplaySize / DeltaTime と、マウス抽象から流すポインタだけ。
		 * タッチは TouchMouseBackend が既にマウス抽象へ合成しているため、
		 * imgui_impl_android は同梱しない(設計書/Android移植設計.md P7)。
		 * キーボード / ホイール / カーソル形状は扱わない。
		 */
		namespace ImGuiPointerInput
		{
			/** バックエンドの登録(ImGui のコンテキスト生成後に呼ぶ) */
			bool Init();

			/** バックエンドの登録解除(ImGui のコンテキスト破棄前に呼ぶ) */
			void Shutdown();

			/** フレーム頭の状態更新(ImGui::NewFrame の前に呼ぶ) */
			void NewFrame();
		}
	}
}
#endif // AQ_IMGUI
