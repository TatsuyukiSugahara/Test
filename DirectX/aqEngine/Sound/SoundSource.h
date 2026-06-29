#pragma once
#include <cstdint>
#include <memory>
#include "Math/Vector.h"
#include "SoundTypes.h"
#include "SoundFwd.h"
#include "ISoundVoice.h"
#include "VolumeEnvelope.h"


namespace aq
{
	namespace sound
	{
		class SoundListener;

		// 3D 発音体（§2.2）。内部に ISoundVoice を 1 本持ち、Mixer3D の計算結果を反映する。
		// 3D 位置を持つソースは mono 入力を推奨（非 mono は距離減衰のみ適用）。
		// SoundEngine がプール所有し、SoundSourceHandle 経由でアクセスする。
		class SoundSource
		{
		// ── メンバ変数 ──
		private:
			std::unique_ptr<ISoundVoice> voice_;
			RefSoundClip                 clip_;
			SoundFormat                  format_;
			SoundBusId                   bus_    = SoundBusId::SE;
			bool                         isMono_ = false;

			// 3D パラメータ
			math::Vector3    position_     = math::Vector3(0.0f, 0.0f, 0.0f);
			math::Vector3    velocity_     = math::Vector3(0.0f, 0.0f, 0.0f);
			AttenuationModel attenuation_  = AttenuationModel::Inverse;
			float            minDistance_  = 1.0f;
			float            maxDistance_  = 1000.0f;
			float            dopplerFactor_ = 1.0f;
			float            pitch_        = 1.0f;
			float            volume_       = 1.0f;
			VolumeEnvelope   volumeEnv_;

		// ── メンバ関数 ──
		public:
			SoundSource(std::unique_ptr<ISoundVoice> voice, RefSoundClip clip, SoundBusId bus);
			~SoundSource();

			SoundBusId GetBus() const { return bus_; }

			SoundSource(const SoundSource&) = delete;
			SoundSource& operator=(const SoundSource&) = delete;

			// 再生制御（§2.2）
			void Play(const LoopRegion& loop = LoopRegion{});
			void Pause();
			void Resume();
			void Stop();
			bool IsPlaying() const;

			// 3D パラメータ設定
			void SetPosition(const math::Vector3& position) { position_ = position; }
			void SetVelocity(const math::Vector3& velocity) { velocity_ = velocity; }
			void SetAttenuation(AttenuationModel model)     { attenuation_ = model; }
			void SetDistances(float minDistance, float maxDistance)
			{
				minDistance_ = minDistance;
				maxDistance_ = maxDistance;
			}
			void SetDopplerFactor(float factor) { dopplerFactor_ = factor; }
			void SetPitch(float pitch)          { pitch_ = pitch; }
			void SetVolume(float volume)        { volume_ = volume; volumeEnv_.SetImmediate(volume); }

			// フェード。FadeOut は完了時に Stop する。
			void FadeIn(float seconds, float targetVolume = 1.0f);
			void FadeOut(float seconds);
			void FadeTo(float targetVolume, float seconds);

			// SoundEngine::Update から毎フレーム呼ばれる。フェードを進めて volume_ を更新する。
			void UpdateFade(float deltaTime);
			// SoundEngine::Update から毎フレーム呼ばれ、Mixer3D 結果をボイスへ反映する（§4）。
			void UpdateSpatialization(const SoundListener& listener);
		};
	}
}
