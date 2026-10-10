# 摄像头实测键位映射

2026-10-10，USB 摄像头 `/dev/video2`，对照 `assets/keyboard.jpg`。实测 CTRL0–7 为高电平、其余 CTRL 高阻，灰度 1024，GCLK 请求 100 kHz；逐项采集全部 384 个原始地址，通过 SWD 读取 `keyboard_scan_address` 核对照片地址。检查后恢复 `build/test-c-brightness.elf`。

## 控制方法

表格的行号为灯位编号 n，列为 CTRL 编号。选定 CTRL 后，该灯位的红、绿、蓝原始通道分别为 **n、n+16、n+32**。三通道的位置已逐项对照。共覆盖 88 个独立按键、109 个灯位；19 个灯位的三个通道均未检测到亮灯，不能据此断言没有连接。

宽键上的多个灯位分别可控：表里重复的 Space、Tab、CapsLock、Shift、Ctrl、Enter、Backspace、PrtSc 都属于同一物理按键。要让整个宽键均匀发光，应同时设置该键的全部灯位；跨 CTRL 的灯位需轮流选通，不能把不同 CTRL 的灰度帧混用。

| n | CTRL0 | CTRL1 | CTRL2 | CTRL3 | CTRL4 | CTRL5 | CTRL6 | CTRL7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | C | — | Space | Space | Space | — | — | 音量减 |
| 1 | Fn右 | — | Space | Windows | — | Home | PgUp | — |
| 2 | Q | — | Fn左 | Tab | ` ~ | 1 ! | — | A |
| 3 | Alt右 | Alt左 | Tab | Tab | CapsLock | CapsLock | 音量加 | 麦克风静音 |
| 4 | — | — | E | F4 | CapsLock | 3 # | F3 | PgDn |
| 5 | X | Z | F2 | F1 | S | 2 @ | W | Esc |
| 6 | V | B | G | T | 5 % | 4 $ | R | F |
| 7 | M | N | H | Y | 6 ^ | 7 & | U | J |
| 8 | . > | ↓ | \ &#124; | O | F10 | 9 ( | F11 | L |
| 9 | Shift右 | Shift左 | Shift左 | Shift左 | Ctrl右 | — | — | ROG |
| 10 | , < | Shift右 | F6 | F7 | F5 | 8 * | I | K |
| 11 | — | — | Shift右 | F9 | — | — | ← | PrtSc |
| 12 | Ctrl右 | Ctrl左 | Ctrl右 | Enter | — | Ctrl左 | Ctrl左 | PrtSc |
| 13 | / ? | ↑ | - _ | F12 | 0 ) | P | [ { | ; : |
| 14 | ' &quot; | Enter | Enter | End | = + | Backspace | ] } | D |
| 15 | — | F8 | → | — | Delete/Insert | Backspace | Backspace | PrtSc |

“—”表示本次未检测到亮灯。CTRL3/n2、CTRL3/n3、CTRL4/n0、CTRL6/n15、CTRL7/n15 的绿光较弱，自动阈值未检出，但局部放大后可见对应绿光。颜色判别通过单通道画面完成；摄像头亮度受曝光、PWM 和光晕影响，不用于比较 LED 的线性亮度。

例如，只控制 C 键：

```c
keyboard_rgb_emergency_off();
keyboard_rgb_set_channel(0, 1024);   /* red */
keyboard_rgb_set_channel(16, 1024);  /* green */
keyboard_rgb_set_channel(32, 1024);  /* blue */
keyboard_rgb_apply();
keyboard_rgb_ctrl_only(0, 1);        /* other CTRL pins high impedance */
keyboard_rgb_gclk_start(100000);
```

关闭时调用 `keyboard_rgb_emergency_off()`。此表只验证单个 CTRL 高电平选通；没有验证多个 CTRL 同时选通或低电平扫描。后续 `breathe` 测试已验证轮流选通八个 CTRL 的全键盘统一颜色呼吸，详见项目根目录 README.md；逐键不同颜色的刷新尚未验证。

## C 键当前效果和证据

C 键亮度循环期间只观察到 C 发光，画面偏粉白；亮度逐档升降，亮时出现光晕，没有观察到全灭。源码 `c-brightness` 模式三通道同步设置 128→256→512→1024→2048→4096→8192→4096→2048→1024→512→256，每档约 0.5 秒，名义周期约 6 秒。

本地分析 240 帧 C 键画面，并为扫描地址各保留一帧；模型只查看布局、汇总图及少数局部裁剪以节省 token。完整记录在 `build/camera-check/`：`keymap.csv`、`keymap.json` 为映射；`observations.json` 为亮点坐标；`scan-NNN.png` 为原始帧，NNN=CTRL×48+channel；`keys.jpg` 为红光灯位汇总；`layout.jpg` 为多帧最大值合成布局；`colors.jpg` 为 C 的三个颜色；`weak-green.jpg` 为较弱的绿光。构建目录中的文件不受版本控制。
