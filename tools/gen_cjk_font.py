"""生成 LVGL 中文字体（GB2312 一级字库 3755 字，12px，bpp4，不压缩）。

用途：给 ui_kit 的四个 Montserrat 字号挂 fallback，解决
"glyph dsc. not found for U+xxxx" 的方框（中文文件名/状态文字）。

为什么是 GB2312 一级字库而不是 -r 0x4E00-0x9FA5：
  全区间 20902 字，12px bpp4 约 1.4 MB，flash 装不下也没有必要；
  一级字库 3755 个常用字覆盖日常文件名的 99% 以上，
  生僻字仍会走 LVGL 的空缺字形逻辑（画方框 + 一条 warn），不会崩。

用法：python tools/gen_cjk_font.py
输出：lib/lvgl/src/ui_test/fonts/ui_font_cjk12.c
"""

import os
import subprocess
import sys

OUT = os.path.join("lib", "lvgl", "src", "ui_test", "fonts", "ui_font_cjk12.c")
FONT = r"C:\Windows\Fonts\simhei.ttf"      # 黑体，纯 TTF（.ttc 集 lv_font_conv 不稳）
NODE_NAIL = r"E:\t\npx.cmd"                # 本机 npx 实际路径（PATH 里也有，但不依赖它）

# GB2312 一级汉字：区 0xB0A1 .. 0xD7F9，按字节枚举解码即得 3755 字
chars = []
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        try:
            ch = bytes([hi, lo]).decode("gb2312")
        except UnicodeDecodeError:
            continue
        chars.append(ch)
chars = sorted(set(chars))
symbols = "".join(chars)
print("GB2312 level-1 chars:", len(chars))

npx = NODE_NAIL if os.path.exists(NODE_NAIL) else "npx"
args = [
    npx, "--yes", "lv_font_conv@1.5.3",
    "--size", "12",
    "--bpp", "4",
    "--format", "lvgl",
    "--font", FONT,
    "--symbols", symbols,
    "-r", "0x3000-0x303F",      # CJK 标点
    "-r", "0xFF01-0xFF5E",      # 全角 ASCII
    "--no-compress",            # LV_USE_FONT_COMPRESSED = 0
    "--no-prefilter",
    "--force-fast-kern-format",
    "--lv-include", "lvgl.h",
    "--lv-font-name", "ui_font_cjk12",
    "-o", OUT,
]
print("running lv_font_conv ...")
r = subprocess.run(args, shell=False)
if r.returncode != 0:
    print("FAILED, exit", r.returncode)
    sys.exit(r.returncode)
print("ok ->", OUT, os.path.getsize(OUT), "bytes")