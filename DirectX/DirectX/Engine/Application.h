#pragma once
#include "Graphics/RenderContext.h"

namespace engine
{
	/**
	 * アプリケーションを動作させる基本機能
	 */
	class IApplication
	{
	private:
	public:
		virtual bool Initialize() = 0;
		virtual void Finalize() = 0;
		virtual void Update(graphics::RenderContext& context) = 0;

		/**
		 * 登録用関数
		 * NOTE:Initialize後に呼ばれる
		 */
		virtual void Register() = 0;
	};
}