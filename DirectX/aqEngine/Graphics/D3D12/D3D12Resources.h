#pragma once
#include "D3D12Common.h"
#include "Graphics/IShaderResourceView.h"
#include "Graphics/IUnorderedAccessView.h"
#include "Graphics/ISamplerState.h"
#include "Graphics/GraphicsTypes.h"


namespace aq
{
	namespace graphics
	{
		// ── D3D12 共通 SRV インターフェース (Phase 2/3) ──
		// PSSetShaderResource が具象型を問わず staging ヒープ上の SRV ハンドルを取得するための基底。
		// テクスチャ(D3D12Texture2D)・オフスクリーンRT・深度マップの SRV がこれを実装する。
		class D3D12SRV : public IShaderResourceView
		{
		public:
			virtual D3D12_CPU_DESCRIPTOR_HANDLE GetStagingCPUHandle() const = 0;
			// SRV として読む直前に PIXEL_SHADER_RESOURCE へ遷移が必要なら barrier を積む。
			// 常時 PIXEL_SHADER_RESOURCE のテクスチャは no-op。RT/Depth はステート追跡して遷移する。
			virtual void TransitionToSRV(ID3D12GraphicsCommandList* /*list*/) {}
			// コンピュートで読む場合は NON_PIXEL_SHADER_RESOURCE へ遷移する。
			virtual void TransitionToComputeSRV(ID3D12GraphicsCommandList* /*list*/) {}
		};


		// staging ヒープ上の既存 SRV ハンドルを参照するだけの非所有ホルダ (RT/Depth が内包)。
		// オプションで「ソース資源 + その現在ステート」を保持し、SRV 読み取り前に遷移できる。
		class D3D12SRVHandleRef final : public D3D12SRV
		{
		public:
			void SetStagingCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE h) { handle_ = h; }
			// 遷移対象の資源とそのステート変数 (RT/Depth のメンバ) を結び付ける。
			void SetBarrierSource(ID3D12Resource* resource, D3D12_RESOURCE_STATES* statePtr)
			{
				resource_ = resource;
				statePtr_ = statePtr;
			}
			D3D12_CPU_DESCRIPTOR_HANDLE GetStagingCPUHandle() const override { return handle_; }
			void TransitionToSRV(ID3D12GraphicsCommandList* list) override;
			void TransitionToComputeSRV(ID3D12GraphicsCommandList* list) override;
			// DeferredSRV など IShaderResourceView ラッパが転送できるよう D3D12SRV* を返す。
			void* GetNativeHandle() const override
			{
				return static_cast<D3D12SRV*>(const_cast<D3D12SRVHandleRef*>(this));
			}
			void Release() override {}  // 非所有

		private:
			D3D12_CPU_DESCRIPTOR_HANDLE handle_   = {};
			ID3D12Resource*             resource_ = nullptr;
			D3D12_RESOURCE_STATES*      statePtr_ = nullptr;
		};


		// UAV ホルダ (オフスクリーン RT が内包)。コンピュートで書く前に UNORDERED_ACCESS へ遷移する。
		class D3D12UAVHandleRef final : public IUnorderedAccessView
		{
		public:
			void SetStagingCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE h) { handle_ = h; }
			void SetBarrierSource(ID3D12Resource* resource, D3D12_RESOURCE_STATES* statePtr)
			{
				resource_ = resource;
				statePtr_ = statePtr;
			}
			D3D12_CPU_DESCRIPTOR_HANDLE GetStagingCPUHandle() const { return handle_; }
			void TransitionToUAV(ID3D12GraphicsCommandList* list);
			void Release() override {}  // 非所有

		private:
			D3D12_CPU_DESCRIPTOR_HANDLE handle_   = {};
			ID3D12Resource*             resource_ = nullptr;
			D3D12_RESOURCE_STATES*      statePtr_ = nullptr;
		};


		// D3D12 テクスチャ2D + SRV (Phase 2)
		// リソース本体(DEFAULTヒープ)を所有し、CPU ステージングヒープ上の SRV ハンドルを保持する。
		class D3D12Texture2D final : public D3D12SRV
		{
		public:
			D3D12Texture2D()           = default;
			~D3D12Texture2D() override { Release(); }

			void Bind(ID3D12Resource* texture, D3D12_CPU_DESCRIPTOR_HANDLE stagingCPU);
			void Release() override;
			// バインド経路 (PSSetShaderResource) が D3D12SRV* として解決できるよう this を返す。
			// 未ロード(リソース無し)のときは nullptr を返し、呼び出し側がスキップできるようにする。
			void* GetNativeHandle() const override
			{
				return texture_ ? static_cast<D3D12SRV*>(const_cast<D3D12Texture2D*>(this)) : nullptr;
			}

			D3D12_CPU_DESCRIPTOR_HANDLE GetStagingCPUHandle() const override { return stagingCPUHandle_; }

		private:
			ID3D12Resource*             texture_          = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE stagingCPUHandle_ = {};
		};


		// D3D12 サンプラーステート (Phase 2)
		// 静的サンプラー (s0=Clamp, s1=比較LESS_EQUAL) は D3D12RootSignature に組み込み済みのため、
		// このクラスは SamplerDesc を保持するだけの軽量 wrapper。
		class D3D12SamplerState final : public ISamplerState
		{
		public:
			D3D12SamplerState()           = default;
			~D3D12SamplerState() override = default;

			bool Create(const SamplerDesc& desc) override { desc_ = desc; return true; }
			void Release() override {}

		private:
			SamplerDesc desc_ = {};
		};
	}
}
