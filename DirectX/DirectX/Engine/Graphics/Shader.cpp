#include "EnginePreCompile.h"
#include "Shader.h"
#include <utility>


namespace engine
{
	namespace graphics
	{
		namespace
		{
			const char* ToShaderTypeName(IShader::ShaderType shaderType)
			{
				switch (shaderType)
				{
					case IShader::ShaderType::VS: return "VS";
					case IShader::ShaderType::PS: return "PS";
					case IShader::ShaderType::CS: return "CS";
					default:                      return "Unknown";
				}
			}

			bool ToShaderType(const std::string& text, IShader::ShaderType& outShaderType)
			{
				if (text == "VS") {
					outShaderType = IShader::ShaderType::VS;
					return true;
				}
				if (text == "PS") {
					outShaderType = IShader::ShaderType::PS;
					return true;
				}
				if (text == "CS") {
					outShaderType = IShader::ShaderType::CS;
					return true;
				}
				return false;
			}
		}


		std::string BuildShaderResourceKey(
			const char* filePath,
			const char* entryFuncName,
			IShader::ShaderType shaderType)
		{
			std::string key = "shader:";
			key += filePath ? filePath : "";
			key += "|";
			key += entryFuncName ? entryFuncName : "";
			key += "|";
			key += ToShaderTypeName(shaderType);
			return key;
		}


		bool ParseShaderResourceKey(
			const std::string& key,
			ShaderResourceDesc& outDesc)
		{
			static const std::string PREFIX = "shader:";
			if (key.compare(0, PREFIX.size(), PREFIX) != 0) {
				return false;
			}

			const size_t firstSeparator = key.find('|', PREFIX.size());
			if (firstSeparator == std::string::npos) {
				return false;
			}

			const size_t secondSeparator = key.find('|', firstSeparator + 1);
			if (secondSeparator == std::string::npos) {
				return false;
			}

			ShaderResourceDesc desc;
			desc.filePath      = key.substr(PREFIX.size(), firstSeparator - PREFIX.size());
			desc.entryFuncName = key.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);

			const std::string shaderTypeText = key.substr(secondSeparator + 1);
			if (!ToShaderType(shaderTypeText, desc.shaderType)) {
				return false;
			}
			if (desc.filePath.empty() || desc.entryFuncName.empty()) {
				return false;
			}

			outDesc = std::move(desc);
			return true;
		}
	}
}
