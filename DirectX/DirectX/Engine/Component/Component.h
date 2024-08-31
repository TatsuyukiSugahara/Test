#pragma once


// Componentを新たに定義した際に記載する
#define engineComponent(name) public:\
	static int32_t ID() { return util::Hash32(#name); }

namespace engine
{
	class IGameObject;

	namespace component
	{
		class IComponent
		{
		protected:
			engine::IGameObject* gameObject_;


		public:
			IComponent(IGameObject* gameObject)
				: gameObject_(gameObject)
			{
			}


			virtual ~IComponent() {}


		public:
			/** NOTE: 継承先で実装が必要な関数群 */

			virtual void Start() = 0;
			virtual void Update() = 0;
			virtual void Render(graphics::RenderContext& context) = 0;


		public:
			virtual void Awake() {}
			virtual void PreUpdate() {}
			virtual void PostUpdate() {}
			virtual void PreRender(graphics::RenderContext& context) {}
			virtual void PostRender(graphics::RenderContext& context) {}


		public:
			static int32_t ID() { return 0; }
		};




		/*******************************************/


		class ComponentManager
		{
		private:
			std::vector<IComponent*> components_;

		public:
			ComponentManager() {}
			virtual ~ComponentManager() {}
			
			
		public:
			void Start();
			void Update();
			void Render(graphics::RenderContext& context);

			void PreUpdate();
			void PostUpdate();
			void PreRender(graphics::RenderContext& context);
			void PostRender(graphics::RenderContext& context);


		public:
			/** コンポーネント追加 */
			template <typename T>
			T* AddComponent(IGameObject* gameObject)
			{
				T* component = new T(gameObject);
				components_.push_back(component);
				component->Awake();
				return component;
			}

			template <typename T>
			T* GetComponent()
			{
				for (auto* component : components_) {
					if (component->ID() == T::ID()) {
						return component;
					}
				}
				// 見つからない
				EngineAssert(false);
				return nullptr;
			}

			template <typename T>
			void RemoveComponent()
			{
				std::vector<IComponent*>::iterator it = components_.begin();
				while (it != components_.end()) {
					if (it->ID() == T::ID()) {
						auto* ptr = (*it);
						if (ptr) {
							delete ptr;
							ptr = nullptr;
						}
						components_.erase(it);
						break;
					}
					++it;
				}
			}

			void Release()
			{
				std::vector<IComponent*>::iterator it = components_.begin();
				while (it != components_.end()) {
					auto* ptr = (*it);
					if (ptr) {
						delete ptr;
						ptr = nullptr;
					}
					it = components_.erase(it);
				}
			}
		};
	}
}