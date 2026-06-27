#pragma once

namespace aq
{
	namespace graphics
	{
		class IShaderResourceView;
		class IUnorderedAccessView;

		/**
		 * GPU 駆動処理用の汎用バッファ (構造化 SRV / RAW UAV / インデックス / 間接引数)。
		 * GPU クラスタカリング (compute → ExecuteIndirect) で使う。
		 *
		 * - 入力 (クラスタ記述子 / 並べ替えインデックス): UPLOAD ヒープ + SRV。
		 * - 出力 (compact インデックス / 間接引数): DEFAULT ヒープ + UAV (+ IB / 間接引数)。
		 *
		 * インデックスバッファ / 間接引数としての利用は API 依存のため、
		 * RenderContext::IASetIndexBufferGpu / DrawIndexedIndirect が内部で具象型へキャストして扱う。
		 */
		class IGpuBuffer
		{
		public:
			virtual ~IGpuBuffer() = default;
			virtual void Release() = 0;

			/** 構造化/RAW SRV ビュー (compute 入力)。未対応なら nullptr。 */
			virtual IShaderResourceView*  AsSRV() { return nullptr; }
			/** RAW UAV ビュー (compute 出力)。未対応なら nullptr。 */
			virtual IUnorderedAccessView* AsUAV() { return nullptr; }
		};
	}
}
