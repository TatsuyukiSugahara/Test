#!/bin/zsh
# ----------------------------------------------------------------------------
#  Mac: Xcode プロジェクトを生成して開く
#
#  使い方(どこから実行してもよい):
#    Tools/SetupCMake/open_xcode.sh            # Vulkan(macos-xcode)
#    Tools/SetupCMake/open_xcode.sh metal      # ネイティブ Metal(macos-xcode-metal)
#    Tools/SetupCMake/open_xcode.sh metal --no-open   # 生成だけ
#
#  Xcode はターミナルの環境を引き継がないので、VULKAN_SDK などはここで用意してから
#  configure する。configure 時に SDK のパスが CMake のキャッシュ(AQ_VULKAN_SDK_ROOT)へ
#  残るため、以降 Xcode の ZERO_CHECK が CMake を再実行しても止まらない。
#  ソースを追加・削除したときもこのスクリプトを叩き直せばよい。
# ----------------------------------------------------------------------------
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"   # <repo>/DirectX

PRESET="macos-xcode"
OPEN_PROJECT=1
for arg in "$@"; do
	case "$arg" in
		metal)     PRESET="macos-xcode-metal" ;;
		vulkan)    PRESET="macos-xcode" ;;
		--no-open) OPEN_PROJECT=0 ;;
		*) echo "不明な引数: $arg(metal | vulkan | --no-open)" >&2; exit 1 ;;
	esac
done

# ビルド環境(cmake / ninja の PATH と VULKAN_SDK)。この Mac は ~/.local に手置きしてある。
# Vulkan SDK の setup-env.sh は未定義変数を参照するので set -u は使わない。
ENV_SCRIPT="$HOME/.local/aq-mac-env.sh"
if [[ -f "$ENV_SCRIPT" ]]; then
	source "$ENV_SCRIPT" >/dev/null
fi
if [[ -z "${VULKAN_SDK:-}" ]]; then
	echo "VULKAN_SDK が設定されていません。$ENV_SCRIPT を用意するか、export してから実行してください。" >&2
	exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake が見つかりません(README §5.1)。" >&2
	exit 1
fi

cd "$ROOT"
cmake --preset "$PRESET"

PROJECT="$ROOT/build/$PRESET/AquaDash.xcodeproj"
if [[ $OPEN_PROJECT -eq 1 ]]; then
	open "$PROJECT"
fi
echo "OK: $PROJECT"
