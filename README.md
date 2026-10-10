# STM32L051C8 键盘 RGB 底层驱动

面向 ASUS ROG GX502 逐键 RGB 键盘的硬件 bring-up 工程。`mbia045.c` 实现 MBIA045 串行灰度帧和 LE 命令；`board.c` 集中管理 STM32L051C8 引脚和 TIM22 的 GCLK 硬件输出；`keyboard_rgb.c` 提供原始 CTRL 和通道测试接口。当前固件上电后保持 CTRL 高阻、GCLK 停止、灰度数据为零，不自动点亮键盘，也不预设 CTRL 极性或按键映射。接口与待确认项目见 [KEYBOARD.md](docs/KEYBOARD.md)。

## 接线

ST-LINK/V2 的 SWDIO、SWCLK、GND 分别接目标板对应引脚。MCU 需稳定的 3.3 V 供电；键盘背光还需按原理图提供 KB_3V3 和 KB_5V，所有电源共地。不要把 5 V 接到 MCU 的 3.3 V 引脚。

## 测试接口

`keyboard_rgb_probe(ctrl, level, channel, brightness, gclk_hz)` 一次只写一个原始通道并驱动指定 CTRL 引脚为明确的电平，适合逐步确认电气连接。`level` 是实际 GPIO 电平，含义尚未定义为“开”。`brightness` 上限暂设 `0x4000`，建议最初用较低值，例如 `0x0100`。`keyboard_rgb_emergency_off()` 立即释放 CTRL、停止 GCLK 并送全零帧。当前没有接串口，主程序只初始化并等待；测试接口可在后续接入串口或调试器命令。

PA5/PA7 分别支持 SPI1_SCK/MOSI，PA6 支持 TIM22_CH1（AF5）。为了让 LE 在指定的 DCLK 上升沿保持高电平，首版串行传输由 GPIO 精确生成；GCLK 已由 TIM22 硬件 PWM 生成，目前支持请求 100 kHz、500 kHz、1 MHz，并返回实际频率。使用 MCU 复位后的约 2.097 MHz MSI 时钟，因此频率有量化和振荡器误差。

## Ubuntu 构建与烧录

首次安装依赖（需要 sudo 权限）：

```bash
./setup-ubuntu.sh
```

本次迁移已在 `tools/linux/` 放入 Ubuntu 24.04 amd64 的 ARM GCC 13.2、binutils 及 newlib 头文件，无需 sudo 即可运行 `./build.sh`。脚本优先使用 PATH 中的系统工具链，未安装时使用该项目内工具链。其他架构或 Ubuntu 版本请运行上述安装脚本使用本机软件包。

使用 Ubuntu 的 ARM GCC 和 OpenOCD。安装会重新加载 OpenOCD 的 udev 规则，完成后重新插拔 ST-LINK。若提示 USB 权限不足，检查 `/usr/lib/udev/rules.d/60-openocd.rules` 或 `/lib/udev/rules.d/60-openocd.rules`，以及当前用户的 `plugdev` 组成员资格；需要时运行 `sudo usermod -aG plugdev "$USER"`，然后注销并重新登录。

```bash
./build.sh                  # 默认安全固件
./flash.sh                  # 烧录 build/demo.elf，并读回校验和复位
```

ST-LINK/V2 烧录已在本机验证：使用 `stlink-dap.cfg` 原生 SWD 驱动和软件复位，无需连接 NRST。探头固件需支持原生 DAP（V2J24 或更新版本）。

产物为 `build/demo.elf`、`build/demo.bin` 和 `build/demo.map`。构建脚本可从任意目录调用，编译宏作为参数传入，例如 `./build.sh MBIA045_NUM_IC=3`。可用环境变量 `ARM_GCC`、`ARM_OBJCOPY`、`OPENOCD` 指定其他 Linux 工具的完整路径。`./flash.sh /path/to/firmware.elf` 可烧录指定文件，相对路径相对于当前工作目录。

### 键盘测试

```bash
./test-keyboard.py --mode two-key --brightness 1024 --gclk-hz 100000 --seconds 60
# 加 --flash 才会实际烧录
./test-keyboard.py --mode two-key --brightness 1024 --gclk-hz 100000 --seconds 60 --flash
# 恢复默认安全固件
./test-keyboard.py --mode safe --flash
```

测试构建同时保存 `build/test-模式名.elf`。`--help` 可查看全部参数。

摄像头实测的逐键控制地址见 [KEYMAP.md](docs/KEYMAP.md)。`scan` 模式提供只读观察用途的全局变量 `keyboard_scan_address`，值为 `CTRL * 48 + channel`，切换时为 `UINT32_MAX`；可由 SWD 读取，用于同步照片和扫描地址。

| 模式 | 行为 |
| --- | --- |
| safe | CTRL 高阻、GCLK 停止、全零灰度 |
| signals | 重复发送数据帧、间歇输出 GCLK，CTRL 保持高阻 |
| ctrl | 将 `--ctrl-index` 指定的 CTRL 驱动到 `--level`，灰度保持零 |
| channel | 测试指定 CTRL、电平和 `--channel` 原始通道 |
| rgb-c | C 键 0/16/32 三通道同时点亮 |
| two-key | C 键与空格键每秒交替点亮 |
| scan | 遍历 CTRL0–7 和通道 0–47，每组合 0.5 秒，持续循环 |
| c-brightness | C 键灰度 128–8192 逐档变化，持续循环 |
| breathe | 全键盘白→蓝→红→绿，每色暗→亮→暗，持续循环 |
| wave | 红→黄→蓝的斜向波峰，尾光变色并衰减，持续循环 |

signals、ctrl、channel、rgb-c、two-key 在 `--seconds` 指定时间后自动关闭；scan、c-brightness、breathe 和 wave 持续循环，需烧录 safe 模式停止。实测连接记录见 [KEYBOARD.md](docs/KEYBOARD.md)。

全键盘四色呼吸测试：

```bash
./test-keyboard.py --mode breathe --brightness 4096 --flash
```

`--brightness` 为峰值灰度。每种颜色约 6.2 秒，64 档平方曲线上升和下降，起止灰度为零；输出灰度向下量化为 64 的倍数，让 S-PWM 各子周期均匀重复。breathe 模式独立将 MSI 提升至 4.194 MHz，默认请求 GCLK 2 MHz，实际约 2.097 MHz；其他模式保留复位后的时钟。SysTick 中断持续轮流选通 CTRL0–7，每组恰好对应 1024 个 GCLK 周期，约 0.488 ms，全键盘名义刷新率约 256 Hz，同一时刻只有一组。

使用实测灯位表的并集共享单色帧，避免每次选组都重新发送 768 位数据；下一帧在当前画面显示期间发送，仅在最终全局锁存时短暂停止 GCLK。由于所有灯位颜色相同，此路径也会写入未检测到亮灯的地址；它用于全键盘统一颜色测试。每组约 1/8 占空比，实际亮度低于单键静态测试。breathe 可显式选择较低 GCLK，但扫描会随之变慢；峰值建议至少 64，否则量化后全暗。已修复的亮度高峰频闪问题与验证见 [KEYBOARD.md](docs/KEYBOARD.md#呼吸灯亮度高峰频闪修复)。

## 保存与复现灯效

当前已确认好看的四色效果保存在 [classic-breathe.json](configs/effects/classic-breathe.json)；[blue-breathe.json](configs/effects/blue-breathe.json) 是单蓝色示例。JSON 在电脑上校验并编译为 C 常量，STM32 无需文件系统或 JSON 解析器。每次选择一种配置构建固件；目前通过重新烧录切换保存的模式。

```bash
./test-keyboard.py --list-profiles
./test-keyboard.py --profile classic-breathe          # 只构建当前四色效果
./test-keyboard.py --profile classic-breathe --flash  # 构建、烧录、校验
./test-keyboard.py --profile blue-breathe --flash     # 选择单蓝色呼吸
```

每种效果保存 `build/effect-名字.elf` 和同名 `.json`，后者包含命令行覆盖后的实际参数。可用 `./flash.sh build/effect-classic-breathe.elf` 直接复现已构建的固件，也可用 `./test-keyboard.py --profile build/effect-classic-breathe.json --flash` 按实际配置重新构建。重新构建同名模式会替换同名快照；要保留不同版本，请给配置不同的名字或另存快照。跨代码版本要求完全相同的运行行为时，应保留对应 ELF，JSON 保存的是效果参数。

新增效果：复制一个 JSON，修改文件名与 `name`，再修改颜色顺序、`peak`、`steps_per_half`、`step_ms` 或 `curve`。每色名义时长约为 `(2 × steps_per_half + 1) × step_ms` 毫秒，实际时间按扫描周期取整；经典模式约 6.2 秒。`curve` 支持 `square`（柔和暗部）与 `linear`，颜色支持 white/blue/red/green/cyan/magenta/yellow。`gclk_hz` 建议保留 2000000，持续扫描、1024 GCLK 对齐及 64 灰度量化由播放引擎固定维护。新配色和节奏需要实际观察；单蓝色示例已通过构建，尚未烧录验证。

代码分工如下：

| 文件 | 职责 |
| --- | --- |
| `configs/effects/*.json` | 保存可命名、复制、比较的灯效参数 |
| `tools/scripts/effect_profiles.py` | 校验配置并生成 `build/effect_config.h` 和实际参数 JSON |
| `firmware/src/effects/` + `firmware/include/` | 呼吸曲线、颜色循环与持续扫描；保留频闪修复 |
| `firmware/src/keyboard/` / `drivers/` / `board/` | 键盘映射、芯片传输、GPIO 与时钟 |
| `firmware/src/app/main.c` / `tools/scripts/test-keyboard.py` | 选择测试入口、构建模式与烧录 |

现有 `--mode breathe` 默认选择 classic-breathe；`--brightness`、`--gclk-hz` 可临时覆盖配置，并记录在构建快照中。其他底层测试模式用法保留。配置版本为 1，支持全键盘呼吸与逐键斜向波浪；配置的 `effect` 自动选择相应固件模式。

## 项目目录与依赖

```text
.
├── firmware/
│   ├── src/
│   │   ├── app/main.c                 # 初始化和测试入口
│   │   ├── effects/                  # 呼吸播放、斜向波浪和查表渲染
│   │   ├── keyboard/keyboard_rgb.c    # 键盘灯位与 CTRL 操作
│   │   ├── drivers/mbia045.c          # 芯片协议、数据帧与锁存
│   │   └── board/board.c              # GPIO、引脚映射、时钟和 TIM22
│   ├── include/                      # 固件模块公共接口
│   ├── startup/                      # 向量表、复位入口
│   └── linker/                       # Flash/RAM 布局
├── configs/
│   ├── effects/                      # 可复现的灯效 JSON
│   └── keyboard/keymap.json          # 128 灯位与实测照片坐标
├── tools/
│   ├── scripts/                      # 构建、烧录、配置生成及环境安装
│   └── linux/                        # 本地 ARM 工具链，忽略版本控制
├── tests/                            # 配置和亮度曲线测试
├── docs/                             # 接线、实测映射、问题记录和原始资料
├── assets/keyboard.jpg               # 键盘布局参考图
├── build/                            # 固件、生成配置及摄像头证据，忽略版本控制
└── README.md
```

根目录的 `build.sh`、`flash.sh`、`test-keyboard.py`、`setup-ubuntu.sh` 是兼容入口，实际逻辑在 `tools/scripts/`；原有命令保持可用，也支持从其他工作目录调用。

依赖关系以 `main → keyboard_effect → keyboard_rgb → mbia045 → board` 为主。灯效模块还通过 board 接口选择已验证的 MSI 时钟；底层模块不依赖 JSON 或具体灯效。目录整理没有修改固件算法，经典呼吸模式在移动前后构建的 `.bin` 逐字节相同。后续扩展约定见 [ARCHITECTURE.md](docs/ARCHITECTURE.md)。

配置测试：`python3 -m unittest discover -s tests -v`。C 曲线测试需要本机 `cc`/`gcc`，不可用时会明确跳过；ARM 固件构建使用系统或项目内的交叉工具链。

## 左下到右上的三色波浪

```bash
./test-keyboard.py --profile diagonal-wave --flash
# 恢复已保存的四色呼吸
./test-keyboard.py --profile classic-breathe --flash
```

[diagonal-wave.json](configs/effects/diagonal-wave.json) 保存红、黄、蓝三种波峰颜色。当前第四版使用 `wave_curve: "smooth"`，峰值 6144、底部灰度参数 `floor: 256`；亮度以平滑曲线形成更窄的波峰与更深的波谷，颜色在相邻波峰之间持续混合，到波峰时保持单色。每 3600 ms 发出一道波，横穿时间为 9600 ms；整套节奏比第三版再慢 1.5 倍，保持空间波形。`travel_ms` 控制速度，`interval_ms` 控制波峰间距，`crest_ms` 指定抵达后的波峰时刻，`tail_ms` 控制平滑曲线中波峰到波谷的宽度（当前 1800 ms）。`floor` 是混色前的底部灰度，实际单个 RGB 通道会更低，且按 64 量化。间隔须为 8 ms 的倍数，`crest_ms` 必须小于间隔。前三版分别保存在 `diagonal-wave-v1.json`、`diagonal-wave-v2.json`、`diagonal-wave-v3.json`，未指定 `wave_curve` 时仍使用原尾光算法。

波浪使用 `configs/keyboard/keymap.json` 的实测位置生成每个灯位的到达时间，未检测到亮灯的地址保持零。构建时以 8 ms 分辨率生成三色渐变表，STM32 查表渲染各组的不同画面。每组选通期间先发送下一组数据，完成至少一个 1024 GCLK 子周期后才熄灭当前组、锁存下一组并切换，避免把整帧发送变成全键盘黑场。

已确认的慢速波浪另外固定保存为 [diagonal-wave-slow.json](configs/effects/diagonal-wave-slow.json)，恢复命令为 `./test-keyboard.py --profile diagonal-wave-slow --flash`。

[diagonal-color-flow.json](configs/effects/diagonal-color-flow.json) 是独立的逐键换色模式：横穿 8000 ms，红→黄→蓝，每 3600 ms 进入下一次颜色过渡。使用 `wave_curve: "color-flow-blended"`，峰值 3072、底部 2944，亮度起伏约为峰值灰度的 4.2%。当前修订版按周期三次 B 样条持续混合三种颜色，连原波峰也不回到纯色，再把最强 RGB 通道归一化，避免红蓝等颜色在混合中间因各通道被分摊而明显变暗；该比例描述驱动灰度，实际视觉亮度仍受颜色影响。运行 `./test-keyboard.py --profile diagonal-color-flow --flash` 即可复现；较明显换色的首版保存在 `diagonal-color-flow-v1.json`。

这是独立的逐键播放路径：CPU 使用 HSI16+PLL 的 32 MHz 时钟，电压 range 1、Flash 一等待周期，GCLK 请求 2 MHz，GPIO 串行数据使用加速循环，LE/DCLK/SDI/GCLK 设置快速边沿。呼吸模式继续使用原先的 MSI 与持续扫描路径。新版波浪已烧录、读回校验，并采集 180 帧；时序诊断在采集期间没有发现准备下一组数据超时。实现细节及证据见 [KEYBOARD.md](docs/KEYBOARD.md#三色斜向波浪测试)。是否完全还原原厂观感，仍以用户实物观察为准。
