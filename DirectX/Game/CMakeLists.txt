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
#  .app バンドルの情報(Mac / iOS)
#
#  CMake の既定 Info.plist は CFBundleIdentifier / CFBundleName が **空文字列**になる。
#  識別子が空のバンドルは macOS から通常のアプリとして扱われず、
#  **ウィンドウがキーウィンドウにならずキーボードイベントがアプリのキューに届かない**
#  (P2.5 の入力実装で発覚。nextEventMatchingMask がほぼ nil を返す状態になる)。
#
#  iOS は必要なキーが macOS より多く(UILaunchScreen / 向き / デバイス能力 /
#  UIDeviceFamily。設計書/iOS移植設計.md §2.3)、既定テンプレートでは与えられないので
#  iOSInfo.plist.in を自前で持つ。**Mac 側の設定は従来のまま**にするため分岐を分ける。
# ----------------------------------------------------------------------------
if(IOS)
	set_target_properties(Game PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME          "AquaDash"
		MACOSX_BUNDLE_GUI_IDENTIFIER       "com.aqengine.aquadash"
		MACOSX_BUNDLE_BUNDLE_VERSION       "1.0"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
		MACOSX_BUNDLE_INFO_PLIST           "${CMAKE_CURRENT_SOURCE_DIR}/iOSInfo.plist.in"
		# Xcode は TARGETED_DEVICE_FAMILY(既定 "1" = iPhone のみ)から UIDeviceFamily を
		# 上書きするため、テンプレート側に 1,2 を書いてもここを指定しないと iPhone 専用になる。
		XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"

		# ------------------------------------------------------------------
		#  コード署名(実機のみ。設計書/iOS移植設計.md §2.3)
		#
		#  シミュレータは署名が要らないので、プリセット側で
		#  CODE_SIGNING_ALLOWED=NO を渡している。ここは実機向けの設定。
		#
		#  AQ_IOS_DEVELOPMENT_TEAM に Team ID(10 文字)を渡すと自動署名になる。
		#  未指定なら署名関連を何も設定しない(= 従来どおり、シミュレータ用に
		#  configure しても余計な要求が出ない)。
		#
		#      cmake --preset ios-xcode -D AQ_IOS_DEVELOPMENT_TEAM=XXXXXXXXXX
		#
		#  Team ID は Xcode に Apple ID を追加すると発行される。無料アカウントでも
		#  「Personal Team」として使えるが、プロビジョニングの有効期間は 7 日間。
		# ------------------------------------------------------------------
		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.aqengine.aquadash"
	)

	if(AQ_IOS_DEVELOPMENT_TEAM)
		set_target_properties(Game PROPERTIES
			XCODE_ATTRIBUTE_CODE_SIGN_STYLE      "Automatic"
			XCODE_ATTRIBUTE_DEVELOPMENT_TEAM     "${AQ_IOS_DEVELOPMENT_TEAM}"
		)
		message(STATUS "AquaDash: iOS の自動署名を有効化 (team = ${AQ_IOS_DEVELOPMENT_TEAM})")
	else()
		message(STATUS "AquaDash: AQ_IOS_DEVELOPMENT_TEAM が未指定のため署名を設定しません"
		               " (シミュレータのみ。実機は -D AQ_IOS_DEVELOPMENT_TEAM=<TeamID> が要ります)")
	endif()
elseif(APPLE)
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

if(ANDROID)
	# ANativeActivity_onCreate は native_app_glue(静的ライブラリ)の中にあり、
	# アプリ側の誰も参照しないためリンカがそのアーカイブメンバを落としてしまう。
	# 落ちると NativeActivity がエントリを見つけられず起動時に落ちるので、
	# 未定義シンボルとして明示的に要求して取り込ませる。
	target_link_options(Game PRIVATE "-u" "ANativeActivity_onCreate")
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
#  .app バンドルへの同梱(Mac / iOS。設計書 Mac移植設計.md §9 P5 /
#  iOS移植設計.md §7.3 の P2)
#
#  **ALL には入れない。**Assets が 92MB(iOS 時点で 98MB)あるため毎ビルドで
#  コピーするとイテレーションが目に見えて遅くなる。配布形態を作りたいときだけ
#  明示的に aqBundleApp を叩く。通常の cmake --build の挙動は従来どおり。
#  **この方針は iOS でも維持する**(設計書 iOS移植設計.md §7.3)。
#
#      cmake --build --preset macos-ninja-debug --target aqBundleApp
#
#  シェーダ生成物(Assets/Shader/msl・msl-ios・spv)はビルド生成物なので、
#  「生成してから同梱」する必要がある。aqCompileMsl / aqCompileSpv は
#  ルート CMakeLists.txt で add_dependencies(Game ...) されているので、
#  aqBundleApp -> Game -> aqCompileXxx と**推移的に**順序が付く
#  (この時点では aqCompileXxx がまだ定義されていないため直接は書けない)。
#
#  投入の中身は Tools/PackageApp/package_app.cmake 側。バンドルの構造
#  (macOS は Contents/Resources、iOS はバンドル直下)と、Metal のシェーダを
#  msl / msl-ios のどちらから取るかを AQ_PKG_PLATFORM で切り替える。
#
#  ------------------------------------------------------------------------
#  **iOS 実機では aqBundleApp -> 署名 の順序が要る(P5 の課題)**
#
#  署名済みのバンドルに後からファイルを足すと**署名が壊れて実機で起動できない**。
#  したがって実機向けには aqBundleApp をコード署名より **前** に走らせる必要がある
#  (Xcode ジェネレータではビルドフェーズの順序に注意)。
#  P2 の対象であるシミュレータは CODE_SIGNING_ALLOWED=NO で署名自体が走らないため、
#  ここでは問題にならない。実機対応は P5 で扱う。
# ----------------------------------------------------------------------------
if(APPLE)
	if(IOS)
		set(AQ_PKG_PLATFORM "iOS")
	else()
		set(AQ_PKG_PLATFORM "macOS")
	endif()

	add_custom_target(aqBundleApp
		COMMAND "${CMAKE_COMMAND}"
			"-DAQ_PKG_BUNDLE_DIR=$<TARGET_BUNDLE_DIR:Game>"
			"-DAQ_PKG_EXECUTABLE=$<TARGET_FILE:Game>"
			"-DAQ_PKG_ASSETS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/Assets"
			"-DAQ_PKG_ENGINE_ASSETS_DIR=${CMAKE_SOURCE_DIR}/aqEngine/Assets"
			"-DAQ_PKG_GRAPHICS_API=${AQ_GRAPHICS_API}"
			"-DAQ_PKG_PLATFORM=${AQ_PKG_PLATFORM}"
			"-DAQ_PKG_VULKAN_SDK=${AQ_VULKAN_SDK}"
			-P "${CMAKE_SOURCE_DIR}/Tools/PackageApp/package_app.cmake"
		VERBATIM
		COMMENT "Game.app へ Assets(と Vulkan ランタイム)を同梱"
	)
	add_dependencies(aqBundleApp Game)

	# ------------------------------------------------------------------
	#  iOS 実機向け: 同梱を **コード署名より前** に済ませる
	#
	#  署名済みのバンドルへ後からファイルを足すと署名が壊れて
	#  「インストールできるが起動しない」になる。Xcode の CodeSign は
	#  ターゲットの全ビルドフェーズの **後** に走るので、POST_BUILD
	#  (= 末尾の Run Script フェーズ)へ置けば順序が保証される。
	#
	#  **毎ビルド 98MB をコピーするわけではない。** package_app.cmake の
	#  file(COPY) はタイムスタンプが同じファイルを飛ばすので、実質は初回だけ。
	#  それでも手動 aqBundleApp を残してあるのは、Mac(署名しない)では
	#  従来どおり「配布形態を作りたいときだけ」で足りるため。
	# ------------------------------------------------------------------
	if(IOS)
		add_custom_command(TARGET Game POST_BUILD
			COMMAND "${CMAKE_COMMAND}"
				"-DAQ_PKG_BUNDLE_DIR=$<TARGET_BUNDLE_DIR:Game>"
				"-DAQ_PKG_EXECUTABLE=$<TARGET_FILE:Game>"
				"-DAQ_PKG_ASSETS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/Assets"
				"-DAQ_PKG_ENGINE_ASSETS_DIR=${CMAKE_SOURCE_DIR}/aqEngine/Assets"
				"-DAQ_PKG_GRAPHICS_API=${AQ_GRAPHICS_API}"
				"-DAQ_PKG_PLATFORM=iOS"
				-P "${CMAKE_SOURCE_DIR}/Tools/PackageApp/package_app.cmake"
			VERBATIM
			COMMENT "Game.app へ Assets を同梱(署名の前)"
		)
	endif()

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
