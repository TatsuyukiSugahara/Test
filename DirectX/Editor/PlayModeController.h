#pragma once
#include "Core/RunMode.h"
#include "Level/LevelId.h"
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	/**
	 * 再生・一時停止・再開・停止の遷移。
	 * ボタンの操作は要求として記録し、次のフレームの先頭(EditorLayer::OnBeginFrame の先頭)で ApplyPendingRequest がまとめて実行する。
	 * 再生の前に Edit のワールドを「ルート Level の出どころの組 + カメラ」として記録し、停止でワールドを空にしてから読み直す
	 */
	class PlayModeController
	{
	private:
		/** 遷移の要求 */
		enum class Request : uint8_t
		{
			None,
			Play,
			Pause,
			Resume,
			Stop,
		};


	private:
		/** 次のフレームの先頭で実行する要求(同じフレームに複数来たら最後のもの) */
		Request pendingRequest_ = Request::None;

		/** 再生前に記録した Edit のワールド */
		std::vector<std::string> savedLevelKeys_;          // ルート Level の出どころ(CollectLoadedRoots の順)
		aq::Camera::State        savedMainCamera_;
		aq::Camera::State        savedOffscreenCamera_;
		bool                     hasSavedMainCamera_      = false;
		bool                     hasSavedOffscreenCamera_ = false;
		bool                     hadPreview_              = false;   // 再生前に Level エディタのプレビューがあったか

		/** 停止で読み直した Level(復元の完了の判定に使う) */
		std::vector<aq::level::LevelId> restoredLevelIds_;


	public:
		/** 再生を要求する(Edit のときだけ受け付ける) */
		void RequestPlay();
		/** 一時停止を要求する(Play のときだけ受け付ける) */
		void RequestPause();
		/** 再開を要求する(Pause のときだけ受け付ける) */
		void RequestResume();
		/** 停止を要求する(Play / Pause のときだけ受け付ける) */
		void RequestStop();

		/** 実行待ちの要求があるか */
		inline bool HasPendingRequest() const { return pendingRequest_ != Request::None; }

		/**
		 * 記録した要求を実行する。EditorLayer::OnBeginFrame の先頭(FlushRender の後・ECS の反復の外)で毎フレーム 1 回呼ぶ。
		 * 実行の時点のモードで意味の無い要求は捨てる
		 */
		void ApplyPendingRequest();

		/**
		 * 停止で読み直した全 Level と、プレビューがあったならその Level が生成済みか(自己テストが復元の完了を待つのに使う)。
		 * まだ一度も停止していなければ、読み直す Level が無いので true
		 */
		bool IsWorldSpawned() const;


	private:
		/** Edit → Play */
		void BeginPlay();
		/** Play → Pause */
		void PausePlay();
		/** Pause → Play */
		void ResumePlay();
		/** Play / Pause → Edit */
		void StopPlay();

		/** Level をすべてアンロードし、残りのエンティティもすべて破棄する。WaitForRenderIdle 済みで呼ぶ */
		void ClearWorld();


	public:
		/** Level エディタの編集モデル(パネルが無ければ nullptr) */
		static aq::level::LevelEditModel* FindLevelEditModel();


	private:
		/** 今のモードで要求に意味があるか */
		static bool IsRequestValid(const Request request, const aq::RunMode mode);
	};
}
