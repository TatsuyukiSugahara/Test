# ============================================================================
#  aqEngine(静的ライブラリ)
#
#  ソースは file(GLOB_RECURSE ... CONFIGURE_DEPENDS) で拾い、プラットフォーム
#  別に除外する。除外なしの GLOB 結果は Engine.vcxproj の ClCompile 集合と
#  Graphics/GPUBuffer.cpp の 1 本だけ差があるので、それも明示的に落とす。
# ============================================================================

file(GLOB_RECURSE AQ_ENGINE_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.mm"
)
file(GLOB_RECURSE AQ_ENGINE_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)


# ----------------------------------------------------------------------------
#  構成に依らない除外
#
#  x64/ … MSBuild の中間出力。cppwinrt の生成ヘッダが 5000 本以上あるため、
#      ヘッダの GLOB がこれを拾うと生成が極端に遅くなる。build/ と .vs/ も同様。
#  Graphics/GPUBuffer.cpp … D3D11 のデバイスを直接触る旧実装。Engine.vcxproj の
#      ClCompile に無く現在はビルドされていない(参照元も無い)。GLOB では拾って
#      しまうため落とす。
#  Platform/PlatformUWP.cpp … UWP 専用。UWP は vcxproj 専用経路なので扱わない。
# ----------------------------------------------------------------------------
set(AQ_ENGINE_BUILD_DIR_PATTERNS
	"/x64/"
	"/build/"
	"/\\.vs/"
	"/Generated Files/"
)
aq_exclude_sources(AQ_ENGINE_SOURCES ${AQ_ENGINE_BUILD_DIR_PATTERNS})
aq_exclude_sources(AQ_ENGINE_HEADERS ${AQ_ENGINE_BUILD_DIR_PATTERNS})

aq_exclude_sources(AQ_ENGINE_SOURCES
	"/Graphics/GPUBuffer\\.cpp$"
	"/Platform/PlatformUWP\\.cpp$"
)


# ----------------------------------------------------------------------------
#  プラットフォーム別の除外
#
#  Graphics/D3D11・Graphics/D3D12 は Windows のみ。
#  Graphics/Vulkan は「常に含める」。既存 Engine.vcxproj も 3 バックエンドを
#  無条件に ClCompile しており、非 Vulkan 構成では VulkanXxx.cpp 冒頭の
#  #ifdef ENGINE_GRAPHICS_VULKAN で空 TU になる(Vulkan SDK は要求されない)。
#  この既存方針にそのまま合わせる。
#
#  Sound/XAudio2・MFDecoder・VideoPlayer はまだ AQ_PLATFORM_* ガードが入って
#  いない(P1 / P4 の作業)ため、非 Windows ではファイルごと除外する。
#  HID/* と Platform/PlatformWin32.cpp は既にガード済みだが、意図を明示する
#  ため同様に除外しておく(空 TU を並べる意味が無いため)。
#
#  Windows / Mac / Android の 3 分岐。Android は「非 Windows 共通の除外」に
#  加えて Mac 専用実装も落とす必要があるため、除外集合を名前で持つ。
# ----------------------------------------------------------------------------
set(AQ_ENGINE_WINDOWS_ONLY_PATTERNS
	"/Graphics/D3D11/"
	"/Graphics/D3D12/"
	"/Platform/PlatformWin32\\.cpp$"
	"/Platform/Common/DebugOutputWin32\\.cpp$"
	"/HID/XInputPadBackend\\.cpp$"
	"/HID/Win32PadBackend\\.cpp$"
	"/HID/DualSensePadBackend\\.cpp$"
	"/HID/WinRTGamepadBackend\\.cpp$"
	"/Sound/XAudio2/"
	"/Sound/Decoder/MFDecoder\\.cpp$"
	"/Sound/Video/VideoPlayer\\.cpp$"
)

set(AQ_ENGINE_MAC_ONLY_PATTERNS
	"/Platform/Common/DebugOutputMac\\.cpp$"
	"/Platform/Mac/"
	"/HID/Mac/"
	"/Sound/CoreAudio/"
	"/Graphics/Metal/"
	"\\.mm$"
)

#  Android 専用実装。現時点の実体は DebugOutputAndroid.cpp だけで、残りは
#  後続フェーズで増える(Platform/Android・HID/Android・Sound/Oboe)。
#  先にパターンを置いておけば、ファイルを追加するだけで他プラットフォームの
#  ビルドから自動的に外れる。
set(AQ_ENGINE_ANDROID_ONLY_PATTERNS
	"/Platform/Common/DebugOutputAndroid\\.cpp$"
	"/Platform/Android/"
	"/HID/Android/"
	"/Sound/Oboe/"
)

#  ANDROID を先に判定すること(Android は UNIX が真になるため、
#  「Windows でなければ Mac」という分岐では Mac 側へ落ちてしまう)。
if(ANDROID)
	aq_exclude_sources(AQ_ENGINE_SOURCES
		${AQ_ENGINE_WINDOWS_ONLY_PATTERNS}
		${AQ_ENGINE_MAC_ONLY_PATTERNS}
	)
elseif(WIN32)
	aq_exclude_sources(AQ_ENGINE_SOURCES
		${AQ_ENGINE_MAC_ONLY_PATTERNS}
		${AQ_ENGINE_ANDROID_ONLY_PATTERNS}
	)
else()
	aq_exclude_sources(AQ_ENGINE_SOURCES
		${AQ_ENGINE_WINDOWS_ONLY_PATTERNS}
		${AQ_ENGINE_ANDROID_ONLY_PATTERNS}
	)
endif()


add_library(aqEngine STATIC ${AQ_ENGINE_SOURCES} ${AQ_ENGINE_HEADERS})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AQ_ENGINE_SOURCES} ${AQ_ENGINE_HEADERS})

aq_apply_common_compile_options(aqEngine)
aq_apply_platform_definitions(aqEngine PUBLIC)


# ----------------------------------------------------------------------------
#  PCH
#
#  Engine.vcxproj の PrecompiledHeaderFile = aq.h に対応。CMake は /FI 相当で
#  強制インクルードするが、各 .cpp 側の #include "aq.h" は #pragma once により
#  二重に効かない。
# ----------------------------------------------------------------------------
target_precompile_headers(aqEngine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/aq.h")


# ----------------------------------------------------------------------------
#  インクルードパス
#
#  Engine.vcxproj の AdditionalIncludeDirectories / IncludePath に対応。
#  ThirdParty と BulletPhysics/src は各 ThirdParty ターゲットが INTERFACE で
#  持っているのでここには書かない。FBX SDK($(FBXSDK_DIR))は vcxproj に
#  記述があるものの、実際に fbxsdk.h を include するソースが 1 本も無いため
#  CMake 側には持ち込まない。
# ----------------------------------------------------------------------------
target_include_directories(aqEngine PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}"
)


# ----------------------------------------------------------------------------
#  プリプロセッサ定義
#
#  Engine.vcxproj Debug|x64  : _DEBUG;_WINDOWS;BT_*;AQ_IMGUI;AQ_DEBUG_IMGUI
#            Release|x64     : NDEBUG;_WINDOWS;BT_*;AQ_IMGUI
#  _DEBUG / NDEBUG は CMake の既定フラグが付ける。_WINDOWS と AQ_PLATFORM_* は
#  aq_apply_platform_definitions が、BT_* は bullet ターゲットが伝播させる。
# ----------------------------------------------------------------------------
#  ENGINE_GRAPHICS_* は必ず大文字にする。AQ_GRAPHICS_API の値は MSBuild の
#  AqGraphicsApi と表記を揃えて "Vulkan" なので、そのまま連結すると
#  ENGINE_GRAPHICS_Vulkan になり、コードが見ている ENGINE_GRAPHICS_VULKAN と
#  一致しない(D3D11 / D3D12 は元から大文字なので、この取り違えは Vulkan 構成
#  でしか表面化せず、リンク時に D3D12 の未定義シンボルとして出る)。
# ----------------------------------------------------------------------------
string(TOUPPER "${AQ_GRAPHICS_API}" AQ_GRAPHICS_API_UPPER)

target_compile_definitions(aqEngine PUBLIC
	AQ_IMGUI
	$<$<CONFIG:Debug>:AQ_DEBUG_IMGUI>
	ENGINE_GRAPHICS_${AQ_GRAPHICS_API_UPPER}
)


# ----------------------------------------------------------------------------
#  ThirdParty
# ----------------------------------------------------------------------------
target_link_libraries(aqEngine PUBLIC
	imgui
	directxtex
	ufbx
	spirv_reflect
	vma
	bullet
)


# ----------------------------------------------------------------------------
#  プラットフォームライブラリ
#
#  かつて aq.h / 各 .cpp の #pragma comment(lib) が担っていた分をここへ移す。
#  Game へも伝播させる必要があるので PUBLIC。
# ----------------------------------------------------------------------------
if(ANDROID)
	# ----------------------------------------------------------------------
	#  native_app_glue
	#
	#  NDK はこれをプリビルドではなくソースで配っているので、ここでビルドする。
	#  役割は 2 つ:
	#   - NativeActivity が探す ANativeActivity_onCreate を提供する
	#   - 専用スレッドでアプリの android_main を呼び、Activity のライフサイクルと
	#     入力を ALooper 経由のキューに変換する(PlatformAndroid が読む側)
	#
	#  C のソースなので aqEngine(PCH = aq.h)には混ぜず、独立ターゲットにする。
	# ----------------------------------------------------------------------
	if(DEFINED ANDROID_NDK)
		set(AQ_ANDROID_NDK_ROOT "${ANDROID_NDK}")
	else()
		set(AQ_ANDROID_NDK_ROOT "${CMAKE_ANDROID_NDK}")
	endif()
	set(AQ_NATIVE_APP_GLUE_DIR "${AQ_ANDROID_NDK_ROOT}/sources/android/native_app_glue")

	if(NOT EXISTS "${AQ_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
		message(FATAL_ERROR "native_app_glue が見つかりません: ${AQ_NATIVE_APP_GLUE_DIR}")
	endif()

	add_library(native_app_glue STATIC "${AQ_NATIVE_APP_GLUE_DIR}/android_native_app_glue.c")
	target_include_directories(native_app_glue PUBLIC "${AQ_NATIVE_APP_GLUE_DIR}")
	target_link_libraries(native_app_glue PUBLIC log android)

	# NDK の sysroot が持つ共有ライブラリ。
	#   log     : __android_log_write(DebugOutputAndroid.cpp)
	#   android : ANativeWindow / AAssetManager / ALooper
	target_link_libraries(aqEngine PUBLIC
		native_app_glue
		log
		android
	)
elseif(WIN32)
	# API 非依存
	target_link_libraries(aqEngine PUBLIC
		dinput8
		dxguid
		xinput
		xaudio2
		mfplat
		mfreadwrite
		mfuuid
		propsys
		setupapi
		hid
		winmm
		comctl32
	)

	# API 別(Game/GraphicsApi.props の AdditionalDependencies と同じ集合)
	if(AQ_GRAPHICS_API STREQUAL "D3D11")
		target_link_libraries(aqEngine PUBLIC d3d11 d3dcompiler)
	elseif(AQ_GRAPHICS_API STREQUAL "D3D12")
		target_link_libraries(aqEngine PUBLIC d3d12 dxgi d3dcompiler)
	endif()
elseif(APPLE)
	# Mac のフレームワーク。実装(PlatformMac / CocoaInput / CoreAudio)は
	# P2 以降で追加する(設計書 §9)。
	target_link_libraries(aqEngine PUBLIC
		"-framework Cocoa"
		"-framework QuartzCore"
		"-framework Metal"
		"-framework GameController"
		"-framework AudioToolbox"
		"-framework CoreAudio"
		"-framework AVFoundation"
	)
endif()


# ----------------------------------------------------------------------------
#  Vulkan
#
#  SDK のパス解決(AQ_VULKAN_SDK)はルート CMakeLists.txt で行う。AQ_GRAPHICS_API
#  が Vulkan 以外ならこの分岐に入らないため、SDK 未導入の環境でも configure は
#  成功する。Windows は $VULKAN_SDK/Lib の vulkan-1.lib と dxcompiler.lib、
#  Mac は SDK が入れた libvulkan(MoltenVK)を使う。
# ----------------------------------------------------------------------------
if(AQ_GRAPHICS_API STREQUAL "Vulkan" AND ANDROID)
	# Android は NDK の sysroot がヘッダ(vulkan/vulkan.h)と libvulkan.so の
	# 両方を持つため、Vulkan SDK を要求しない(ルート CMakeLists.txt 側でも
	# VULKAN_SDK の要求から除外している)。
	# シェーダは事前生成した .spv を読む経路のみ。実行時 DXC は Windows 専用で、
	# VulkanShader.cpp が AQ_PLATFORM_WIN32 でガードしている。
	target_link_libraries(aqEngine PUBLIC vulkan)
elseif(AQ_GRAPHICS_API STREQUAL "Vulkan")
	target_include_directories(aqEngine PUBLIC "${AQ_VULKAN_SDK}/Include")

	if(WIN32)
		target_link_directories(aqEngine PUBLIC "${AQ_VULKAN_SDK}/Lib")
		target_link_libraries(aqEngine PUBLIC vulkan-1)
		# HLSL -> SPIR-V の実行時コンパイル用。dxcompiler.dll は実行時に必要。
		if(EXISTS "${AQ_VULKAN_SDK}/Lib/dxcompiler.lib")
			target_link_libraries(aqEngine PUBLIC dxcompiler)
		endif()
	else()
		# Mac は SDK 同梱の libvulkan.dylib(MoltenVK 経由)。検証は P2。
		target_link_directories(aqEngine PUBLIC "${AQ_VULKAN_SDK}/lib")
		target_link_libraries(aqEngine PUBLIC vulkan)
	endif()
endif()
