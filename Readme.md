# Kuper Printer / ChisPrinter

## 项目简介

Kuper Printer 是 Raspberry Pi Pico 2 W 的 Game Boy Printer 多功能扩展板。

## English

Kuper Printer is a multifunctional Game Boy Printer expansion board for Raspberry Pi Pico 2 W.

## 日本語

Kuper Printer は Raspberry Pi Pico 2 W 用の Game Boy Printer 多機能拡張ボードです。

## 特别声明

Kuper Printer is also developed and distributed under the name ChisPrinter.

## microSD 启动配置

克隆仓库后运行 `git submodule update --init`，以获取固定版本的
[`pico_fatfs`](https://github.com/elehobica/pico_fatfs)。

普通固件上电约 1 秒后读取低电平有效的 `SD_CARD_DETECT_N`，再尝试挂载
`pico_fatfs` 支持的 FAT 文件系统，并检查卡根目录中的空标记文件 `.kproot`。
卡不存在、挂载失败或标记文件不存在时，屏幕固定显示 `SD CARD ERROR`。
此检查只在启动时执行一次；`KP_UNIT_TEST_LCD_0` 构建跳过它。

卡检查通过后，固件读取根目录的 `config.toml`。格式见
[`config.example.toml`](config.example.toml)。当前只使用 `[display]` 的
`palette`（red/yellow/green/cyan/blue/magenta/white）和
`backlight_percent`（0 到 100 的整数）。配置文件缺失、过大、语法错误、
格式版本不符或 display 值无效时，两项都使用代码默认值：white、50%。
`[ui]` 尚未由固件读取。左/右键仍按 10%、25%、50%、75%、100% 的预设档位调整背光。
