#pragma once
#include <cstdint>
#include <vector>
#include "Rendering/Pipeline/PassResources.h"


namespace aq
{
	namespace rendering
	{
		struct RenderFrame;
		class RenderCommandList;


		/**
		 * 画面寸法の変更(リサイズ)への対応(設計書/エンジン課題解消設計.md P4)。
		 * IRenderPass は**ゲームが独自パスを足せる公開の拡張点**で、画面寸法の RT を持つパスが
		 * 何もせず旧寸法のまま残ると静かに壊れる。そこで既定は Unsupported(opt-in)にする。
		 */
		enum class ResizePolicy : uint8_t
		{
			Unsupported,   // 既定。1 つでもあるとパイプライン全体がリサイズ無効になる(ログに名前を出す)
			Independent,   // 画面寸法に依存する資源も、掲示板への登録(Scene 以外)も持たない
			Resizable,     // 画面寸法の RT を持つ。PrepareResize / CommitResize / AbortResize を実装する
		};


		/**
		 * パスの実行単位(設計書/レンダーパイプライン設計.md §1.1)
		 */
		enum class PassScope : uint8_t
		{
			Frame,   // フレームに 1 回(影・ポストプロセス・UI)
			View,    // ビューごと(分割画面ではビュー数だけ回る)
		};




		/**
		 * Build() に渡すビュー情報。
		 * Frame scope のパスには先頭ビュー相当(viewIndex=0 / 全画面矩形)が渡る。
		 */
		struct PassViewInfo
		{
			/** ビューの番号と総数 */
			uint32_t viewIndex = 0;
			uint32_t viewCount = 1;

			/** このビューのビューポート(ピクセル) */
			float x = 0.0f;
			float y = 0.0f;
			float w = 0.0f;
			float h = 0.0f;

			/** 全画面の寸法(ピクセル)。ポストプロセス等が RT 全体を扱うときに使う */
			float fullWidth  = 0.0f;
			float fullHeight = 0.0f;


			/** 先頭ビューか(GBuffer のクリアやパーティクルは先頭ビューだけが行う) */
			inline bool IsFirstView()  const { return viewIndex == 0; }
			/** 単一ビューか(Hi-Z やクラスタカリングは単一カメラ前提) */
			inline bool IsSingleView() const { return viewCount == 1; }
		};




		/**
		 * パスが読む / 書く掲示板キーの宣言。PipelineBuilder::Build() の検証に使う。
		 */
		class PassDeclaration
		{
		private:
			std::vector<PassResourceKey> reads_;
			std::vector<PassResourceKey> readsOptional_;
			std::vector<PassResourceKey> writes_;


		public:
			/** 必ず前段で書かれていなければならないキー */
			inline void Reads(const PassResourceKey key)         { reads_.push_back(key); }
			/** 無くても動くキー(例: Sky は Depth が無ければ深度なしで描く) */
			inline void ReadsOptional(const PassResourceKey key) { readsOptional_.push_back(key); }
			/** このパスが書く(登録する)キー */
			inline void Writes(const PassResourceKey key)        { writes_.push_back(key); }


		public:
			inline const std::vector<PassResourceKey>& GetReads()         const { return reads_; }
			inline const std::vector<PassResourceKey>& GetReadsOptional() const { return readsOptional_; }
			inline const std::vector<PassResourceKey>& GetWrites()        const { return writes_; }
		};




		/**
		 * レンダーパスの抽象。エンジンの標準パスもゲーム独自のパスも同じ型で、
		 * PipelineBuilder に Add / Insert してパイプラインを組む。
		 *
		 * 契約(設計書 §1.1):
		 *  1. 読むキーは DeclareResources で宣言する
		 *  2. 入った状態に戻して終わる(BlendMode=Opaque / DepthMode=ReadWrite)。RT のバインドは次のパスが自分で行う
		 *  3. Scene に描くパスは自分で SetRenderTargetWithDepthCommand(Scene, Depth) を積む
		 *     (Depth が無ければ SetRenderTargetCommand(Scene))。前のパスの終わり方に依存しない
		 *  4. Setup で作った RT は自分が所有し、デストラクタで解放する
		 *  5. Build はゲームスレッド、積んだコマンドの Execute はレンダースレッド
		 */
		class IRenderPass
		{
		public:
			virtual ~IRenderPass() = default;

			/** ログと Find<T> の表示に使う名前(静的文字列) */
			virtual const char* GetName() const = 0;

			/** 実行単位。既定はビューごと */
			virtual PassScope GetScope() const { return PassScope::View; }

			/** 機能要件。false のパスは Build() が除外してログに出す(例: Bloom は compute 必須) */
			virtual bool IsSupported() const { return true; }

			/** 読む / 書くキーの宣言 */
			virtual void DeclareResources(PassDeclaration& decl) const = 0;

			/**
			 * RT の生成と掲示板への登録。列の順に 1 回だけ呼ばれる。
			 * 失敗したら false(パイプライン全体の Build() が失敗する)
			 */
			virtual bool Setup(PassResources& res, const uint32_t width, const uint32_t height)
			{
				(void)res; (void)width; (void)height;
				return true;
			}

			/** リサイズへの対応。既定は Unsupported(opt-in)。Independent のパスは Scene 以外のキーを Writes() してはならない */
			virtual ResizePolicy GetResizePolicy() const { return ResizePolicy::Unsupported; }

			/**
			 * Resizable のパスだけが実装する(以下 3 関数。ゲームスレッドで、WaitIdle 済みの区間にだけ呼ばれる)。
			 *
			 * PrepareResize: 新寸法の RT を**一時領域へ**作る(旧 RT はそのまま残す)。
			 *                失敗したら自分で一時領域を片付けてから false を返す。
			 * CommitResize : 一時領域を本体へ入れ替え、**旧 RT を GraphicsDevice::ReleaseOffscreenRenderTarget で解放**し、
			 *                掲示板への登録と入力の取り直し(Setup の Wire 相当)をやり直す。
			 *                呼ぶ前に掲示板は初期状態(Scene のみ)へ戻され、列順に呼ばれるので、前段の登録は見える。
			 *                Setup が掲示板を見て「無効」と決めたパスは、その判定を保持して何もしない。
			 * AbortResize  : 一時領域を解放する(何度呼んでも安全。PrepareResize が true を返したパスにだけ呼ばれる)。
			 */
			virtual bool PrepareResize(const uint32_t width, const uint32_t height) { (void)width; (void)height; return true; }
			virtual void CommitResize(PassResources& res) { (void)res; }
			virtual void AbortResize() {}

			/**
			 * コマンドの記録(ゲームスレッド)。
			 * frame は非 const(クラスタカリングがアイテムの useGpuCull を確定させるため)。
			 * Frame scope のパスには frames[0] と全画面の view が渡る。
			 */
			virtual void Build(RenderFrame& frame, const PassViewInfo& view,
			                   PassResources& res, RenderCommandList& outList) = 0;
		};
	}
}
