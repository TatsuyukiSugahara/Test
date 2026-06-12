#pragma once
#include <cstdint>

namespace engine
{
	namespace rendering
	{
		/**
		 * GraphicsDevice が管理するレンダーターゲット・レジストリへの不透明なインデックス。
		 *
		 * 生ポインタ (IRenderTarget*) の代わりにインデックスを保持することで、
		 * コマンド記録を RT オブジェクトのライフタイムから切り離す。
		 * DX12 バックエンド追加時は RTV デスクリプタ・ヒープのオフセットに直接マップできる。
		 *
		 * リサイズ・再生成の注意:
		 *   RT オブジェクトの再生成は RenderThread を drain（Submit が in-flight でない状態）
		 *   してから行うこと。drain 後はハンドルのインデックスが指す実オブジェクトが
		 *   新しい RT に差し替えられるため、以降の記録・実行は新 RT を参照する。
		 *
		 * 解決: Execute 時に GraphicsDevice::GetMainRenderTarget(handle.index) で実オブジェクトを取得する。
		 */
		struct RenderTargetHandle
		{
			static constexpr uint32_t INVALID = ~0u;
			uint32_t index = INVALID;

			bool IsValid() const { return index != INVALID; }
		};
	}
}
