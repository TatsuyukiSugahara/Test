#include "aq.h"
#include "D3D12Common.h"
#include "D3D12Resources.h"


namespace aq
{
	namespace graphics
	{
		namespace
		{
			void TransitionResource(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
			                        D3D12_RESOURCE_STATES* statePtr, D3D12_RESOURCE_STATES after)
			{
				if (!list || !res || !statePtr || *statePtr == after) return;
				D3D12_RESOURCE_BARRIER b = {};
				b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource   = res;
				b.Transition.StateBefore = *statePtr;
				b.Transition.StateAfter  = after;
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				list->ResourceBarrier(1, &b);
				*statePtr = after;
			}
		}


		void D3D12SRVHandleRef::TransitionToSRV(ID3D12GraphicsCommandList* list)
		{
			TransitionResource(list, resource_, statePtr_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}

		void D3D12SRVHandleRef::TransitionToComputeSRV(ID3D12GraphicsCommandList* list)
		{
			TransitionResource(list, resource_, statePtr_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}

		void D3D12UAVHandleRef::TransitionToUAV(ID3D12GraphicsCommandList* list)
		{
			TransitionResource(list, resource_, statePtr_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}


		void D3D12Texture2D::Bind(ID3D12Resource* texture, D3D12_CPU_DESCRIPTOR_HANDLE stagingCPU)
		{
			Release();
			texture_          = texture;
			stagingCPUHandle_ = stagingCPU;
		}

		void D3D12Texture2D::Release()
		{
			SafeReleaseD3D12(texture_);
			stagingCPUHandle_ = {};
		}
	}
}
