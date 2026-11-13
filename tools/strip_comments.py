"""删除自有代码里的冗余注释，保留引脚/接线标注。

只处理"自己的代码"：
    src/*.c *.cpp *.h
    lib/lvgl/src/ui_test/**（排除 fonts/ 生成文件）
不动第三方库（LVGL 内核、TFT_eSPI、FT6336、TJpg_Decoder）—— 那是别人的代码。

规则：
  1) 整行注释（// 或 /* ... */ 块内的行）删除；
  2) 但含引脚/电气关键词的行保留（引脚标注必须留）；
  3) 代码行尾的 // 注释不删 —— 那里的引脚标注最密集，删了损失最大；
  4) 连续空行压成一行。
"""

import os
import re

KEEP = ("引脚", "接线", "界面接线", "GPIO", "VCC", "GND", "3.3V", "3V3",
        "天线", "电压", "电源", "CSN", "SCK", "MISO", "MOSI", "SDA", "SCL",
        "PIN_", "_PIN", "TFT_", "SD_CS", "I2C", "SPI 总线")

ROOTS = ["src", os.path.join("lib", "lvgl", "src", "ui_test")]
SKIP_DIRS = {os.path.join("lib", "lvgl", "src", "ui_test", "fonts")}
EXTS = (".c", ".cpp", ".h")


def keep_line(line: str) -> bool:
    return any(k in line for k in KEEP)


def strip_file(path: str):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")

    out = []
    in_block = False
    removed = 0

    for raw in lines:
        s = raw.strip()

        if in_block:
            if "*/" in s:
                in_block = False
                tail = raw.split("*/", 1)[1]
                if tail.strip():          # /* ... */ 后面还跟着代码，保留代码部分
                    out.append(tail.rstrip())
                    continue
            if keep_line(raw):
                out.append(raw)
            else:
                removed += 1
            continue

        # 整行注释起始
        if s.startswith("//"):
            if keep_line(raw):
                out.append(raw)
            else:
                removed += 1
            continue

        if s.startswith("/*"):
            if "*/" not in s:
                in_block = True
            if keep_line(raw):
                out.append(raw)
            else:
                removed += 1
            continue

        # /* 开头的装饰行（如 " * xxx"）
        if s.startswith("*") and not s.startswith("*/"):
            if keep_line(raw):
                out.append(raw)
            else:
                removed += 1
            continue

        out.append(raw)

    # 连续空行压成一行
    squeezed = []
    for ln in out:
        if ln.strip() == "" and squeezed and squeezed[-1].strip() == "":
            continue
        squeezed.append(ln)

    text = "\n".join(squeezed)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    return removed


total = 0
for root in ROOTS:
    for dirpath, dirnames, filenames in os.walk(root):
        if dirpath in SKIP_DIRS:
            continue
        dirnames[:] = [d for d in dirnames if os.path.join(dirpath, d) not in SKIP_DIRS]
        for fn in filenames:
            if not fn.endswith(EXTS):
                continue
            p = os.path.join(dirpath, fn)
            n = strip_file(p)
            if n:
                print("%-58s -%d 行注释" % (p.replace("\\", "/"), n))
            total += n

print("---- 共删除 %d 行注释 ----" % total)