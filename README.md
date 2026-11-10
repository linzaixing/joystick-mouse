# Arcade Mouse — 街机摇杆蓝牙鼠标（ESP32）

把街头游戏机的摇杆和按钮，做成一只免驱的 BLE 无线鼠标。
主控 ESP32 通过蓝牙 HID（HOGP）直接连电脑蓝牙，**不需要任何接收器**，Windows / macOS / Linux / Android / iOS 通用。

```
摇杆推方向 ──┐
左键 / 右键按钮 ├─► ESP32（消抖 + 速度斜坡 + 正交解码） ─► BLE HID ─► 电脑通用 HID 驱动
滚轮编码器 ──┘
```

## 硬件清单

| 数量 | 器件 | 说明 |
|---|---|---|
| 1 | ESP32 开发板 | **ESP32-WROOM-32**（经典）/ **ESP32-C3 Super Mini** / ESP32-S3 等带 BLE 的型号；**ESP32-S2、P4 没有 BLE** |
| 1 | 街机摇杆 | 微动开关式（4 个方向微动，数字通断） |
| 2 | 街机按钮 | 左键 / 右键（Ø24 或 Ø30 卡扣式） |
| 1 | 滚轮模块 | 从普通 2.4G 无线鼠标上拆下的「编码器 + 中键微动」 |
| 1 | 精确模式按钮 | 可选，按住时光标降速到 35% |
| — | 导线、底板、防滑垫、M4 螺丝 | — |

## 接线表

所有开关一律 **COM → GND，信号脚 → GPIO**，使用芯片内部上拉，**免外接电阻**。

| 功能 | GPIO |
|---|---|
| 摇杆 上 UP | 25 |
| 摇杆 下 DOWN | 26 |
| 摇杆 左 LEFT | 27 |
| 摇杆 右 RIGHT | 14 |
| 按钮 左键 | 13 |
| 按钮 右键 | 4 |
| 滚轮 编码器 A 相 | 32 |
| 滚轮 编码器 B 相 | 33 |
| 滚轮 中键微动 | 16 |
| 按钮 精确模式（可选） | 18 |
| 灯环 WS2812 DIN（串 330Ω） | 23 |

### 滚轮模块接法（最易出错的一处）

本工程默认按 **机械式编码器**（无源 3 脚 A / 公共 C / B）落地：

- 公共脚 C → GND。接错成信号脚会让编码器一直读到 0，表现为「转了没反应」
- A → GPIO32，B → GPIO33
- 原上拉电阻丢弃，直接用 ESP32 内部上拉
- 中键微动两只脚：一脚 GPIO16、一脚 GND

**备选：若拆开后发现是光学式编码器**（引脚 ≥4 根且有 VCC / IR 供电脚）：

- 模块 VCC → ESP32 **3.3V**（光学式必须供电）
- OUT_A → GPIO32，OUT_B → GPIO33（不变）
- **固件一行都不用改**：内部上拉对集电极开路输出无害
- 供电高于 3.3V 的模块不可直连 GPIO

### 引脚约束

- 避开 GPIO6~11（连闪存）、GPIO34~39（无内部上拉）、启动绑带脚 GPIO0 / 2 / **5** / 12 / 15
- ESP32-WROVER（带 PSRAM）的 GPIO16/17 被 PSRAM 占用，中键改 GPIO19、精确模式键改 GPIO23
- 上电前用万用表确认滚轮模块信号脚静态高电平 ≤ 3.3V

### 换板子：固件按芯片自动切换引脚表

上表是**经典 ESP32（WROOM-32 等）**的接线。换芯片时固件里的板级条件编译会**自动切换**引脚表，
照上表硬接会拿到根本不存在的引脚。

**ESP32-C3 / ESP32-C3 Super Mini**（可用 GPIO 只有 0~10 与 18~21）：

| 功能 | C3 GPIO |
|---|---|
| 摇杆 上 / 下 / 左 / 右 | 3 / 4 / 5 / 6 |
| 按钮 左键 / 右键 | 7 / 10 |
| 滚轮 编码器 A 相 / B 相 | 20 / 21 |
| 滚轮 中键微动 | 0 |
| 按钮 精确模式（可选） | 1 |
| 灯环 WS2812 DIN（串 330Ω） | 2 |

- 这套表避开了 C3 的绑带脚 **8 / 9**（在 Super Mini 上就是板载 LED 与 BOOT 键，
  开机瞬间被拉低可能进下载模式）、片内 SPI flash 占用的 **11~17**、以及 USB 的 **18 / 19**。
- GPIO2 也是绑带脚，但只用来输出 WS2812 数据 —— DIN 是高阻输入，不会把 GPIO2 拉低，
  因此安全可用；它是 C3 上唯一剩下的空闲脚。
- ⚠️ **C3 必须用 `CDCOnBoot=cdc` 编译**：编码器占的 GPIO20/21 也正是 C3 默认 UART0 的 TX/RX，
  不把 `Serial` 改走 USB 就会和编码器抢脚。固件里加了 `#error` 兜底，选错了会直接编译报错提示。

**ESP32-S3**：

| 功能 | S3 GPIO |
|---|---|
| 摇杆 上 / 下 / 左 / 右 | 5 / 6 / 7 / 8 |
| 按钮 左键 / 右键 | 9 / 10 |
| 滚轮 编码器 A 相 / B 相 | 11 / 12 |
| 滚轮 中键微动 | 13 |
| 按钮 精确模式（可选） | 14 |
| 灯环 WS2812 DIN（串 330Ω） | 4 |

（这套**避开了** S3 的绑带脚 0/3/45/46、USB 脚 19/20、以及 26~37 的 flash/PSRAM 脚。）

想手工指定任意引脚，在 `build_flags` 里用 `-DPIN_JOY_UP=xx` 覆盖即可，**不必改代码**：

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc --build-property "compiler.cpp.extra_flags=-DPIN_ENC_SW=2" <sketch目录>
```

## 灯环灯效（WS2812B，12 颗）

接线：**DIN 串 330Ω → `PIN_LED_RING`**，VCC → 5V，GND → GND（环上的 DOUT 不用接）。
逻辑编号 0 = 正上方，顺时针递增；装好后若方向与摇杆不一致，改 `LED_RING_OFFSET`（0~11）校准。

| 状态 / 输入 | 灯效 |
|---|---|
| 待机（已连接） | 青色整环呼吸（周期 2.6s） |
| 未连接蓝牙 | 红色整环呼吸 |
| 摇杆 上 / 右 / 下 / 左 | 对应方位的 3 颗灯珠亮青色弧 |
| 左键 | 左半环绿色填充 |
| 右键 | 右半环橙红填充 |
| 滚轮中键 | 整环紫色脉冲 |
| 精确模式 | 整环暖白低亮 |
| 滚轮转动 | 金色彗星 + 4 颗渐暗拖尾，方向跟随转动 |

电流：12 颗全白最亮约 **720mA**。`LED_MASTER`（默认 110，约 43%）是总亮度闸门，USB 供电时
不要超过 150，否则会掉压甚至拖断蓝牙；要全亮就给灯环单独供 5V 并与开发板共地。

## 依赖库

| 依赖 | 要求 | 本工程实测版本 |
|---|---|---|
| 开发板包 **arduino-esp32** | 3.x | **3.2.0**（开发板管理器搜 `esp32`） |
| 库 **NimBLE-Arduino** | **≥ 2.3.8** | **2.3.9** |
| 库 **HijelHID_BLEMouse** | — | **0.1.0**（管理库搜 `HijelHID`） |
| 库 **Adafruit NeoPixel** | ≥ 1.12 | **1.15.5**（管理库搜 `Adafruit NeoPixel`） |

> ⚠️ **坑：Arduino 库管理器里的 NimBLE-Arduino 最高只到 2.3.7，低于要求的 2.3.8。**
> 直接「管理库搜 NimBLE-Arduino」装到的版本偏旧，请改装 GitHub 的 2.3.9：
> 下载 <https://github.com/h2zero/NimBLE-Arduino/archive/refs/tags/2.3.9.zip>，
> 再用 `Sketch → Include Library → Add .ZIP Library` 安装。

> 旧的 `ESP32-BLE-Mouse`（T-vK）在 arduino-esp32 core 3.x 上编译不过，不要使用。

## 编译与烧录

打开 `joystick_mouse/joystick_mouse.ino`，选好 ESP32 开发板与串口，直接上传。串口监视器 115200。

命令行（arduino-cli）等价做法 —— `HijelHID_BLEMouse` 不在官方索引里，必须走 zip 安装，
而 `--zip-path` 需要先在配置里开 `library.enable_unsafe_install: true`：

```powershell
arduino-cli lib install --zip-path NimBLE-2.3.9.zip
arduino-cli lib install --zip-path HijelHID_BLEMouse.zip

# ESP32-WROOM-32（经典 ESP32）
arduino-cli compile --fqbn esp32:esp32:esp32  joystick_mouse
arduino-cli upload  --fqbn esp32:esp32:esp32  -p COM3 joystick_mouse

# ESP32-C3 Super Mini —— 注意 CDCOnBoot=cdc 不能省（否则与编码器抢 UART0 脚）
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc joystick_mouse
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc -p COM3 joystick_mouse
```

- 换板子只改 `--fqbn`，**引脚表会自动切换**（见上文）。
- C3 Super Mini 用**原生 USB** 烧录，插上就是 USB Serial/JTAG 设备；个别板子需要在插着线时按一下 BOOT 再松手进下载模式。
- C3 的串口监视器要选 **USB 那个串口**（`CDCOnBoot=cdc` 时 `Serial` 走 USB，115200）。

## 配对

1. 上电后串口打印「已启动，等待蓝牙配对…」
2. 电脑蓝牙搜索 **Arcade Mouse** → 点击配对（Just Works，无 PIN）
3. 串口打印「已配对，可以开始操作」后即可使用

## 校准清单

| 现象 | 调什么 |
|---|---|
| 要校准滚轮方向 | 先把 `DEBUG_ENC` 改 `1` 打开串口打印（默认 `0`） |
| 滚轮方向与预期相反 | `ENC_INVERT` 由 `0` 改 `1` |
| 滚一格跳太多 / 太少 | `ENC_STEP`（默认 `4`，即四倍频） |
| 光标太飘 | 降 `CURSOR_MAX_SPEED`（默认 22 像素/报告） |
| 起速太慢 | 升 `CURSOR_MIN_SPEED`（默认 2） |
| 加速太急 / 太缓 | 调 `ACCEL_TIME_MS`（默认 700ms 到顶速） |
| **光标比预期慢** | 升 `SEND_MS`（默认 `8`）。真实 BLE 连接间隔由主机协商，常为 11.25~15ms，慢于名义 7.5ms |
| 按住“精确模式”键降速比例 | `FINE_SCALE`（默认 `0.35`） |
| 校准完成后 | `DEBUG_ENC` 改回 `0`，关掉串口打印 |

## 组装顺序

面板开孔（摇杆 Ø30、按钮 Ø24/Ø30、滚轮 Ø7）→ 摇杆背面 M4 固定 → 按钮卡入 → 滚轮模块做支架装到位 → ESP32 固定背面 → 所有 COM 并一条 GND 主线 → 底板贴防滑垫。

## 已知状态

- ✅ **编译已通过验证**（2026-10-18）：arduino-esp32 **3.2.0** + NimBLE-Arduino **2.3.9** + HijelHID_BLEMouse **0.1.0**，
  目标 `esp32:esp32:esp32`（经典 ESP32）与 `esp32:esp32:esp32s3` 两个板子**均 0 错误、0 警告**。
  经典 ESP32 占用 Flash 46%（613 KB / 1.25 MB）、RAM 11%。
- ⏳ **实机行为仍待烧录后校准**：滚轮方向 / 步进（`ENC_INVERT` / `ENC_STEP`）、光标速度
  （`SEND_MS` / `CURSOR_MAX_SPEED`）需装配后按上表实测 —— 这一步必须在真硬件上做，无法离线验证。
- 本轮已修掉的问题（细节见 `.ino` 里的注释）：
  编码器「读-改-写」被中断打断导致**丢步**、断连后重连**瞬间冲到顶速**、
  `move()` 发送快于真实 BLE 报告间隔导致**位移被覆盖**、累加器溢出、
  出厂默认开着串口打印拖慢手感、S3 引脚表不匹配。

## 后续：同一个手柄将来要控制机械臂

本固件的「输入层」（消抖 + 正交解码 + 方向/按键/格数）与「输出层」（写进 BLE 鼠标）是分开的，
所以换用途**不需要返工**：

- **最省事的做法**：ESP32 的 BLE 可以**同时**承载 HID 和自定义 GATT 服务。鼠标照常能用，
  再挂一个 Nordic UART Service(NUS)，把摇杆/按键/格数报文实时发给主机侧的桥接程序，
  由桥接程序转成机械臂的指令（HTTP / WebSocket / 串口都行）—— **两者不冲突，不必二选一**。
- 若将来改走 WiFi 或 USB 串口，直接复用 `loop()` 里算出的 `vx / vy / 按键 / notches`
  这几个量即可，输入层原样保留。
- 注意保留「精确模式键」的语义：做机械臂时针点动/微调会很需要低速档（`FINE_SCALE`）。

