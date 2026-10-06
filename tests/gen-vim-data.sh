#!/bin/sh
# Regenerates tests/motions_vim.inc, counts_vim.inc and blocks_vim.inc from the real Vim (vim -Nu NONE -es, in a scratch
# directory, never on a repo file). Needs vim on the PATH; changes nothing but the three .inc files.
# Usage: tests/gen-vim-data.sh        (from anywhere; then review the diff and run make test)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/gen.vim" <<'VIM'
set encoding=utf-8
" The buffers of tests/test_motions.c, in its order (\x escapes are bytes).
let bufs = [
\ ["foo bar  baz", "", "  qux.quux(a, b);", "  ", "end"],
\ ["h\xc3\xa9llo w\xc3\xb6rld \xc3\xbcn\xc3\xaf", "\xe2\x80\x94dash\xe2\x80\x94x", "\xe6\x97\xa5\xe6\x9c\xac \xe8\xaa\x9ea", "x\xc3\x97y \xc2\xa1z"],
\ ["a"],
\ [""],
\ ["", "", "x"],
\ ["foo.bar", "baz...", "a_b-c", "(  )"],
\ ["\tfoo\tbar", "x  "],
\ ["foo", "   "],
\ ["foo", ""],
\ ["  ", "  a"],
\ ["a b", "", "", "c d"],
\ ["\xe3\x81\xb2\xe3\x82\x89\xe3\x82\xab\xe3\x82\xbf\xe6\xbc\xa2\xe5\xad\x97\xed\x95\x9c\xea\xb8\x80ab", "a\xe2\x80\xa6b \xe2\x9f\xa8c\xe2\x9f\xa9 \xe2\x82\xacx \xc7\x86\xc5\xad"],
\ ["a\xc2\xa0b c", "\xc3\xa9\xe2\x80\x93b  ", "\x01a\x7fb"],
\ ]
" Every start of the motions table: each character of each line.
func Starts(b)
  let r = []
  for y in range(len(g:bufs[a:b]))
    let l = g:bufs[a:b][y]
    for x in range(len(l))
      if and(char2nr(l[x]), 0xc0) != 0x80 | call add(r, [y, x]) | endif
    endfor
    if l == '' | call add(r, [y, 0]) | endif
  endfor
  return r
endfunc
func Run(b, y, x, cmd, ...)
  silent %delete _
  call setline(1, g:bufs[a:b])
  call cursor(a:y + 1, a:x + 1)
  execute 'normal! ' . a:cmd
  if a:0 | execute 'normal! ' . a:1 | endif " a separate :normal!: a failing motion flushes the rest of its own
  return [line('.') - 1, col('.') - 1]
endfunc
" motions_vim.inc: the unit table.
let out = ['/* Generated from the real Vim 9.1 (vim -Nu NONE -es, one :normal! per key, from every character of every buffer; regenerate with gen-vim-data.sh):',
\ ' * {buffer, key, from line, from byte, to line, to byte}. Included by test_motions.c. */']
for b in range(len(bufs))
  for key in split('wbeWBE^0$', '\zs')
    let row = []
    for s in Starts(b)
      let t = Run(b, s[0], s[1], key)
      call add(row, printf("{%d,'%s',%d,%d,%d,%d}", b, key, s[0], s[1], t[0], t[1]))
    endfor
    call add(out, "\t" . join(row, ', ') . ',')
  endfor
endfor
call writefile(out, g:motions_out)
" counts_vim.inc: {buffer, keys, count (0 = none), from line, from byte, to line, to byte, line and byte after a following j}.
let out = ['/* Generated from the real Vim 9.1 by gen-vim-data.sh (vim -Nu NONE -es; one :normal! per count and key, then one for j):',
\ ' * {buffer, keys, count (0 = none), from line, from byte, to line, to byte, line and byte after a j that follows}. Included by test_motions.c. */']
" No wide (CJK) lines: notvim shows every character as one column, Vim shows them as two.
let starts = {0: [[0,0],[0,5],[0,11],[1,0],[2,3],[2,16],[3,1],[4,2]], 12: [[0,0],[0,3],[1,2],[2,1]], 5: [[0,0],[1,4],[2,2],[3,3]], 10: [[0,1],[1,0],[2,0],[3,2]]}
for b in sort(keys(starts), 'n')
  for key in ['h','j','k','l','w','b','e','W','B','E','0','^','$','G','gg']
    let row = []
    for n in [0, 2, 3, 100]
      if key == '0' && n | continue | endif " "30" is a count of 30, not a count before "0"
      for s in starts[b]
        let t = Run(b, s[0], s[1], (n ? n : '') . key)
        let u = Run(b, s[0], s[1], (n ? n : '') . key, 'j')
        call add(row, printf('{%d,"%s",%d,%d,%d,%d,%d,%d,%d}', b, key, n, s[0], s[1], t[0], t[1], u[0], u[1]))
      endfor
    endfor
    call add(out, "\t" . join(row, ', ') . ',')
  endfor
endfor
call writefile(out, g:counts_out)
" blocks_vim.inc: "{" "}" and "%": {buffer, key, count (0 = none), from line, from byte, to line, to byte, line and byte after a j that follows}.
let qbufs = [
\ ["a", "b", "", "c", "d", "", "", "e", "  ", "f"],
\ ["", "", "a"],
\ ["a", ""],
\ ["a"],
\ [""],
\ ["  x", "", "  y  z", " "],
\ ["f(a[1], {b})", "  if (x) {", "    y(z);", "  }", "end)"],
\ ["(a", "b", "c)"],
\ ["((a)", "b)", ")"],
\ ["x(h\xc3\xa9llo)y", "[\xc3\xa9]"],
\ ["no brackets", ""],
\ ["  ( )", "}{"],
\ ]
" Not here: Vim's "%" also skips brackets inside double quotes and pairs "/*" with "*/" and "#if" with "#endif"; notvim does not (a known gap).
func Starts2(b)
  let r = []
  for y in range(len(g:qbufs[a:b]))
    let l = g:qbufs[a:b][y]
    for x in range(len(l))
      if and(char2nr(l[x]), 0xc0) != 0x80 | call add(r, [y, x]) | endif
    endfor
    if l == '' | call add(r, [y, 0]) | endif
  endfor
  return r
endfunc
func Run2(b, y, x, cmd, ...)
  silent %delete _
  call setline(1, g:qbufs[a:b])
  call cursor(a:y + 1, a:x + 1)
  execute 'normal! ' . a:cmd
  if a:0 | execute 'normal! ' . a:1 | endif
  return [line('.') - 1, col('.') - 1]
endfunc
let out = ['/* Generated from the real Vim 9.1 by gen-vim-data.sh (vim -Nu NONE -es; one :normal! per count and key, then one for j):',
\ ' * {buffer, key, count (0 = none), from line, from byte, to line, to byte, line and byte after a j that follows}. Included by test_motions.c. */']
for b in range(len(qbufs))
  for key in ['{', '}', '%']
    let row = []
    for n in (key == '%' ? [0] : [0, 2, 3, 100])
      for s in Starts2(b)
        let t = Run2(b, s[0], s[1], (n ? n : '') . key)
        let u = Run2(b, s[0], s[1], (n ? n : '') . key, 'j')
        call add(row, printf('{%d,"%s",%d,%d,%d,%d,%d,%d,%d}', b, key, n, s[0], s[1], t[0], t[1], u[0], u[1]))
      endfor
    endfor
    call add(out, "\t" . join(row, ', ') . ',')
  endfor
endfor
call writefile(out, g:blocks_out)
qa!
VIM
vim -Nu NONE -es -c "let g:motions_out='$work/motions_vim.inc'" -c "let g:counts_out='$work/counts_vim.inc'" -c "let g:blocks_out='$work/blocks_vim.inc'" -S "$work/gen.vim" </dev/null >/dev/null 2>&1 || true
[ -s "$work/motions_vim.inc" ] && [ -s "$work/counts_vim.inc" ] && [ -s "$work/blocks_vim.inc" ] || { echo "vim produced no data" >&2; exit 1; }
cp "$work/motions_vim.inc" "$work/counts_vim.inc" "$work/blocks_vim.inc" "$here/"
