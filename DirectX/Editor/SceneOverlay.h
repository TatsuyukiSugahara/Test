#pragma once
#include <imgui/imgui.h>


namespace aq
{
	class Camera;
}


namespace editor
{
	/**
	 * シーンビューの補助線。ゲームビューのウィンドウの描画リストへ、エディタ用 ImGui で描く(レンダラーには線の描画を足さない)。
	 * 線は Camera::WorldToClip の結果をクリップ空間で近い面(z = 0)に切ってから w で割る(カメラの後ろへ回り込んだ線が画面を横切らないように)。
	 * 選んだノードの箱は E11b で足す
	 */
	class SceneOverlay
	{
	public:
		/**
		 * y = 0 の XZ 面のグリッドと、原点の X 軸(赤)・Z 軸(青)を描く。
		 * 間隔はカメラの高さで 1 / 10 / 100 m に切り替え、範囲はカメラの真下から ±max(|高さ| × 10, 50)。
		 * @param drawList  画像の矩形で切り抜く(中で PushClipRect する)描画リスト
		 * @param camera    今のフレームの行列が作られているカメラ(メインカメラ)
		 * @param imageMin  画像の左上(スクリーン座標)
		 * @param imageMax  画像の右下(スクリーン座標)
		 */
		static void DrawGrid(ImDrawList* drawList, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax);

		/**
		 * ワールドの線分を描く。近い面(z = 0)で切ってから w で割る。完全に近い面より手前なら何も描かない。
		 * @return 描いたら true
		 */
		static bool DrawWorldLine(ImDrawList* drawList, const aq::Camera& camera, const ImVec2& imageMin, const ImVec2& imageMax,
		                          const float (&from)[3], const float (&to)[3], const ImU32 color, const float thickness);
	};
}
