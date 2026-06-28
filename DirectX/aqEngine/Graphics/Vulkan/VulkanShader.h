#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IShader.h"
#include <vector>
#include <string>

namespace aq
{
	namespace graphics
	{
		// ── Vulkan シェーダ (Phase 1a) ──
		// 既存 .fx (HLSL) を DXC で実行時に SPIR-V へコンパイルする (.fx は無改変)。
		// register→binding 写像は -fvk-*-shift で機械的に行う (設計 §5.1, §0.2 で実証済)。
		//   b# → binding 0+#, t# → 16+#, s# → 32+#, u# → 48+#
		// SPIR-V エントリ名は -fspv-entrypoint-name=main で "main" に固定する。
		// VS は SPIRV-Reflect で入力レイアウト (VkVertexInputAttributeDescription) を構築する。
		class VulkanShader : public IShader
		{
		public:
			VulkanShader() = default;
			~VulkanShader() override { Release(); }

			bool   Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) override;
			void   Release() override;
			void*  GetByteCode() const override     { return spirv_.empty() ? nullptr : (void*)spirv_.data(); }
			size_t GetByteCodeSize() const override { return spirv_.size() * sizeof(uint32_t); }

			ShaderType    GetType() const       { return type_; }
			VkShaderModule GetModule() const    { return module_; }
			const char*   GetEntryPoint() const { return "main"; }

			// PSO 用入力レイアウト (VS のみ。VS 以外は count=0)。
			void GetInputLayout(const VkVertexInputAttributeDescription*& outAttrs, uint32_t& outCount,
			                    uint32_t& outStride) const
			{
				outAttrs  = attributes_.empty() ? nullptr : attributes_.data();
				outCount  = (uint32_t)attributes_.size();
				outStride = vertexStride_;
			}

		private:
			bool CompileToSpirv(const char* resolvedPath, const char* entry, ShaderType type);
			void BuildInputLayout();  // spirv_ から VS 入力シグネチャをリフレクション

			ShaderType                                     type_   = ShaderType::VS;
			std::vector<uint32_t>                          spirv_;
			VkShaderModule                                 module_ = VK_NULL_HANDLE;
			std::vector<VkVertexInputAttributeDescription> attributes_;
			uint32_t                                       vertexStride_ = 0;
		};
	}
}
