#pragma once
#include <string>
#include "IShader.h"

namespace engine
{
	namespace graphics
	{
		struct ShaderResourceDesc
		{
			std::string         filePath;
			std::string         entryFuncName;
			IShader::ShaderType shaderType = IShader::ShaderType::VS;
		};

		std::string BuildShaderResourceKey(
			const char* filePath,
			const char* entryFuncName,
			IShader::ShaderType shaderType);

		bool ParseShaderResourceKey(
			const std::string& key,
			ShaderResourceDesc& outDesc);
	}
}
