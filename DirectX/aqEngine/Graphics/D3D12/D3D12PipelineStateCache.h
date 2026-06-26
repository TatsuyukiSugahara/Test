#pragma once
#include "D3D12Common.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/IRenderContextImpl.h"  // DepthMode / BlendMode
#include <unordered_map>


namespace aq
{
	namespace graphics
	{
		class D3D12Shader;

		// ── PSO キャッシュ (Phase 1b) ──
		// (VS, PS, Blend, Depth, トポロジ型, RT/DS フォーマット) をキーに
		// ID3D12PipelineState をキャッシュする。入力レイアウトは VS から取得。
		class D3D12PipelineStateCache
		{
		public:
			struct Key
			{
				const void*                   vs = nullptr;
				const void*                   ps = nullptr;
				BlendMode                     blend = BlendMode::Opaque;
				DepthMode                     depth = DepthMode::ReadWrite;
				D3D12_PRIMITIVE_TOPOLOGY_TYPE topoType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				DXGI_FORMAT                   rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
				DXGI_FORMAT                   dsFormat = DXGI_FORMAT_UNKNOWN;

				bool operator==(const Key& o) const;
			};

			D3D12PipelineStateCache()  = default;
			~D3D12PipelineStateCache() { Release(); }

			// キャッシュ未ヒット時に PSO を生成して返す。失敗時 nullptr。
			ID3D12PipelineState* GetOrCreate(ID3D12Device* device, ID3D12RootSignature* rootSig,
			                                 D3D12Shader* vs, D3D12Shader* ps, const Key& key);
			void Release();

		private:
			struct KeyHash { size_t operator()(const Key& k) const; };
			std::unordered_map<Key, ID3D12PipelineState*, KeyHash> cache_;
		};
	}
}
