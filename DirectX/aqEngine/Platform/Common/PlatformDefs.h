#pragma once

// プラットフォーム選択マクロ。
// AQ_PLATFORM_WIN32 / AQ_PLATFORM_UWP / AQ_PLATFORM_MAC / AQ_PLATFORM_IOS /
// AQ_PLATFORM_ANDROID の うち ちょうど 1 つだけが定義された状態を保証する。
// 通常はプロジェクト設定(vcxproj / CMake)で定義し、未定義ならコンパイラの
// 定義済みマクロから推定する。
//
// 最初期(aq.h の先頭)に読まれるため、他のヘッダを一切 include しないこと。
// 唯一の例外が下の <TargetConditionals.h>。Apple では __APPLE__ が macOS でも iOS でも
// 真になり、両者を分ける TARGET_OS_IPHONE がこのヘッダにしか無いため、
// コンパイラの定義済みマクロだけでは推定が成立しない。SDK が提供する
// マクロ定義だけのヘッダ(型も関数も宣言しない)なので、依存としては最小で済む。
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(AQ_PLATFORM_WIN32) && !defined(AQ_PLATFORM_UWP) && !defined(AQ_PLATFORM_MAC) && !defined(AQ_PLATFORM_IOS) && !defined(AQ_PLATFORM_ANDROID)
// __ANDROID__ は __linux__ も一緒に定義されるため、Linux より先に判定する。
#if defined(__ANDROID__)
#define AQ_PLATFORM_ANDROID
#elif defined(_WIN32)
#define AQ_PLATFORM_WIN32
// TARGET_OS_IPHONE は __APPLE__ も一緒に定義されるため、macOS より先に判定する
// (__ANDROID__ を _WIN32 より先に見ているのと同じ理由)。iPhone / iPad の実機に
// 加えてシミュレータでも真になる。実機とシミュレータの区別が要る場合は
// TARGET_OS_SIMULATOR を別に見ること。
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define AQ_PLATFORM_IOS
#elif defined(__APPLE__)
#define AQ_PLATFORM_MAC
#else
#error "Define exactly one AQ_PLATFORM_* platform"
#endif
#endif

#if (defined(AQ_PLATFORM_WIN32) + defined(AQ_PLATFORM_UWP) + defined(AQ_PLATFORM_MAC) + defined(AQ_PLATFORM_IOS) + defined(AQ_PLATFORM_ANDROID)) > 1
#error "Define exactly one AQ_PLATFORM_* platform"
#endif


// 派生マクロ。
//  AQ_PLATFORM_DESKTOP        : デスクトップ OS (Win32 / Mac)
//  AQ_PLATFORM_WINDOWS_FAMILY : windows.h 系 API が使える (Win32 / UWP)
//  AQ_PLATFORM_APPLE          : Apple プラットフォーム (Mac / iOS)
//
// Android / iOS はデスクトップにも Windows ファミリにも入らない。
// ウィンドウ/ファイルシステム/入力の前提がデスクトップと異なるため、
// 「デスクトップでない」ことを型で効かせる。
#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_MAC)
#define AQ_PLATFORM_DESKTOP
#endif

#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_UWP)
#define AQ_PLATFORM_WINDOWS_FAMILY
#endif

// Metal / GameController / AudioToolbox(ExtAudioFile)/ stb_image 経路・
// Objective-C の MRR 前提は、いずれも Mac と iOS でまったく同じ分岐になる。
// これが無いと defined(AQ_PLATFORM_MAC) || defined(AQ_PLATFORM_IOS) が各所へ散る。
// 逆に AppKit / デスクトップ前提のもの(NSWindow・Cocoa 入力など)は
// AQ_PLATFORM_MAC のまま残すこと。
#if defined(AQ_PLATFORM_MAC) || defined(AQ_PLATFORM_IOS)
#define AQ_PLATFORM_APPLE
#endif
