#pragma once
#include "D3D12Common.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 グラフィクス用ルートシグネチャ (Phase 1b / Phase 2 でテーブル拡張) ──
		// レイアウト:
		//   [0..MAX_ROOT_CBV-1] ルート CBV b0..bN       (ALL 可視)  ← VS/PSSetConstantBuffer
		//   [MAX_ROOT_CBV]      SRV ディスクリプタテーブル t0..t11 (PIXEL 可視) ← PSSetShaderResource
		//   静的サンプラー s0 = Linear/Wrap, s1 = 比較(LESS_EQUAL シャドウ用)
		//
		// CBV をルートディスクリプタにすることでディスクリプタヒープ管理を CBV 分省略する。
		// CBV はエンジンが使う最大レジスタ b5 まで covering する:
		//   b0 PerDraw, b1 Lighting, b2 Material/ShadowLight, b3 Shadow, b4 Bones, b5 Ocean。
		// SRV テーブルはエンジンが使う最大レジスタ t11 まで covering する:
		//   t0..t3 マテリアル(albedo/normal/specular/emissive), t4 シャドウマップ配列,
		//   t8..t11 GBuffer。間の t5..t7 は未使用だが連続テーブルとして確保する。
		class D3D12RootSignature
		{
		public:
			static constexpr uint32_t MAX_ROOT_CBV = 6;    // b0..b5
			static constexpr uint32_t SRV_TABLE_SIZE = 12; // t0..t11

			// ルートパラメータインデックス
			static constexpr uint32_t PARAM_SRV_TABLE = MAX_ROOT_CBV;  // SRV テーブルの位置

			// ── コンピュート用 (Phase 4: ブルーム等) ──
			//   [0] CBV b0 (ルートディスクリプタ)
			//   [1] SRV テーブル t0..t1
			//   [2] UAV テーブル u0
			//   静的サンプラー s0 = Linear/Clamp
			static constexpr uint32_t CS_SRV_TABLE_SIZE = 2;  // t0..t1
			static constexpr uint32_t CS_UAV_TABLE_SIZE = 2;  // u0..u1 (u1 = クラスタカリング間接引数)
			static constexpr uint32_t CS_PARAM_CBV       = 0;
			static constexpr uint32_t CS_PARAM_SRV_TABLE = 1;
			static constexpr uint32_t CS_PARAM_UAV_TABLE = 2;

			D3D12RootSignature()  = default;
			~D3D12RootSignature() { Release(); }

			bool Create(ID3D12Device* device);
			bool CreateCompute(ID3D12Device* device);
			void Release();

			ID3D12RootSignature* Get() const        { return rootSignature_; }
			ID3D12RootSignature* GetCompute() const { return computeRootSignature_; }

		private:
			ID3D12RootSignature* rootSignature_        = nullptr;
			ID3D12RootSignature* computeRootSignature_ = nullptr;
		};
	}
}
