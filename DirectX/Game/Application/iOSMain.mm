#include "stdafx.h"
// iOS のエントリ。Win32 デスクトップは Main.cpp、UWP(Xbox 道A)は UWPMain.cpp、
// macOS は MacMain.mm、Android は AndroidMain.cpp が担うため、それ以外の構成では
// 空 TU になる(同一プロジェクトに 2 つのエントリを共存させないため)。
//
// Game/CMakeLists.txt は if(NOT APPLE) で .mm を除外しているだけで、APPLE は iOS でも
// 真になる。よって MacMain.mm と本ファイルは iOS / macOS の双方でコンパイルされ、
// どちらが実体を持つかはガードマクロが決める(CMake 側の振り分けは不要)。
#if defined(AQ_PLATFORM_IOS)


// P0 はリンクを通すためだけの骨格。
//
// TODO(P1): UIApplicationMain と AqAppDelegate へ差し替える(設計書/iOS移植設計.md §3.2)。
//   int main(int argc, char* argv[]) {
//       @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"AqAppDelegate"); }
//   }
//   ブートストラップ(PlatformiOS の生成 → Engine::Create → Initialize)は
//   application:didFinishLaunchingWithOptions: へ移す。
//
//   ★注意: UIApplicationMain は戻ってこないので、MacMain.mm のように main の末尾で
//   後始末はできない。Engine::Finalize / Engine::Release / aq::ShutdownMemory は
//   applicationWillTerminate: に置くこと(4 つのエントリ共通の「Engine と
//   プラットフォームを壊した後に ShutdownMemory」という順序はここでも守る)。
int main(int argc, char* argv[])
{
	// UNREFERENCED_PARAMETER は windows.h の定義なので iOS では使えない。
	(void)argc;
	(void)argv;

	return 0;
}
#endif // AQ_PLATFORM_IOS
