#pragma once
#include "D3D12Common.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 グラフィクス用ルートシグネチャ (Phase 1b) ──
		// レイアウト:
		//   [0..MAX_ROOT_CBV-1] ルート CBV b0..bN      (ALL 可視)  ← VS/PSSetConstantBuffer
		//   [MAX_ROOT_CBV]      SRV ディスクリプタテーブル t0..t7 (PIXEL 可視) ← PSSetShaderResource
		//   静的サンプラー s0 = Linear/Clamp, s1 = Linear/Wrap
		//
		// CBV をルートディスクリプタにすることでディスクリプタヒープ管理を CBV 分省略する。
		class D3D12RootSignature
		{
		public:
			static constexpr uint32_t MAX_ROOT_CBV = 5;   // b0..b4
			static constexpr uint32_t SRV_TABLE_SIZE = 8; // t0..t7

			// ルートパラメータインデックス
			static constexpr uint32_t PARAM_SRV_TABLE = MAX_ROOT_CBV;  // SRV テーブルの位置

			D3D12RootSignature()  = default;
			~D3D12RootSignature() { Release(); }

			bool Create(ID3D12Device* device);
			void Release();

			ID3D12RootSignature* Get() const { return rootSignature_; }

		private:
			ID3D12RootSignature* rootSignature_ = nullptr;
		};
	}
}
