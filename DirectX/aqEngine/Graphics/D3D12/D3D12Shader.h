#pragma once
#include "D3D12Common.h"
#include "Graphics/IShader.h"
#include <vector>
#include <string>


namespace aq
{
	namespace graphics
	{
		// ── D3D12 シェーダ (Phase 1) ──
		// .fx を D3DCompile で DXBC にコンパイルし blob を保持する。
		// VS の場合は入力レイアウト (D3D12_INPUT_ELEMENT_DESC) をリフレクションで構築し、
		// PSO 生成時に参照できるよう保持する (PSO キャッシュが消費)。
		class D3D12Shader : public IShader
		{
		private:
			ShaderType  type_   = ShaderType::VS;
			ID3DBlob*   blob_   = nullptr;

			// 入力レイアウト (VS のみ)。semanticNames_ が elements_ の SemanticName ポインタの実体を保持する。
			std::vector<D3D12_INPUT_ELEMENT_DESC> elements_;
			std::vector<std::string>              semanticNames_;

		public:
			D3D12Shader()           = default;
			~D3D12Shader() override { Release(); }

			bool   Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) override;
			void   Release() override;
			void*  GetByteCode() const override     { return blob_ ? blob_->GetBufferPointer() : nullptr; }
			size_t GetByteCodeSize() const override { return blob_ ? blob_->GetBufferSize() : 0; }

			ShaderType GetType() const { return type_; }

			// PSO 用入力レイアウトを返す (VS 以外は count=0)
			void GetInputLayout(const D3D12_INPUT_ELEMENT_DESC*& outElements, uint32_t& outCount) const
			{
				outElements = elements_.empty() ? nullptr : elements_.data();
				outCount    = static_cast<uint32_t>(elements_.size());
			}

		private:
			void BuildInputLayout();  // blob_ から入力シグネチャをリフレクション
		};
	}
}
