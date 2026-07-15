#pragma once
#include "Resource/Resource.h"
#include "Resource/ParticleSystemData.h"


namespace aq
{
	namespace res
	{
		/**
		 * `.particle` v1 読み込み。
		 *
		 * ワーカースレッド (Loading) で SimpleJson パース + カーブ/グラデーションの
		 * LUT 焼き込みまで行う。GPU 生成はないため FinalizeLoading は不要。
		 */
		class ParticleLoader : public aq::res::ResourceLoaderBase
		{
		public:
			ParticleLoader() = default;
			~ParticleLoader() = default;

			virtual aq::res::ResourceBase* Create() override { return new ParticleSystemData(); }

		private:
			bool Loading() override;
		};
	}
}
