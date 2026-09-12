# ============================================================================
#  Game(AquaDash 実行ファイル)
#
#  ソース集合は Game/GameSources.props と同じ。ただし UWP 専用エントリの
#  Application/UWPMain.cpp は CMake では扱わない(UWP は GameUWP.vcxproj の
#  経路)。Application/Main.cpp は AQ_PLATFORM_WIN32 でガードされているため、
#  Mac では空 TU になる。
# ============================================================================

file(GLOB_RECURSE AQ_GAME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.mm"
)
file(GLOB_RECURSE AQ_GAME_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.h"
)

aq_exclude_sources(AQ_GAME_SOURCES
	"/Application/UWPMain\\.cpp$"
)

if(NOT APPLE)
	aq_exclude_sources(AQ_GAME_SOURCES "\\.mm$")
endif()


# ----------------------------------------------------------------------------
#  実行ファイル
#
#  Windows は WIN32(SubSystem=Windows / WinMain)。既存 DirectX.vcxproj の
#  x64 構成の SubSystem と同じ。
#  Mac は .app バンドル。バンドルへの Assets 同梱は P5 の作業(設計書 §9)。
#
#  Android だけ実行ファイルではなく **共有ライブラリ(libGame.so)** になる。
#  アプリの実行主体は Java/Kotlin 側の Activity で、ネイティブコードは
#  System.loadLibrary で読み込まれるため。エントリも WinMain / main ではなく
#  android_main(P1 で追加)。
# ----------------------------------------------------------------------------
if(ANDROID)
	add_library(Game SHARED ${AQ_GAME_SOURCES} ${AQ_GAME_HEADERS})
else()
	add_executable(Game WIN32 MACOSX_BUNDLE ${AQ_GAME_SOURCES} ${AQ_GAME_HEADERS})
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AQ_GAME_SOURCES} ${AQ_GAME_HEADERS})

# ----------------------------------------------------------------------------
#  .app バンドルの情報(Mac のみ)
#
#  CMake の既定 Info.plist は CFBundleIdentifier / CFBundleName が **空文字列**になる。
#  識別子が空のバンドルは macOS から通常のアプリとして扱われず、
#  **ウィンドウがキーウィンドウにならずキーボードイベントがアプリのキューに届かない**
#  (P2.5 の入力実装で発覚。nextEventMatchingMask がほぼ nil を返す状態になる)。
# ----------------------------------------------------------------------------
if(APPLE)
	set_target_properties(Game PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME          "AquaDash"
		MACOSX_BUNDLE_GUI_IDENTIFIER       "com.aqengine.aquadash"
		MACOSX_BUNDLE_BUNDLE_VERSION       "1.0"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
	)
endif()


aq_apply_common_compile_options(Game)

target_include_directories(Game PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application"
)

# aqEngine が PUBLIC で持つ定義(AQ_PLATFORM_* / ENGINE_GRAPHICS_* / AQ_IMGUI /
# BT_*)とインクルードパス、プラットフォームライブラリはここで伝播してくる。
target_link_libraries(Game PRIVATE aqEngine)

# DirectX.vcxproj の PrecompiledHeaderFile = stdafx.h に対応。
target_precompile_headers(Game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Application/stdafx.h")

if(MSVC)
	# Bullet の prebuilt .lib に PDB が揃っていないため出る LNK4099 を抑止する
	# (DirectX.vcxproj の Link/AdditionalOptions と同じ)。
	target_link_options(Game PRIVATE /ignore:4099)
endif()


# ----------------------------------------------------------------------------
#  デバッグ実行時の作業ディレクトリ
#
#  既存 vcxproj は LocalDebuggerWorkingDirectory を明示していないため、既定の
#  $(ProjectDir) = Game/ が使われる。Resource.cpp の FindProjectRoot() は
#  カレントディレクトリから上へ Game/Assets を探すので、ここを合わせないと
#  アセットが見つからない。
# ----------------------------------------------------------------------------
set_target_properties(Game PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Game"
)

# Xcode も同じ扱いにする。加えて、Xcode から実行(⌘R)するときは
# ターミナルの環境を引き継がないため、Vulkan の環境変数をスキームに焼き込む。
# これが無いと MoltenVK の ICD が見つからず vkCreateInstance が
# VK_ERROR_INCOMPATIBLE_DRIVER(-9)で落ちる(実機確認済み)。
# 値は Vulkan SDK の setup-env.sh が設定するものと同じ。
if(CMAKE_GENERATOR STREQUAL "Xcode")
	set_target_properties(Game PROPERTIES
		XCODE_GENERATE_SCHEME         ON
		XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Game"
	)
	if(AQ_VULKAN_SDK)
		set_property(TARGET Game PROPERTY XCODE_SCHEME_ENVIRONMENT
			"VULKAN_SDK=${AQ_VULKAN_SDK}"
			"VK_ICD_FILENAMES=${AQ_VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
			"VK_DRIVER_FILES=${AQ_VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
			"VK_ADD_LAYER_PATH=${AQ_VULKAN_SDK}/share/vulkan/explicit_layer.d"
			"DYLD_LIBRARY_PATH=${AQ_VULKAN_SDK}/lib"
		)
	endif()
endif()


# ----------------------------------------------------------------------------
#  Vulkan 構成の実行時 DLL
#
#  DirectX.vcxproj の PostBuildEvent と同じ。VulkanShader.cpp が実行時に
#  dxcompiler.dll(と dxil.dll)を必要とするため出力先へコピーする。
# ----------------------------------------------------------------------------
if(WIN32 AND AQ_GRAPHICS_API STREQUAL "Vulkan")
	foreach(vulkanDll dxcompiler.dll dxil.dll)
		if(EXISTS "${AQ_VULKAN_SDK}/Bin/${vulkanDll}")
			add_custom_command(TARGET Game POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${AQ_VULKAN_SDK}/Bin/${vulkanDll}"
					"$<TARGET_FILE_DIR:Game>"
				COMMENT "Vulkan ランタイム DLL をコピー: ${vulkanDll}"
			)
		endif()
	endforeach()
endif()


# ----------------------------------------------------------------------------
#  .app バンドルへの同梱(Mac のみ。設計書 Mac移植設計.md §9 P5)
#
#  **ALL には入れない。**Assets が 92MB あるため毎ビルドでコピーすると
#  イテレーションが目に見えて遅くなる。配布形態を作りたいときだけ
#  明示的に aqBundleApp を叩く。通常の cmake --build の挙動は従来どおり。
#
#      cmake --build --preset macos-ninja-debug --target aqBundleApp
#
#  シェーダ生成物(Assets/Shader/msl・spv)はビルド生成物なので、
#  「生成してから同梱」する必要がある。aqCompileMsl / aqCompileSpv は
#  ルート CMakeLists.txt で add_dependencies(Game ...) されているので、
#  aqBundleApp -> Game -> aqCompileXxx と**推移的に**順序が付く
#  (この時点では aqCompileXxx がまだ定義されていないため直接は書けない)。
#
#  投入の中身は Tools/PackageApp/package_app.cmake 側。
# ----------------------------------------------------------------------------
if(APPLE)
	add_custom_target(aqBundleApp
		COMMAND "${CMAKE_COMMAND}"
			"-DAQ_PKG_BUNDLE_DIR=$<TARGET_BUNDLE_DIR:Game>"
			"-DAQ_PKG_EXECUTABLE=$<TARGET_FILE:Game>"
			"-DAQ_PKG_ASSETS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/Assets"
			"-DAQ_PKG_GRAPHICS_API=${AQ_GRAPHICS_API}"
			"-DAQ_PKG_VULKAN_SDK=${AQ_VULKAN_SDK}"
			-P "${CMAKE_SOURCE_DIR}/Tools/PackageApp/package_app.cmake"
		VERBATIM
		COMMENT "Game.app へ Assets(と Vulkan ランタイム)を同梱"
	)
	add_dependencies(aqBundleApp Game)

	# Vulkan 構成は実行ファイルが @rpath/libvulkan.1.dylib を参照するため、
	# バンドル内の Frameworks を探索パスに足す。CMake が自動で足す
	# Vulkan SDK の lib より **前** に来るよう BUILD_RPATH の先頭に入れる
	# (SDK が先だと、同梱した dylib ではなく開発機の SDK を読んでしまい、
	#  自己完結しているかどうかを取り違える)。
	if(AQ_GRAPHICS_API STREQUAL "Vulkan")
		set_property(TARGET Game APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
		set_property(TARGET Game APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
	endif()
endif()
