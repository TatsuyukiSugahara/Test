#pragma once
#include "D3D12Common.h"
#include "Graphics/IGpuBuffer.h"
#include "D3D12Resources.h"  // D3D12SRVHandleRef / D3D12UAVHandleRef


namespace aq
{
	namespace graphics
	{
		/**
		 * GPU 駆動用バッファ (D3D12)。用途に応じて SRV/UAV/IB/間接引数として使える。
		 *
		 *  - 入力 (構造化/RAW SRV): UPLOAD ヒープ常駐。CPU から 1 回書く。状態遷移なし。
		 *  - 出力 (RAW UAV + IB / 間接引数): DEFAULT ヒープ。compute が書き、描画/間接描画で読む。
		 *    状態は state_ で追跡し Transition() で UAV↔INDEX_BUFFER↔INDIRECT_ARGUMENT を切り替える。
		 */
		class D3D12GpuBuffer final : public IGpuBuffer
		{
		public:
			D3D12GpuBuffer()           = default;
			~D3D12GpuBuffer() override { Release(); }

			/**
			 * @param structuredStride  >0 で StructuredBuffer、0 で RAW(ByteAddress)。
			 * @param srv/uav           作成するビュー。uav=true は DEFAULT ヒープ、false は UPLOAD ヒープ。
			 * @param initData          srv 用の初期データ (uav バッファでは無視)。
			 * @param srvStaging/uavStaging  device が確保した staging ディスクリプタスロット。
			 */
			bool Create(ID3D12Device* device, uint32_t byteSize, uint32_t structuredStride,
			            bool srv, bool uav, const void* initData,
			            D3D12_CPU_DESCRIPTOR_HANDLE srvStaging,
			            D3D12_CPU_DESCRIPTOR_HANDLE uavStaging);
			void Release() override;

			IShaderResourceView*  AsSRV() override { return srvValid_ ? &srv_ : nullptr; }
			IUnorderedAccessView* AsUAV() override { return uavValid_ ? &uav_ : nullptr; }

			ID3D12Resource*         GetResource() const { return resource_; }
			D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;

			/** state_ から after へ遷移バリアを積む (UPLOAD ヒープは no-op)。 */
			void Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES after);

		private:
			ID3D12Resource*       resource_ = nullptr;
			uint8_t*              mapped_   = nullptr;  // UPLOAD ヒープのみ
			uint32_t              byteSize_ = 0;
			bool                  isDefault_ = false;   // DEFAULT ヒープ (遷移可能)
			D3D12_RESOURCE_STATES state_    = D3D12_RESOURCE_STATE_COMMON;
			bool                  srvValid_ = false;
			bool                  uavValid_ = false;
			D3D12SRVHandleRef     srv_;
			D3D12UAVHandleRef     uav_;
		};
	}
}
