#pragma once
#include <cstdint>
#include "Math/Vector.h"


namespace app
{
	static constexpr uint32_t INVALID_SCENE_ID = 0xffffffff;


	class IScene
	{
	public:
		virtual void Initialize() = 0;
		virtual void Update() = 0;
		virtual void Finalize() = 0;

		/** 影の投影中心として使うワールド座標を返す。デフォルトは原点。 */
		virtual aq::math::Vector3 GetFocusPosition() const { return aq::math::Vector3(0.f, 0.f, 0.f); }


	public:
		static constexpr uint32_t GetID() { return INVALID_SCENE_ID; }
	};




	/***************************/


	class SceneManager
	{
	private:
		IScene* currentScene_;
		uint32_t nextSceneId_;


	public:
		SceneManager();
		~SceneManager();

		void Update();

		IScene* GetCurrentScene() const { return currentScene_; }


	public:
		void RequestChangeScene(const uint32_t request) { nextSceneId_ = request; }


	private:
		static SceneManager* instance_;


	public:
		static void Create()
		{
			instance_ = new SceneManager();
		}
		static SceneManager& Get()
		{
			return *instance_;
		}
		static void Release()
		{
			delete instance_;
			instance_ = nullptr;
		}
	};
}