#pragma once
#include "Graphics/RenderContext.h"

namespace engine
{
	/**
	 * �A�v���P�[�V�����𓮍삳�����{�@�\
	 */
	class IApplication
	{
	private:
	public:
		virtual bool Initialize() = 0;
		virtual void Finalize() = 0;
		virtual void Update(graphics::RenderContext& context) = 0;

		/**
		 * �o�^�p�֐�
		 * NOTE:Initialize��ɌĂ΂��
		 */
		virtual void Register() = 0;
	};
}