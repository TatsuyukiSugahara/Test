#include "stdafx.h"
// macOS のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp が
// 担うため、それ以外の構成では空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
#if defined(AQ_PLATFORM_MAC)
#import <Cocoa/Cocoa.h>
#include "Application.h"
#include "Platform/Mac/PlatformMac.h"
#include <cstdlib>


namespace
{
	/**
	 * .app 単体で起動するための下ごしらえ(設計書 Mac移植設計.md §9 P5)
	 *
	 * バンドルに Assets を同梱(Tools/PackageApp/package_app.cmake)しても、
	 * 以下の 2 つはアプリ側でしか手当てできない。エンジン初期化より前に呼ぶこと。
	 *
	 * 1. カレントディレクトリ
	 *    PlatformMac::GetContentRoot() は Contents/Resources を返すが、**これを見ていない
	 *    経路が 2 種類ある**:
	 *      - シェーダのパス解決(VulkanShader.cpp / MetalShader.mm /
	 *        MetalRenderContextImpl.mm の FindProjectRoot。CWD から上方へ Game/Assets を探す)
	 *      - サウンドとオーディオバンク(SoundStream / AudioDirector が
	 *        "Assets/..." を **CWD 相対のまま** fopen する)
	 *    そこで **CWD は Resources/Game へ移す**。こうすると
	 *      - "Assets/Sound/..." が Resources/Game/Assets/Sound/... に解決し、
	 *      - FindProjectRoot は 1 つ上の Resources で Game/Assets を見つける
	 *    の両方が同時に成り立つ。開発時の `cd Game && ...` と同じ形になる。
	 *    **Resources へ移すと前者だけしか満たせず、BGM とオーディオバンクが
	 *    読めなくなる**(実機で踏んだ)。設計書 §8-16。
	 *
	 * 2. Vulkan の ICD
	 *    ローダーは vkCreateInstance の時点で ICD 定義 JSON を探す。バンドル内の
	 *    MoltenVK_icd.json を指しておかないと、SDK を入れていない配布先で
	 *    VK_ERROR_INCOMPATIBLE_DRIVER(-9)になる。
	 *
	 * どちらも「.app から起動したとき」かつ「既存の指定が無いとき」だけ行う。
	 * 開発中の `source ~/.local/aq-mac-env.sh` + CWD=Game/ の起動を壊さないため。
	 */
	void SetupBundleEnvironment()
	{
		@autoreleasepool
		{
			// 非バンドル実行でも mainBundle は実行ファイルのディレクトリを返すので、
			// 拡張子で本物の .app だけを採る(PlatformMac::GetContentRoot と同じ判定)。
			NSBundle* bundle = [NSBundle mainBundle];
			if (![[[bundle bundlePath] pathExtension] isEqualToString:@"app"]) {
				return;
			}

			NSString* resourcePath = [bundle resourcePath];
			if (resourcePath == nil) {
				return;
			}

			NSFileManager* fileManager = [NSFileManager defaultManager];

			// 1) カレントディレクトリ。Assets 未同梱(aqBundleApp 未実行)のバンドルでは
			//    移さない。従来どおり CWD=Game/ で起動する開発フローを残すため。
			NSString* gamePath   = [resourcePath stringByAppendingPathComponent:@"Game"];
			NSString* assetsPath = [gamePath     stringByAppendingPathComponent:@"Assets"];
			BOOL isDirectory = NO;
			if ([fileManager fileExistsAtPath:assetsPath isDirectory:&isDirectory] && isDirectory) {
				[fileManager changeCurrentDirectoryPath:gamePath];
			}

#if defined(ENGINE_GRAPHICS_VULKAN)
			// 2) Vulkan の ICD 定義。VK_DRIVER_FILES が現行名、VK_ICD_FILENAMES は
			//    後方互換の旧名。どちらか一方でも設定済みなら環境側の指定を優先する。
			if (std::getenv("VK_DRIVER_FILES") == nullptr && std::getenv("VK_ICD_FILENAMES") == nullptr) {
				NSString* icdPath = [resourcePath stringByAppendingPathComponent:@"vulkan/icd.d/MoltenVK_icd.json"];
				if ([fileManager fileExistsAtPath:icdPath]) {
					const char* icdPathUtf8 = [icdPath UTF8String];
					setenv("VK_DRIVER_FILES", icdPathUtf8, 0);
					setenv("VK_ICD_FILENAMES", icdPathUtf8, 0);
				}
			}
#endif // ENGINE_GRAPHICS_VULKAN
		}
	}
}


int main(int argc, const char* argv[])
{
	// UNREFERENCED_PARAMETER は windows.h の定義なので Mac では使えない。
	(void)argc;
	(void)argv;

	// .app 単体起動の下ごしらえ。Vulkan ローダーが ICD を読むのも、起動診断ログを
	// CWD へ開くのも、この後の処理なので**最初に**行う。
	SetupBundleEnvironment();

	@autoreleasepool
	{
		// NSWindow を作る前に NSApplication を用意する。run を呼ばず自前でループを
		// 回すため、run が内部で行う finishLaunching(アプリのアクティブ化)は手動で呼ぶ。
		// TODO(Mac実機): 要確認 — 公式ドキュメントは「run が finishLaunching を呼ぶ」と
		// 書いているだけで、自前ループ時に手動で呼ぶ手順は明記されていない。これが
		// 抜けているとウィンドウが前面に来ない/キー入力が来ないことがある。
		// TODO(Mac実機): 要確認 — メニューの「終了」/ Cmd+Q は NSApplication の terminate:
		// が直接プロセスを落とすため、Engine::Finalize を通らない。P4 でアプリデリゲートの
		// applicationShouldTerminate: を用意して RequestExit へ寄せるか判断する。
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		[NSApp finishLaunching];

		// Mac プラットフォーム実装。ウィンドウ/イベントループの寿命は main が持つ。
		aq::platform::PlatformMac platform;

		aq::StartupMark("main");
		aq::Engine::Create();
		aq::Engine& engineInstance = aq::Engine::Get();
		engineInstance.CreateApplication<app::Application>();

		aq::InitializeParameter initializeParameter;
		initializeParameter.platform = &platform;
		initializeParameter.screenWidth = 1280;
		initializeParameter.screenHeight = 720;
		initializeParameter.renderWidth = 1280;
		initializeParameter.renderHeight = 720;
		if (engineInstance.Initialize(initializeParameter)) {
			engineInstance.RunGame();
		}
		engineInstance.Finalize();
	}

	return 0;
}
#endif // AQ_PLATFORM_MAC
