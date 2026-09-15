# ============================================================================
#  Sample(最小サンプル / 新しいプロジェクトの雛形)
#
#  箱が 1 個出るだけの実行ファイル。**新しくゲームを作るときの出発点**として置いている。
#  詳しくは Sample/README.md。
#
#  対象は Windows と macOS の 2 つだけ。iOS / Android / UWP のエントリは
#  形が違ううえ(特に iOS は起動の前半と後半が別のコールバックに分かれる)、
#  最初に触る環境ではないので持たない。必要になったら Game/Application から写す
#  (設計書/使いやすさ改善設計.md P3-B)。
# ============================================================================

file(GLOB_RECURSE AQ_SAMPLE_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.mm"
)
file(GLOB_RECURSE AQ_SAMPLE_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.h"
)

if(NOT APPLE)
	aq_exclude_sources(AQ_SAMPLE_SOURCES "\\.mm$")
endif()

add_executable(Sample WIN32 MACOSX_BUNDLE ${AQ_SAMPLE_SOURCES} ${AQ_SAMPLE_HEADERS})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AQ_SAMPLE_SOURCES} ${AQ_SAMPLE_HEADERS})

# CFBundleIdentifier が空だと macOS がウィンドウをキーウィンドウにせず、
# キーボードイベントがアプリのキューに届かない(Game/CMakeLists.txt と同じ理由)。
if(APPLE)
	set_target_properties(Sample PROPERTIES
		MACOSX_BUNDLE_BUNDLE_NAME       "aqEngine Sample"
		MACOSX_BUNDLE_GUI_IDENTIFIER    "com.aqengine.sample"
		MACOSX_BUNDLE_BUNDLE_VERSION    "1.0"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
	)
endif()

target_include_directories(Sample PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application"
)

# 定義(AQ_PLATFORM_* / ENGINE_GRAPHICS_* 等)とインクルードパスは aqEngine が
# PUBLIC で持っているのでここへ伝播してくる。
target_link_libraries(Sample PRIVATE aqEngine)

if(MSVC)
	target_link_options(Sample PRIVATE /ignore:4099)
endif()

# デバッグ実行時の作業ディレクトリ。コンテンツ基点は番兵(aqEngine/Assets)を
# 上方探索して見つけるので、リポジトリ内ならどこでもよいが Game と揃えておく。
set_target_properties(Sample PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(CMAKE_GENERATOR STREQUAL "Xcode")
	set_target_properties(Sample PROPERTIES
		XCODE_GENERATE_SCHEME          ON
		XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	)
endif()
