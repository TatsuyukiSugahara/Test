#include "../EnginePreCompile.h"
#include "GPUBuffer.h"
#include "../Engine.h"

namespace engine
{
	namespace graphics
	{
		GPUBuffer::GPUBuffer()
			: gpuBuffer_(nullptr)
		{
		}


		GPUBuffer::~GPUBuffer()
		{
			Release();
		}


		bool GPUBuffer::Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc)
		{
			Release();
			HRESULT hr;
			if (initialData) {
				D3D11_SUBRESOURCE_DATA data;
				data.pSysMem = initialData;
				hr = Engine::Get().GetD3DDevice()->CreateBuffer(&bufferDesc, &data, &gpuBuffer_);
			} else {
				hr = Engine::Get().GetD3DDevice()->CreateBuffer(&bufferDesc, NULL, &gpuBuffer_);
			}
			if (FAILED(hr)) {
				return false;
			}
			return true;
		}


		void GPUBuffer::Release()
		{
			if (gpuBuffer_) {
				gpuBuffer_->Release();
				gpuBuffer_ = nullptr;
			}
		}




		/*******************************************/


		ConstantBuffer::ConstantBuffer()
		{
		}


		ConstantBuffer::~ConstantBuffer()
		{
			Release();
		}

		
		bool ConstantBuffer::Create(const void* initialData, uint32_t bufferSize)
		{
			D3D11_BUFFER_DESC desc;
			memory::Clear(&desc, sizeof(desc));
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = (((bufferSize - 1) / 16) + 1) * 16; // 16バイトアライメントにする
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = 0;
			return GPUBuffer::Create(initialData, desc);
		}




		/*******************************************/


		StructuredBuffer::StructuredBuffer()
			: structuredBuffer_(nullptr)
		{
		}


		StructuredBuffer::~StructuredBuffer()
		{
			Release();
		}


		bool StructuredBuffer::Create(const void* initialData, D3D11_BUFFER_DESC& bufferDesc)
		{
			Release();
			HRESULT hr;
			if (initialData) {
				D3D11_SUBRESOURCE_DATA data;
				data.pSysMem = initialData;
				hr = Engine::Get().GetD3DDevice()->CreateBuffer(&bufferDesc, &data, &structuredBuffer_);
			}
			else {
				hr = Engine::Get().GetD3DDevice()->CreateBuffer(&bufferDesc, NULL, &structuredBuffer_);
			}
			if (FAILED(hr)) {
				return false;
			}
			return true;
		}


		void StructuredBuffer::Release()
		{
			if (structuredBuffer_) {
				structuredBuffer_->Release();
				structuredBuffer_ = nullptr;
			}
		}




		/*******************************************/


		VertexBuffer::VertexBuffer()
			: vertexBuffer_(nullptr)
			, stride_(0)
		{
		}


		VertexBuffer::~VertexBuffer()
		{
			Release();
		}


		bool VertexBuffer::Create(uint32_t vertexNum, uint32_t stride, const void* srcVertexBuffer)
		{
			Release();
			D3D11_BUFFER_DESC desc;
			memory::Clear(&desc, sizeof(desc));
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = stride * vertexNum;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = 0;
			D3D11_SUBRESOURCE_DATA initialData;
			memory::Clear(&initialData, sizeof(initialData));
			initialData.pSysMem = srcVertexBuffer;

			HRESULT hr = Engine::Get().GetD3DDevice()->CreateBuffer(&desc, &initialData, &vertexBuffer_);
			if (FAILED(hr)) {
				return false;
			}
			stride_ = stride;
			return true;
		}


		void VertexBuffer::Release()
		{
			if (vertexBuffer_) {
				vertexBuffer_->Release();
				vertexBuffer_ = nullptr;
			}
		}




		/*******************************************/


		IndexBuffer::IndexBuffer()
			: indexBuffer_(nullptr)
		{
		}


		IndexBuffer::~IndexBuffer()
		{
			Release();
		}


		bool IndexBuffer::Create(uint32_t indexNum, const void* srcIndexBuffer)
		{
			Release();
			D3D11_BUFFER_DESC desc;
			engine::memory::Clear(&desc, sizeof(desc));
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ByteWidth = sizeof(uint32_t) * indexNum;
			desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			desc.CPUAccessFlags = 0;
			D3D11_SUBRESOURCE_DATA initialData;
			memory::Clear(&initialData, sizeof(initialData));
			initialData.pSysMem = srcIndexBuffer;

			HRESULT hr = Engine::Get().GetD3DDevice()->CreateBuffer(&desc, &initialData, &indexBuffer_);
			if (FAILED(hr)) {
				return false;
			}
			return true;
		}


		void IndexBuffer::Release()
		{
			if (indexBuffer_) {
				indexBuffer_->Release();
				indexBuffer_ = nullptr;
			}
		}
	}
}