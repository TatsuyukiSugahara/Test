# Claude Code のプロジェクトメモリ(写し)

Claude Code が `~/.claude/projects/<リポジトリのパス>/memory/` に持つメモの写し。
別 PC で同じ状態から作業を始めるための持ち運び用で、**正本は各 PC のローカル側**。

## 別 PC で使うとき

```
# <path> は、そちらの PC でこのリポジトリを置いた絶対パスを
#   / を - に置き換えたもの(例: /Users/foo/Git/Test → -Users-foo-Git-Test)
mkdir -p ~/.claude/projects/<path>/memory
cp .claude/memory/*.md ~/.claude/projects/<path>/memory/
rm ~/.claude/projects/<path>/memory/README.md
```

## 更新するとき

作業した PC のローカル側が更新されるので、区切りでここへ写し直してコミットする。

```
cp ~/.claude/projects/<path>/memory/*.md .claude/memory/
```

`MEMORY.md` が索引で、各ファイルの 1 行目からの frontmatter(`name` / `description` / `type`)が
Claude Code の読み込みに使われる。手で編集してもよい。
