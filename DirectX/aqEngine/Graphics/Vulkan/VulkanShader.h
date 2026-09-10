#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IShader.h"
#include <vector>
#include <string>

namespace aq
{
	namespace graphics
	{
		// ── Vulkan シェーダ (Phase 1a / Mac 移植 P2) ──
		// SPIR-V の入手経路は 2 つ。まず <shaderDir>/spv/<stem>.<entry>.<vs|ps|cs>.spv を探し、
		// 無ければ既存 .fx (HLSL) を DXC で実行時に SPIR-V へコンパイルする (.fx は無改変)。
		// 後者は Windows 専用 (Mac に実行時 DXC が無いため。Mac移植設計.md §4)。
		// .spv はビルド時に Tools/ShaderCompile/compile_spv.cmake が生成する。
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
			// outStride は binding 0(per-vertex)の stride。
			void GetInputLayout(const VkVertexInputAttributeDescription*& outAttrs, uint32_t& outCount,
			                    uint32_t& outStride) const
			{
				outAttrs  = attributes_.empty() ? nullptr : attributes_.data();
				outCount  = (uint32_t)attributes_.size();
				outStride = vertexStride_;
			}

			/**
			 * binding 1(per-instance ストリーム)の stride。インスタンス属性が無ければ 0。
			 *
			 * D3D12 と同じく「セマンティクスが `I_` で始まる入力は per-instance(slot1)」
			 * という規約で分けている(D3D12Shader.cpp の perInstance 判定と対)。
			 */
			uint32_t GetInstanceStride() const { return instanceStride_; }

		private:
			// 事前ビルドされた .spv を読む (見つからなければ false)。
			bool LoadSpirvBinary(const char* resolvedPath, const char* entry, ShaderType type);
#if defined(AQ_PLATFORM_WIN32)
			// DXC による実行時コンパイル (Windows 専用のフォールバック)。
			bool CompileToSpirv(const char* resolvedPath, const char* entry, ShaderType type);
#endif
			void BuildInputLayout();  // spirv_ から VS 入力シグネチャをリフレクション

			ShaderType                                     type_   = ShaderType::VS;
			std::vector<uint32_t>                          spirv_;
			VkShaderModule                                 module_ = VK_NULL_HANDLE;
			std::vector<VkVertexInputAttributeDescription> attributes_;
			uint32_t                                       vertexStride_   = 0;   // binding 0 (per-vertex)
			uint32_t                                       instanceStride_ = 0;   // binding 1 (per-instance)
		};
	}
}
