#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Resource/Resource.h"
#include "Particle/ParticleTypes.h"


namespace aq
{
	namespace res
	{
		/**
		 * `.particle` v1 の焼き込み済みデータ本体。
		 * ParticleLoader がワーカースレッドで構築し、以後イミュータブルに扱う。
		 */
		struct ParticleSystemInfo
		{
			std::string                       name;
			std::vector<std::string>          warnings;   // エクスポート時警告 (ツール表示用)
			std::vector<particle::EmitterData> emitters;
		};


		/**
		 * パーティクルシステムのリソース (イミュータブル)。
		 *
		 * 実行時はカーブ評価も JSON も触らない。全カーブ/グラデーションは
		 * ロード時に LUT へ焼き込み済み (particleフォーマット仕様v1 §4.2/§4.4)。
		 */
		class ParticleSystemData : public aq::res::ResourceBase
		{
			engineResource(aq::res::ParticleSystemData);

		public:
			ParticleSystemData()
				: ResourceBase()
			{
				data_ = new ParticleSystemInfo();
			}

			virtual ~ParticleSystemData()
			{
				delete static_cast<ParticleSystemInfo*>(data_);
				data_ = nullptr;
			}


		public:
			const std::string& GetName()     const { return Get()->name; }
			uint32_t           GetEmitterCount() const { return static_cast<uint32_t>(Get()->emitters.size()); }
			const particle::EmitterData& GetEmitter(const uint32_t index) const { return Get()->emitters[index]; }
			const std::vector<particle::EmitterData>& GetEmitters() const { return Get()->emitters; }
			const std::vector<std::string>&   GetWarnings() const { return Get()->warnings; }

			/** ローダーが構築に使う書き込み用アクセサ (ワーカースレッドから) */
			ParticleSystemInfo* GetWritable() const { return Get(); }


		private:
			inline ParticleSystemInfo* Get() const { return static_cast<ParticleSystemInfo*>(data_); }
		};
		using RefParticleSystemResource = std::shared_ptr<ParticleSystemData>;
	}
}
