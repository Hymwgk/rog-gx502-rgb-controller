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

## 灯光电路噪声对比（2026-10-10）

实物听感：全灭与单通道测试安静；固定一组多灯、不扫描或重复锁存仍有噪声，说明动画及扫描不是发声的必要条件。固定负载下提高 GCLK 至 2、4、8 MHz 时，用户反馈声音逐步减轻。全键盘动画在 8 MHz 时明显变暗，不能把声音减小全部归因于频率本身；2/4 MHz 的自动 A/B 对比中，用户确认 4 MHz 亮度基本相同、音量减小且音调降低。这些是主观对比，尚无声谱、光强或供电波形测量，也未定位具体发声元件。

选用独立的 [diagonal-color-flow-rby-4mhz.json](configs/effects/diagonal-color-flow-rby-4mhz.json) 保存折中版本，颜色、灰度、混色与动画速度沿用原红蓝黄配置，仍有残余噪声。4 MHz 时每组选通两个完整的 1024 GCLK 子周期，保持原先约 512 μs 的显示和准备预算，而非把选通时间缩短到 256 μs。原 2 MHz 配置及命名固件保留。

```bash
./test-keyboard.py --profile diagonal-color-flow-rby-4mhz --flash
# 恢复原 2 MHz 固件
./flash.sh build/effect-diagonal-color-flow-rby.elf
```

诊断固件通过 `KB_TEST_FREEZE_ANIMATION` 冻结像素，`KB_TEST_BANK_SUBCYCLES` 指定完整子周期数（0 为自动），`KB_TEST_WAVE_GCLK_HZ` 覆盖诊断时钟。`KB_TEST_NOISE_AB=1..4` 分别比较等扫描速度的 1/2 MHz、2 MHz 的快/慢扫描、原扫描速度的 2/8 MHz、原扫描速度的 2/4 MHz；A 播放 5 秒、B 播放 2 秒。`KB_TEST_HOLD_BANK=1` 固定组 0 亮灯 60 秒后关闭，值 2/3/4 分别在固定组上循环比较 1/2、2/4、4/8 MHz。这些诊断宏默认关闭；普通逐键配置支持 1、2、4 MHz，8 MHz 仅用于诊断。


## PB12～PB15 实体按键

四个开关分别连接 **PB12/PB13/PB14/PB15 与 GND**，GPIO 配置为输入、内部上拉，松开为高电平、按下为低电平，不需要外接上拉电阻。所有固件入口都会初始化这四个输入；以下交互在 `wave` / `diagonal-wave` 逐键灯效中启用。

| 引脚 | 按下一次的动作 |
| --- | --- |
| PB12 | 上一个模式，循环切换 |
| PB13 | 下一个模式，循环切换 |
| PB14 | 亮度增加一档 |
| PB15 | 亮度降低一档 |

PB12/PB13 循环切换以下十种模式；旧模式的编号保持不变，升级可沿用已有 EEPROM 记录：

| 编号 | 模式 | 时间参数 |
| --- | --- | --- |
| 0 | 左下→右上斜向流光 | 原配置不变 |
| 1 | 右上→左下斜向流光 | 原配置不变 |
| 2 | 全键盘同步变色（沿用原配置混色曲线） | 原配置不变 |
| 3 | 全白常亮 | 固定颜色 |
| 4 | 全红常亮 | 固定颜色 |
| 5 | 全绿常亮 | 固定颜色 |
| 6 | 全蓝常亮 | 固定颜色 |
| 7 | 全键盘多色呼吸切换（整块键盘同步柔和换色） | 每次换色为原配置 `interval_ms` |
| 8 | 三轨流星：亮头、彩色长尾，错开斜穿键盘 | 当前约 8 秒横穿，每 3.6 秒错开发射 |
| 9 | 随机起点涟漪：从随机灯位向四周扩散 | 当前每圈 3.6 秒，消退后更换起点 |

整体多色呼吸使用配置中的颜色顺序，通过平滑曲线在相邻颜色之间渐变。当前配置为红→蓝→黄，每次换色 3600 ms，完整循环 10800 ms；斜向流光仍保留 8000 ms 横穿时间。换色使用连续的 RGB 能量归一化，避免两种颜色交叉时突然切换基准通道；混色与黄色可能比初版的“最强通道拉满”柔和一些。每次完整扫描共用同一个颜色样本，亮度仍由四档独立调整。全白、红、绿、蓝模式保持常亮；所有新模式只点亮实测灯位，未映射的电气地址保持零。

亮度只有 **关闭、约 1/3、约 2/3、最亮，共四档**；PB14 依次加亮，最亮之后循环到关闭；PB15 反向循环，关闭之后循环到最亮。最亮沿用配置原始峰值，输出灰度仍向下量化到 64 的倍数。

每次有效按键改变模式或亮度后，会在该次换组间隙立即写入内部数据 EEPROM，开机在点亮前恢复最近一条完整记录；关闭档也会恢复，不会先闪亮。没有有效记录的首次启动默认正向流光、最亮。断电恰好发生在写入中时，恢复上一条完整保存的设置。同一对按键同时按下不执行动作，消抖时间 20 ms，长按只触发一次，松开稳定 20 ms 后才可再次触发。

```bash
./test-keyboard.py --profile diagonal-color-flow-rby-4mhz --flash
```

按键每个完整扫描周期轮询一次，消抖不使用中断或阻塞延时；冻结动画诊断仍使用真实经过时间消抖。保留完整 S-PWM 子周期和原配置峰值上限。`keyboard_wave_controls` 可由 SWD 查看模式、四档亮度编号（0～3）和消抖状态；`keyboard_settings_save_errors` 记录保存失败次数。

STM32L051C8 的专用数据 EEPROM 前 128 字节（`0x08080000`～`0x0808007f`）用于 16 槽轮换记录，每条包含格式标识、序号、模式/亮度及最后提交的反码校验；仅设置变化时写入，不持续写入、不擦除程序 Flash。写入会短暂阻塞 Flash 取指，因此在所有灯组选通关闭、GCLK 停止时保存，完成后继续刷新，避免某一组灯在保存期间持续选通。ST 的 [AN4808](https://www.st.com/resource/en/application_note/an4808-writing-to-nonvolatile-memory-without-disrupting-code-execution-on-microcontrollers-of-the-stm32l0-and-stm32l1-series-stmicroelectronics.pdf) 说明了 EEPROM 写入对 Flash 执行的影响。

`tests/keyboard_controls_test.c` 是独立 C 行为测试，覆盖固定白/红/绿/蓝通道、整体换色周期/连续性/能量与单调性、全部模式双向循环与保存，以及消抖、长按、模式回绕、四档双向循环、双键互斥、灰度量化、毫秒计数回绕，另使用 RAM 模拟 EEPROM 的空记录、损坏记录、写入中断、重复设置不写入、槽位和序号回绕。`tests/keyboard_settings_device_test.c` 专门测试真实 EEPROM 写入、回读及槽位覆写，会修改保存的设置。测试成功时 `keyboard_controls_test_result` 为 `0x600DCAFE`，失败时为断言行号，可在没有原生 C 编译器时编译为 STM32 测试固件并通过 SWD 读取。

2026-10-29 首版（16 档、仅 RAM 保存）实机验证记录：独立按键行为测试返回 `0x600DCAFE`；GPIOB 的 MODER 高 8 位为 0、PUPDR 高 8 位为 `0x55`，四个开关松开时 IDR 的 PB12～PB15 均为 1。4 MHz 配置下，正向、反向与同步模式各连续测试 12 秒（反向与同步另测试半亮度），准备超时均为 0，最大准备耗时分别为 15882、15830、15904 个 CPU 周期，均低于 16384 周期的 512 μs 预算。实体开关按压手感与灯效观感仍需操作实物确认。

烧录前的完整 64 KiB Flash 备份保存在 `build/before-pb12-pb15.bin`（构建目录，不纳入版本控制）。


四档与掉电保存版本验证（2026-10-29）：按键/记录容错测试及真实 EEPROM 轮换写入测试均返回 `0x600DCAFE`。在正式固件中以 GPIO 输出低电平模拟按键接地，验证 PB14 的 3→0→1→2→3、PB15 的 3→2→1→0→3，以及 PB12/PB13 模式切换；每次模拟后均恢复上拉输入。已分别验证关闭档和同步模式经 MCU 复位后恢复，保存失败计数为 0。最终恢复并保存正向模式、最亮档。三种模式各连续运行 12 秒，最大准备时间分别为 16039、15977、16072 个 CPU 周期，准备超时均为 0。尚未通过实际切断外部供电执行掉电重启测试。

升级前 Flash 和 EEPROM 分别备份在 `build/before-four-levels-flash.bin`、`build/before-four-levels-eeprom.bin`。


八模式版本验证（2026-10-29）：固定白/红/绿/蓝输出、多色呼吸的红→蓝→黄顺序、3600 ms 换色和 10800 ms 循环、渐变连续性及最强通道峰值、八模式双向循环和记录保存测试均通过，板上测试结果 `0x600DCAFE`。正式固件检查四种常亮模式的 RGB 帧数据符合预期；八种模式的最大准备周期依次为 16053、15991、16086、14530、14527、14524、14530、15460，均低于 16384，超时计数均为 0，四种动态模式各覆盖 12 秒。渲染路径隔离且亮度缩放强制内联，避免编译器按体积优化而增加每像素函数调用。

升级前固件、EEPROM、RAM 设置分别备份在 `build/before-more-modes-flash.bin`、`build/before-more-modes-eeprom.bin`、`build/before-more-modes-controls.bin`。


整体换色跳动修复验证（2026-10-29）：将最大通道归一化替换为连续的 RGB 能量归一化，Q12 插值保留完整多项式后再取整，避免中间舍入引起反向小步；八个扫描组共用每帧一次取样的颜色。保留红→蓝→黄、每次换色 3600 ms 和原 EEPROM 编号。回归测试覆盖三段颜色单调性、换色中点折角、整周期及实际动画时间回绕，板上结果 `0x600DCAFE`。四档亮度各运行完整 12 秒，最高准备时间 15776 个 CPU 周期，超时均为 0；原三模式复查也无超时。升级前 EEPROM 与固件备份为 `build/before-smooth-color-eeprom.bin`、`build/before-smooth-color-flash.bin`，验证结束恢复用户原全白常亮、最亮档。视觉改善尚需用户实物观察确认。


流星与随机涟漪使用真实灯位二维坐标，按同一尺度缩放两个轴以保持键盘长宽比例。流星在三条相邻斜向轨迹上交错飞行，颜色沿用红/蓝/黄，头部带偏白柔光，尾部平滑衰减；每个灯位按距轨迹的距离软化亮度。涟漪每轮从 109 个已映射灯位中伪随机选一个起点，避免连续重复；亮环扩散到键盘远端并完全消退后再换起点，不会在亮着时突然瞬移。随机序列由运行计时扰动初始化，不依赖额外传感器或真正的硬件随机数发生器。

`tools/scripts/spatial_effects.py` 在电脑端预计算二维距离、轨迹归属与柔光/拖尾表，`firmware/src/effects/keyboard_spatial.c` 在 MCU 上查表播放。新效果使用独立经过时间，避免原有三色动画的时间回绕让随机起点只重复三个位置。模式编号追加为 8 和 9，四档亮度、PB12/PB13 切换及 EEPROM 保存规则沿用原实现。生成几何和光型的测试位于 `tests/test_spatial_effects.py`；随机源点、向外扩散、范围限制、亮度量化和计时回绕的板上测试位于 `tests/keyboard_spatial_test.c`。

加入空间特效前的备份为 `build/before-spatial-flash.bin`、`build/before-spatial-eeprom.bin`，升级前模式/亮度快照为 `build/before-spatial-controls.bin`。


空间特效验证（2026-10-29）：几何/光型 Python 测试全部通过，新增空间行为测试和十模式按键/记忆测试均在 STM32 上返回 `0x600DCAFE`。实际播放流星 12 秒、涟漪 4 圈，随机起点持续变化且不连续重复；最高准备时间分别为 15918、15878 个 CPU 周期，均低于 16384 周期预算，超时为 0。空间渲染合并为整组内联处理，避免逐灯函数调用超时。GPIO 模拟接地按键验证了 0→9（涟漪）→8（流星）切换，两种新模式经复位均能恢复；最后恢复并保存模式 0、最亮档。最终固件约 36 KiB，静态数据及 BSS 共 268 字节；保留原四档亮度和设置格式。实物观感仍以用户操作观察为准。

当前从模式 0 开始，PB12 按一次进入随机涟漪，再按一次进入流星；PB13 按相反方向循环全部十种模式。
