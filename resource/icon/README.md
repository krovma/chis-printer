# UI 图标资源

此目录保存根据 `refdocs/UI/图标清单与制作规范.md` 制作的初始图标、清单和固件资源生成器。

- 文件名：`icon_<id>_<size>.png`，例如 `icon_printer_32.png`。
- 当前 PNG 是第一版透明 RGBA 图标；后续调整图形时保持 manifest 中的文件名和尺寸不变。
- `<size>` 是正方形外框边长，所有尺寸均为 16px 的倍数。
- 语义 ID、显示名称、组合关系和制作规范以 `refdocs/UI/图标清单与制作规范.md` 为准。
- `manifest.json` 是输入索引；每条记录包含 `id`、`size` 和相对 `path`。

## 初始图标生成

先安装本目录固定的 Python 依赖，再使用项目 Python 执行：

```text
python -m pip install -r resource/icon/requirements.txt
python resource/icon/generate_icon_pngs.py
```

脚本读取 manifest 中的 24 个语义 ID、33 个尺寸变体，以 24 单位内部坐标绘制浅灰单色轮廓，输出透明 PNG，并将每条记录的 `placeholder` 标记为 `false`。默认前景值为 `238`，与 Gray4 的 `0xE` 对应；可通过 `--foreground` 选择其他中性灰值。

## 生成 C++ 资源

先安装本目录固定的 Python 依赖，再使用项目 Python 执行：

```text
python -m pip install -r resource/icon/requirements.txt
python resource/icon/generate_icon_header.py
```

默认输出 `generated_icons.h`。也可以通过 `--manifest`、`--output` 和 `--alpha-threshold` 覆盖输入、输出和透明度阈值。生成器会校验图片尺寸、路径不能越出 manifest 目录，并拒绝奇数宽度图像。

正式图标应继续遵守 16×16 外部网格、Gray4 灰度和 24 单位内部坐标规范。

## 固件字节格式

- 图像数据为行优先 4bpp；每两个像素一个字节，第一个像素放高 4 bit，第二个像素放低 4 bit。`row_stride` 是每行字节数，即 `width / 2`。
- 透明掩码为行优先 1bpp，按像素连续打包；每个字节从 bit7 到 bit0 使用，`1` 表示该像素可绘制，`0` 表示透明。掩码行步长为 `(width + 7) / 8`。
- alpha 大于等于默认阈值 `128` 的像素写入掩码；低于阈值的像素图像 nibble 固定为 `0`，掩码 bit 固定为 `0`。
- 可绘制像素先按整数 BT.709 近似灰度转换，再量化为 Gray4 的 `0x0..0xF`。

## C++ 接口

生成头文件位于命名空间 `kuper::icons`，定义 `IconBitmap4bpp` 和 `IconMask1bpp`，两者都提供 `width`、`height`、`row_stride`、`bytes` 和 `byte_count`。

同一 `id` 的多尺寸资源使用并行变体表：

```cpp
const auto& pixels = kuper::icons::ICON_PRINTER[0];
const auto& mask = kuper::icons::ICON_PRINTER_MASK[0];
```

`ICON_<ID>_COUNT` 和 `ICON_<ID>_MASK_COUNT` 给出表项数量。表项顺序与 `manifest.json` 中相同 ID 的出现顺序一致；原始数组使用内部名称 `ICON_<ID>_<WIDTH>_DATA` 和 `ICON_<ID>_<WIDTH>_MASK_DATA`。

