#pragma once
#include "IUIComponent.h"
#include "Math/Vector.h"

namespace aq
{
	namespace ui
	{
		// キャンバスは UIObject ツリーのルートにアタッチするコンポーネント。
		// 仮想解像度とクライアント解像度の両方を保持し、HitTest 時の座標変換に使用する。
		class UICanvasComponent : public IUIComponent
		{
		public:
			math::Vector2 resolution   = { 1920.f, 1080.f }; // 仮想解像度
			math::Vector2 clientSize   = { 1920.f, 1080.f }; // 実ウィンドウサイズ (毎フレーム更新)
			int           sortOrder    = 0;                   // 複数 Canvas 間の描画順
		};

	} // namespace ui
} // namespace aq
