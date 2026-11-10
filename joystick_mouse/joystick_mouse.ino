/* ============================================================================
 *  joystick_mouse.ino —— 街机摇杆 → BLE 无线鼠标（ESP32）
 * ----------------------------------------------------------------------------
 *  硬件清单：
 *    - ESP32 开发板（ESP32-WROOM-32 / ESP32-S3 等带 BLE 的型号；S2、P4 没有 BLE）
 *    - 微动开关式街机摇杆 ×1（4 个方向微动）
 *    - 街机按钮 ×2（左键 / 右键）
 *    - 滚轮模块 ×1（从 2.4G 无线鼠标上拆下来的“编码器 + 中键微动”）
 *    - 可选：精确模式按钮 ×1
 *
 *  接线（所有开关一律 COM → GND，信号脚 → GPIO；用芯片内部上拉，免外接电阻）
 *    ┌──────────────────────────┬────────┐
 *    │ 摇杆 上   UP             │ GPIO25 │
 *    │ 摇杆 下   DOWN           │ GPIO26 │
 *    │ 摇杆 左   LEFT           │ GPIO27 │
 *    │ 摇杆 右   RIGHT          │ GPIO14 │
 *    │ 按钮 左键                │ GPIO13 │
 *    │ 按钮 右键                │ GPIO4  │
 *    │ 滚轮 编码器 A 相         │ GPIO32 │
 *    │ 滚轮 编码器 B 相         │ GPIO33 │
 *    │ 滚轮 中键微动            │ GPIO16 │
 *    │ 按钮 精确模式（可选）    │ GPIO18 │
 *    └──────────────────────────┴────────┘
 *
 *  ⚠️ 上表是「经典 ESP32（WROOM-32 等）」的接线。换芯片时引脚会**自动切换到**代码里对应的
 *     那一套表，照经典表硬接会拿到无效引脚：
 *       - ESP32-S3：没有 GPIO22~25、也没有 DAC 脚
 *       - ESP32-C3（含 C3 Super Mini）：只有 GPIO0~10 与 18~21，且 11~17 被片内 flash 占用
 *     想手工指定就在 build_flags 里 -DPIN_JOY_UP=xx 覆盖任意一条。
 *
 *  ★ ESP32-C3（含 C3 Super Mini）接线：
 *    ┌──────────────────────────┬────────┐
 *    │ 摇杆 上   UP             │ GPIO3  │
 *    │ 摇杆 下   DOWN           │ GPIO4  │
 *    │ 摇杆 左   LEFT           │ GPIO5  │
 *    │ 摇杆 右   RIGHT          │ GPIO6  │
 *    │ 按钮 左键                │ GPIO7  │
 *    │ 按钮 右键                │ GPIO10 │
 *    │ 滚轮 编码器 A 相         │ GPIO20 │
 *    │ 滚轮 编码器 B 相         │ GPIO21 │
 *    │ 滚轮 中键微动            │ GPIO0  │
 *    │ 按钮 精确模式（可选）    │ GPIO1  │
 *    │ 灯环 WS2812 DIN          │ GPIO2  │（DIN 串 330Ω；VCC→5V；GND→GND）
 *    └──────────────────────────┴────────┘
 *    C3 上刻意避开的脚：GPIO8 / 9 是绑带脚（在 Super Mini 上就是板载 LED 与 BOOT 键，
 *    开机瞬间被拉低可能进下载模式）；GPIO11~17 被片内 SPI flash 占用；GPIO18/19 是
 *    USB D-/D+。GPIO2 虽也是绑带脚，但只用它输出 WS2812 数据（DIN 是高阻输入，不会把
 *    GPIO2 拉低），所以安全；它也是本板上唯一剩下的空闲脚。
 *    ⚠️ 编码器占了 GPIO20/21，而 C3 默认的 UART0 也在这两个脚上 —— 必须选 **CDCOnBoot=cdc**
 *       （Serial 走 USB），否则 UART0 外设会和编码器抢同一对脚（固件里有 #error 兜底）。
 *       Super Mini 本来就用原生 USB 烧录，选 cdc 正是推荐配置。
 *
 *  ★ 滚轮模块接法（最易出错的一处）
 *
 *    【本方案默认按“机械式编码器”落地】—— 即无源 3 脚：A / 公共(C) / B。
 *      特征：只有 3 根引线，没有任何 VCC 供电脚，转动时靠机械触点通断。
 *        - 编码器公共脚 C → GND。接错成信号脚会让编码器一直读到 0，
 *          表现为“转了没反应”
 *        - 编码器 A → GPIO32，B → GPIO33
 *        - 编码器原来的两颗上拉电阻丢弃，直接用 ESP32 内部上拉
 *        - 中键微动两只脚：一脚 GPIO16、一脚 GND
 *
 *    【备选接法：如果拆开后发现是“光学式编码器”】（引脚 ≥4 根，且有一根
 *      标着 VCC / IR / LED 的供电脚；滚轮是带齿缝的码盘 + 红外对管）
 *        - 模块 VCC → ESP32 3.3V（光学式必须供电，机械式不接这脚）
 *        - 模块 GND → GND
 *        - 输出 OUT_A → GPIO32，OUT_B → GPIO33（不变）
 *        - 代码一行都不用改：内部上拉对光学式的集电极开路输出无害，
 *          照常按 CHANGE 中断解码
 *        - 注意：供电超过 3.3V 的模块不要直连 GPIO，中间加电平转换或分压
 *      → 组装时确认了是哪一种，把对应接法照做即可；两者的固件完全一致。
 *
 *  引脚注意事项：
 *    - 必须避开 GPIO6~11（连闪存）、GPIO34~39（没有内部上拉）、
 *      以及启动绑带脚 GPIO0 / 2 / 12 / 15
 *    - 若用 ESP32-WROVER（带 PSRAM），GPIO16/17 被 PSRAM 占用，
 *      请把中键改到 GPIO19、精确模式改到 GPIO23
 *    - GPIO32/33 同时是触摸引脚：走线尽量短，别让裸露引脚被手蹭到
 *
 *  上电前安全确认：万用表量滚轮模块信号脚的静态高电平，不超过 3.3V 才可直接接 GPIO
 *  （本鼠标为电池供电，内部通常 3.0~3.3V 逻辑，是安全的）
 *
 *  依赖库（Arduino IDE 里装）：
 *    1) 开发板包 arduino-esp32 3.3.x   —— 工具→开发板→开发板管理器，搜 "esp32"
 *    2) 库 NimBLE-Arduino >= 2.3.9     —— 工具→管理库，搜 "NimBLE-Arduino"
 *    3) 库 HijelHID_BLEMouse           —— 工具→管理库，搜 "HijelHID"
 *    4) 库 Adafruit NeoPixel           —— 工具→管理库，搜 "Adafruit NeoPixel"（灯环用）
 *  说明：旧的 ESP32-BLE-Mouse(T-vK) 在 core 3.x 上编译不过，不要用。
 *
 *  功能：
 *    - 摇杆推方向 → 光标按“加速度斜坡”移动：轻推慢、推得越久越快；压两个微动可走斜向
 *    - 左键 / 右键按钮（按住不放即为拖动）
 *    - 滚轮旋转 → 滚轮；滚轮按下 → 中键
 *    - 按住“精确模式”键 → 光标降为 35% 速度（可选键）
 *
 *  装好后必须校准（详见下方参数）：
 *    - 滚轮方向：先把 DEBUG_ENC 临时改成 1 打开串口打印；顺时针转一格，若与预期相反 → ENC_INVERT 改 1
 *    - 滚轮步进：一格跳太多/太少 → 调 ENC_STEP（默认 4）
 *    - 手感：只调下方「光标『时间 → 速度』曲线」那一段的 6 个宏，它们就是这个手柄的全部手感旋钮
 *    - **先关掉 Windows 的「提高指针精确度」**：鼠标设置 → 其他鼠标选项 → 指针选项 → 取消勾选。
 *      系统自带的加速曲线会把**小位移再缩小**、大位移再放大，轻点走不动、快推又飞出去，
 *      我们算好的曲线会被它二次加工，手感无从预测
 *    - **光标比预期慢**：多半是库的报告节拍比主机能收的更快，位移被主机丢掉了 ——
 *      库的 setUpdateRate 与我们的 SEND_MS 必须成对调（见 setup 里的说明）
 *    - 校准完成后把 DEBUG_ENC 改回 0（串口打印会拖慢手感）
 *
 *  后续扩展（同一个手柄将来要用来控制机械臂）—— 已按“可扩展”组织，不需要返工：
 *    - 本文件的「输入层」（消抖 + 正交解码 + 方向/按键状态）与「输出层」（写进 BLE 鼠标）是分开的，
 *      换用途时只需替换/并列增加输出那一段。
 *    - ESP32 的 BLE 可以**同时**跑 HID 和自定义 GATT 服务：也就是说鼠标照常能用，
 *      再挂一个 Nordic UART(NUS) 服务把摇杆/按钮/格数报文发给主机侧的桥接程序，不必二选一。
 *    - 若将来走 WiFi/串口，同样可以直接复用 loop 里算出的 vx/vy/按键/格数这几个量。
 * ==========================================================================*/

#include <HijelHID_BLEMouse.h>
#include <Adafruit_NeoPixel.h>

/* ------------------------------- 引脚 -------------------------------
   【兼容性】按目标芯片自动选表，也可以在 build_flags 里用 -DPIN_JOY_UP=xx 强行覆盖任意一条：
     - ESP32-S3  → 第二套（S3 没有 GPIO22~25，也没有 DAC 脚）
     - ESP32-C3  → 第三套（C3 只有 GPIO0~10 + 18~21，11~17 被片内 flash 占用）
     - 其它（经典 ESP32 / WROOM-32 等）→ 第一套

   ⚠️ ESP32-WROVER（带 PSRAM）的 GPIO16/17 被 PSRAM 占用：
      中键 → GPIO19、精确模式键 → GPIO23（用 -D 覆盖，或直接改下面的经典表）。
   经典 ESP32 绑带脚完整清单：GPIO0 / GPIO2 / GPIO5 / GPIO12 / GPIO15（一个都别用）。 */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  /* ESP32-S3：避开绑带脚 0/3/45/46、USB 脚 19/20、以及 26~37（SPI flash / PSRAM） */
  #ifndef PIN_JOY_UP
  #define PIN_JOY_UP         5
  #endif
  #ifndef PIN_JOY_DOWN
  #define PIN_JOY_DOWN       6
  #endif
  #ifndef PIN_JOY_LEFT
  #define PIN_JOY_LEFT       7
  #endif
  #ifndef PIN_JOY_RIGHT
  #define PIN_JOY_RIGHT      8
  #endif
  #ifndef PIN_BTN_LEFT
  #define PIN_BTN_LEFT       9
  #endif
  #ifndef PIN_BTN_RIGHT
  #define PIN_BTN_RIGHT     10
  #endif
  #ifndef PIN_ENC_A
  #define PIN_ENC_A         11
  #endif
  #ifndef PIN_ENC_B
  #define PIN_ENC_B         12
  #endif
  #ifndef PIN_ENC_SW
  #define PIN_ENC_SW        13
  #endif
  #ifndef PIN_BTN_FINE
  #define PIN_BTN_FINE      14
  #endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  /* ESP32-C3（含 C3 Super Mini）：可用 GPIO 只有 0~10 与 18~21。本表避开
     2 / 8 / 9（绑带脚；8、9 在 Super Mini 上常是板载 LED 与 BOOT 键）、
     11~17（片内 SPI flash）、18 / 19（USB D-/D+）。
     编码器落在 20/21，所以 Serial 必须走 USB —— 见引脚块后面的 #error 兜底。 */
  #ifndef PIN_JOY_UP
  #define PIN_JOY_UP         3
  #endif
  #ifndef PIN_JOY_DOWN
  #define PIN_JOY_DOWN       4
  #endif
  #ifndef PIN_JOY_LEFT
  #define PIN_JOY_LEFT       5
  #endif
  #ifndef PIN_JOY_RIGHT
  #define PIN_JOY_RIGHT      6
  #endif
  #ifndef PIN_BTN_LEFT
  #define PIN_BTN_LEFT       7
  #endif
  #ifndef PIN_BTN_RIGHT
  #define PIN_BTN_RIGHT     10
  #endif
  #ifndef PIN_ENC_A
  #define PIN_ENC_A         20
  #endif
  #ifndef PIN_ENC_B
  #define PIN_ENC_B         21
  #endif
  #ifndef PIN_ENC_SW
  #define PIN_ENC_SW         0
  #endif
  #ifndef PIN_BTN_FINE
  #define PIN_BTN_FINE       1
  #endif
#else
  #ifndef PIN_JOY_UP
  #define PIN_JOY_UP        25
  #endif
  #ifndef PIN_JOY_DOWN
  #define PIN_JOY_DOWN      26
  #endif
  #ifndef PIN_JOY_LEFT
  #define PIN_JOY_LEFT      27
  #endif
  #ifndef PIN_JOY_RIGHT
  #define PIN_JOY_RIGHT     14
  #endif
  #ifndef PIN_BTN_LEFT
  #define PIN_BTN_LEFT      13
  #endif
  #ifndef PIN_BTN_RIGHT
  #define PIN_BTN_RIGHT      4
  #endif
  #ifndef PIN_ENC_A
  #define PIN_ENC_A         32
  #endif
  #ifndef PIN_ENC_B
  #define PIN_ENC_B         33
  #endif
  #ifndef PIN_ENC_SW
  #define PIN_ENC_SW        16
  #endif
  #ifndef PIN_BTN_FINE
  #define PIN_BTN_FINE      18
  #endif
#endif

/* ---------------------------- 灯环引脚 / 参数 ----------------------------
   WS2812B 环形灯只用 1 个 GPIO 当数据线（DIN），VCC/GND 单独供电：
     - C3（含 Super Mini）：GPIO2 —— 上面三张表里唯一剩下的空闲脚。它是绑带脚，
       只能接 WS2812 的高阻输入，**不能**在这根线上挂下拉电阻或对地开关。
     - S3：GPIO4（避开本表占用的 5~14、USB 19/20、片内 flash 26~37）
     - 经典 ESP32 / WROOM：GPIO23（本表未占用，也不是绑带脚；WROVER 的 16/17 归 PSRAM）
   接线：DIN 串 330Ω → 该脚；VCC → 5V；GND → GND；环另一头的 DOUT 不用接。
   12 颗 WS2812B 全白最亮约 720mA，USB 供电时务必用 LED_MASTER 压低总亮度，
   或者给灯环单独供 5V（与开发板共地），否则会掉压、甚至把蓝牙拖断。 */
#ifndef PIN_LED_RING
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    #define PIN_LED_RING       2
  #elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #define PIN_LED_RING       4
  #else
    #define PIN_LED_RING      23
  #endif
#endif

#ifndef LED_RING_COUNT
  #define LED_RING_COUNT    12       // 环形灯珠数量
#endif
#ifndef LED_MASTER
  #define LED_MASTER       110       // 总亮度 0~255（电流总闸）；USB 供电别超过 150
#endif
#define LED_FRAME_MS        25       // 灯效刷新周期（毫秒）
#define LED_BREATH_MS     2600       // 待机呼吸一次的周期
#define LED_RING_OFFSET      0       // 装好后若灯效方向与摇杆不一致，改成 1~11 校准
#define LED_RING_CW          1       // 0 = 环上编号按逆时针排
#ifndef LED_DEMO
  #define LED_DEMO            1      // 1 = 开机后循环演示全部灯效（每种 2 秒），验证完改回 0
#endif
#define LED_DEMO_STEP_MS  2000       // 演示模式下每种灯效的时长

/* C3 的 GPIO20/21 既是这套表的编码器引脚、又是默认 UART0 的 TX/RX。
   若 Serial 没走 USB（CDCOnBoot=default），UART0 外设会和编码器抢同一对脚 ——
   滚轮读数变乱、串口打印也花，且现象很难反推到根因，所以这里直接编译期拦下。 */
#if defined(CONFIG_IDF_TARGET_ESP32C3) && defined(ARDUINO_USB_CDC_ON_BOOT) && \
    !ARDUINO_USB_CDC_ON_BOOT && (PIN_ENC_A == 20 || PIN_ENC_B == 21)
#error "ESP32-C3：编码器接在 GPIO20/21 时必须选 CDCOnBoot=cdc（Serial 走 USB），否则与 UART0 冲突"
#endif

/* ------------------------------- 参数 ------------------------------- */
/* ⚠️ TICK_MS / SEND_MS 必须跟着「库的 BLE 报告节拍」走，改一个要一起改：
   库内部有自己的发送任务，按 setUpdateRate() 指定的节拍把 move() 的值发出去
   （我们设的是 HIDRate::Hz50 ＝ **20ms 一份报告**，见 setup）。
   于是：TICK_MS(10) 是运动解算节拍，SEND_MS(20) 是**真正调用 move() 的间隔**；
   两者相等 ⇒ 每个 tick 只累加半份位移，攒够一份报告才发一次，一一对应不丢步。
   为什么不用库默认的 Hz125(7.5ms)：库的发送任务并不管主机受不受得了，会按 7.5ms 硬发；
   而主机（Windows 常见的协商结果是 11.25~15ms）收不完就直接把那份报告丢掉
   ⇒ 位移凭空消失，表现正是「比预期慢 + 一顿一顿」。降到 50Hz 后每份报告都送得出去。 */
#define TICK_MS            10       // 运动 / HID 解算节拍（毫秒）
#define SEND_MS            20       // 真正调用 move() 的间隔，必须与库的报告节拍一致
#define POLL_MS            2        // 输入采样节拍：比运动节拍快，点击延迟≈消抖窗口而非一个 TICK_MS
#define DEBOUNCE_MS        6        // 微动开关消抖时间
/* ---- 光标「时间 → 速度」曲线 --------------------------------------------
   摇杆只有 0/1，没有「推多深」这个维度 ⇒ 唯一可控量就是**按住多久**。
   因此分三段，目标是「轻点一小步能停准、按住能像鼠标一样飞」：
     ① 切入补步  KICK_PX   —— 刚推方向补一小步，让最短的一次点击也有明确位移
     ② 低速巡航  CRUISE_MS / V_CRUISE —— 前 150ms 恒定慢速，手抖不飞，用来瞄准按钮
     ③ 二次加速  ACCEL_TIME_MS / V_MAX —— 之后按 t² 曲线快速升到顶速，长按 = 跑
   手感偏慢/偏飘时只改这 6 个数即可（速度单位都是「像素/一份报告」，一份报告 = 20ms）。 */
#define KICK_PX            6.0f     // 切入补步（像素）：让「点一下」也有可感位移
#define CRUISE_MS          150      // 低速巡航段时长（毫秒）
#define V_CRUISE           4.0f     // 巡航速度（像素/报告）≈ 4px/20ms ≈ 200 px/s
#define V_MAX              48.0f    // 顶速（像素/报告）≈ 48px/20ms ≈ 2400 px/s（1920 屏约 0.8s 穿屏）
#define ACCEL_TIME_MS      500      // 巡航段之后爬到顶速所需时长
#define FINE_SCALE         0.30f    // 精确模式：整条曲线（含切入补步）缩放系数
#define DIAG_SCALE         0.7071f  // 斜向速度补偿（1/√2，避免斜着走更快）
#define ENC_STEP           4        // 四倍频：4 个电平跳变 = 滚轮 1 格
#define ENC_CAP            400      // 单次最多取走的跳变数（=100 格），防累加器溢出
#define ENC_INVERT         0        // 滚轮方向反了就把这里改成 1
#define DEBUG_ENC          0        // 出厂关闭（串口打印会拖慢手感）；校准滚轮时改成 1
#define DEBUG_PINS         1        // 【调试】串口打印各输入的实时电平，核对接线用；接线确认后改回 0

/* ----------------------------- BLE 鼠标 ----------------------------- */
// 参数：名称, 厂商, 初始电量, 按键数(5=含前进/后退), 是否启用横向滚轮
HijelBLEMouse mouse("Arcade Mouse", "DIY", 100, 5, false);

/* ------------------------------ 消抖输入 ---------------------------- */
struct DebInput {
  uint8_t  pin;
  bool     stable;      // 稳定电平：LOW = 按下
  bool     raw;         // 最近一次原始读数
  uint32_t changedAt;   // 电平最后一次变化的时刻
};

DebInput dirs[4] = {
  {PIN_JOY_UP,    HIGH, HIGH, 0},
  {PIN_JOY_DOWN,  HIGH, HIGH, 0},
  {PIN_JOY_LEFT,  HIGH, HIGH, 0},
  {PIN_JOY_RIGHT, HIGH, HIGH, 0},
};
enum { D_UP, D_DOWN, D_LEFT, D_RIGHT };

DebInput btns[4] = {
  {PIN_BTN_LEFT,  HIGH, HIGH, 0},
  {PIN_BTN_RIGHT, HIGH, HIGH, 0},
  {PIN_ENC_SW,    HIGH, HIGH, 0},
  {PIN_BTN_FINE,  HIGH, HIGH, 0},
};
enum { B_LEFT, B_RIGHT, B_MIDDLE, B_FINE };

static inline bool isDown(const DebInput &d) { return d.stable == LOW; }

static void pollInput(DebInput &d, uint32_t now) {
  bool r = digitalRead(d.pin);
  if (r != d.raw) { d.raw = r; d.changedAt = now; }
  if ((now - d.changedAt) >= DEBOUNCE_MS && d.stable != d.raw) d.stable = d.raw;
}

#if DEBUG_PINS
/* 接线核对用：把 8 个输入的**消抖后**状态印成「名字(GPIO号)=电平」，只在状态变化时打印。
   0 = 按下（微动闭合到 GND），1 = 松开。
   纯 ASCII 输出，避免不同串口工具的编码差异把中文打印成乱码。
   用法：依次推 上 / 下 / 左 / 右 各按住 1 秒，看哪一项变 0 ——
        变 0 的那一项就是「你刚推的那个方向」实际接到的 GPIO，据此判断有没有接反。 */
static void dbgPins(bool force) {
  static uint8_t prev = 0xFF;
  uint8_t m = 0;
  for (int i = 0; i < 4; i++) if (isDown(dirs[i]))        m |= (uint8_t)(1u << i);
  if (isDown(btns[B_LEFT]))   m |= (uint8_t)(1u << 4);
  if (isDown(btns[B_RIGHT]))  m |= (uint8_t)(1u << 5);
  if (isDown(btns[B_MIDDLE])) m |= (uint8_t)(1u << 6);
  if (isDown(btns[B_FINE]))   m |= (uint8_t)(1u << 7);
  if (!force && m == prev) return;
  prev = m;
  Serial.printf("[PIN] UP(%d)=%d DOWN(%d)=%d LEFT(%d)=%d RIGHT(%d)=%d | L(%d)=%d R(%d)=%d MID(%d)=%d FINE(%d)=%d\n",
                PIN_JOY_UP,    isDown(dirs[D_UP])    ? 0 : 1,
                PIN_JOY_DOWN,  isDown(dirs[D_DOWN])  ? 0 : 1,
                PIN_JOY_LEFT,  isDown(dirs[D_LEFT])  ? 0 : 1,
                PIN_JOY_RIGHT, isDown(dirs[D_RIGHT]) ? 0 : 1,
                PIN_BTN_LEFT,  isDown(btns[B_LEFT])   ? 0 : 1,
                PIN_BTN_RIGHT, isDown(btns[B_RIGHT])  ? 0 : 1,
                PIN_ENC_SW,    isDown(btns[B_MIDDLE]) ? 0 : 1,
                PIN_BTN_FINE,  isDown(btns[B_FINE])   ? 0 : 1);
}
#endif

static int iround(float v) { return (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

/* --------------------- 滚轮编码器（中断 + 四倍频） ------------------ */
// 格雷码状态机：把 A/B 的每次跳变按方向累加，机械抖动产生的非法跳变计 0。
// 机械式与光学式输出都是方波，解码逻辑通用。
// ISR 用 digitalRead()：它在 ESP32 上是 IRAM 安全的，且在“手转滚轮”的速率下
// （即便 200 格/秒 ≈ 800 次沿变/秒，每次 ISR 几微秒）完全不会丢沿，故不做寄存器直读的优化。
static const int8_t QTAB[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
volatile int16_t encAccum = 0;   // 累积跳变数，正负代表旋转方向（用 16 位，避免 int8 快速旋转时溢出）
volatile uint8_t encPrev  = 0;

static void IRAM_ATTR encISR() {
  uint8_t cur = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  encAccum += QTAB[(encPrev << 2) | cur];
  encPrev = cur;
}

/* 取走“整格数”。这里是全文件唯一需要在循环里读-改-写 encAccum 的地方。
   ⚠️ 「单字节读写是原子的」只保证了**单独的** load / store 原子，**并不保证**
      `acc = encAccum; …; encAccum = acc - n*ENC_STEP;` 这条读-改-写序列：
      中断若夹在中间累加，就会被后面的写回覆盖 ⇒ 丢步（原版注释在此处判断错误）。
      故整段用 noInterrupts()/interrupts() 包住，只有几条指令、微秒级代价。
   顺带把单次取走上限钳到 ENC_CAP，保证 notches 不会溢出 int8_t。 */
static bool encTakeNotches(int8_t &notches) {
  bool got = false;
  noInterrupts();
  int16_t acc = encAccum;
  if (acc >= ENC_STEP || acc <= -ENC_STEP) {
    if (acc >   ENC_CAP) acc =  ENC_CAP;
    if (acc <  -ENC_CAP) acc = -ENC_CAP;
    notches   = (int8_t)(acc / ENC_STEP);
    encAccum  = (int16_t)(acc - (int16_t)notches * ENC_STEP);  // 保留余数，慢转不丢步
    got = true;
  }
  interrupts();
  return got;
}

/* ---------------------------- WS2812 灯环 ----------------------------
   逻辑编号约定：0 = 正上方（12 点方向），顺时针递增；可用 LED_RING_OFFSET 整体补偿安装角度。
   每种输入都有独立反馈：
     待机        → 已连接：青色呼吸；未连接：红色呼吸
     摇杆四向    → 对应方向的 3 颗灯珠弧形高亮（青色）
     左键        → 左半环绿色填充（逻辑 6~11）
     右键        → 右半环橙红填充（逻辑 0~5）
     滚轮中键    → 整环紫色脉冲
     精确模式    → 整环暖白低亮（提示“当前是慢速档”）
     滚轮转动    → 金色彗星，按转动方向沿环跑
   叠加规则是“逐通道取最大值”，多个效果同时出现时不会糊成一团白。 */
static Adafruit_NeoPixel ledRing(LED_RING_COUNT, PIN_LED_RING, NEO_GRB + NEO_KHZ800);
static uint16_t ledR[LED_RING_COUNT], ledG[LED_RING_COUNT], ledB[LED_RING_COUNT];
static uint32_t ledLastFrame = 0;
static uint32_t ledSpinUntil = 0;      // 彗星保留到该时刻（毫秒）
static float    ledSpinPos   = 0;      // 彗星头部位置（以“颗”为单位）
static int8_t   ledSpinDir   = 1;
static bool     ledReady     = false;

/* 逻辑编号 → 物理编号（处理负数、顺时针/逆时针、整体偏移） */
static int ledIdx(int logical) {
  int i = logical % LED_RING_COUNT;
  if (i < 0) i += LED_RING_COUNT;
  if (!LED_RING_CW) i = (LED_RING_COUNT - i) % LED_RING_COUNT;
  return (i + LED_RING_OFFSET) % LED_RING_COUNT;
}

static void ledMax(int logical, int r, int g, int b) {
  int i = ledIdx(logical);
  if (r > (int)ledR[i]) ledR[i] = (uint16_t)r;
  if (g > (int)ledG[i]) ledG[i] = (uint16_t)g;
  if (b > (int)ledB[i]) ledB[i] = (uint16_t)b;
}

/* 以 center 为中心点亮 3 颗弧形（方向键用） */
static void ledArc(int center, int r, int g, int b) {
  ledMax(center - 1, r, g, b);
  ledMax(center,     r, g, b);
  ledMax(center + 1, r, g, b);
}

static void ledBegin() {
  ledRing.begin();
  ledRing.setBrightness(LED_MASTER);   // 亮度总闸，直接决定 WS2812 的耗电
  ledRing.clear();
  ledRing.show();
  ledReady = true;
}

/* 滚轮每转一格调用一次：彗星位置按格数推进，拖尾方向跟着转动方向 */
static void ledOnEncoder(int8_t notches) {
  ledSpinPos += (float)notches;
  while (ledSpinPos <  0)             ledSpinPos += LED_RING_COUNT;
  while (ledSpinPos >= LED_RING_COUNT) ledSpinPos -= LED_RING_COUNT;
  ledSpinDir   = (notches >= 0) ? 1 : -1;
  ledSpinUntil = millis() + 450;
}

/* 把累积的颜色一次写进灯环（含 255 钳位） */
static void ledFlush() {
  for (int i = 0; i < LED_RING_COUNT; i++) {
    int r = ledR[i] > 255 ? 255 : (int)ledR[i];
    int g = ledG[i] > 255 ? 255 : (int)ledG[i];
    int b = ledB[i] > 255 ? 255 : (int)ledB[i];
    ledRing.setPixelColor(i, (uint8_t)r, (uint8_t)g, (uint8_t)b);
  }
  ledRing.show();
}

#if LED_DEMO
/* 演示模式：不读输入、不等蓝牙配对，按固定顺序循环展示全部灯效，每种 2 秒。
   用途是单独验证灯环（接线、方向、颜色）。验证完把 LED_DEMO 改回 0。 */
static uint8_t  demoStep   = 0;
static uint32_t demoStepAt = 0;
static const char *DEMO_NAME[] = {
  "1 unpaired-red", "2 idle-cyan", "3 joy-up", "4 joy-right",
  "5 joy-down", "6 joy-left", "7 btn-left", "8 btn-right",
  "9 btn-middle", "10 fine", "11 encoder-cw", "12 encoder-ccw"
};
#define LED_DEMO_STEPS 12

static void ledDemoRender(uint32_t now) {
  if (demoStepAt == 0) {
    demoStepAt = now;
    Serial.printf("[LED] %s\n", DEMO_NAME[demoStep]);
  }
  while (now - demoStepAt >= LED_DEMO_STEP_MS) {
    demoStepAt += LED_DEMO_STEP_MS;
    demoStep = (uint8_t)((demoStep + 1) % LED_DEMO_STEPS);
    Serial.printf("[LED] %s\n", DEMO_NAME[demoStep]);
  }

  float t      = (float)(now - demoStepAt) / (float)LED_DEMO_STEP_MS;  // 0 → 1
  float breath = 0.5f - 0.5f * cosf(t * 6.2831853f);                   // 一步一次呼吸

  switch (demoStep) {
    case 0: {                       // 未连接：红色呼吸
      int v = (int)(18 + 130 * breath);
      for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, v, 0, 0);
      break;
    }
    case 1: {                       // 待机：青色呼吸
      int g = (int)(10 + 70 * breath);
      int b = (int)(14 + 90 * breath);
      for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, 0, g, b);
      break;
    }
    case 2: ledArc(0, 0, 150, 190); break;   // 摇杆上
    case 3: ledArc(3, 0, 150, 190); break;   // 摇杆右
    case 4: ledArc(6, 0, 150, 190); break;   // 摇杆下
    case 5: ledArc(9, 0, 150, 190); break;   // 摇杆左
    case 6: for (int i = 6; i < 12; i++) ledMax(i, 0, 165, 35); break;   // 左键
    case 7: for (int i = 0; i < 6;  i++) ledMax(i, 215, 70, 0); break;   // 右键
    case 8: {                       // 中键：紫色脉冲
      int v = (int)(70 + 95 * breath);
      for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, v, 0, v + 55);
      break;
    }
    case 9: for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, 95, 72, 42); break;  // 精确模式
    case 10:
    case 11: {                      // 彗星：正向 / 反向各跑一圈
      int dir  = (demoStep == 10) ? 1 : -1;
      int head = (int)lroundf(dir > 0 ? (t * LED_RING_COUNT)
                                     : (LED_RING_COUNT - t * LED_RING_COUNT));
      for (int k = 0; k < 4; k++) {
        int v = 210 - k * 50;
        ledMax(head - dir * k, v, (int)(v * 0.70f), 0);
      }
      break;
    }
  }
}
#endif

static void ledTask(uint32_t now) {
  if (!ledReady || (now - ledLastFrame) < LED_FRAME_MS) return;
  ledLastFrame = now;
  for (int i = 0; i < LED_RING_COUNT; i++) ledR[i] = ledG[i] = ledB[i] = 0;

#if LED_DEMO
  ledDemoRender(now);       // 演示模式：忽略输入与配对状态，固定顺序循环
  ledFlush();
  return;
#endif

  bool paired = mouse.isPaired();
  float phase  = (float)(now % LED_BREATH_MS) / (float)LED_BREATH_MS;
  float breath = 0.5f - 0.5f * cosf(phase * 6.2831853f);   // 0 → 1 → 0

  bool active = isDown(dirs[D_UP])   || isDown(dirs[D_DOWN]) ||
                isDown(dirs[D_LEFT]) || isDown(dirs[D_RIGHT]) ||
                isDown(btns[B_LEFT]) || isDown(btns[B_RIGHT]) ||
                isDown(btns[B_MIDDLE]) || isDown(btns[B_FINE]) ||
                (int32_t)(ledSpinUntil - now) > 0;

  if (!paired) {                   // 没连上：红色呼吸，其它效果都不显示
    int v = (int)(18 + 130 * breath);
    for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, v, 0, 0);
  } else if (!active) {            // 待机：青色呼吸
    int g = (int)(10 + 70 * breath);
    int b = (int)(14 + 90 * breath);
    for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, 0, g, b);
  }

  if (paired) {
    /* 有输入时保留一点底光：否则没被效果点到的灯珠全黑，看起来像掉线 */
    for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, 0, 8, 10);

    /* 摇杆四向：各自位置的 3 颗弧 */
    if (isDown(dirs[D_UP]))    ledArc(0, 0, 150, 190);
    if (isDown(dirs[D_RIGHT])) ledArc(3, 0, 150, 190);
    if (isDown(dirs[D_DOWN]))  ledArc(6, 0, 150, 190);
    if (isDown(dirs[D_LEFT]))  ledArc(9, 0, 150, 190);

    /* 左右键：各占半边，颜色区分 */
    if (isDown(btns[B_LEFT]))  for (int i = 6; i < 12; i++) ledMax(i, 0, 165, 35);
    if (isDown(btns[B_RIGHT])) for (int i = 0; i < 6;  i++) ledMax(i, 215, 70, 0);

    /* 中键：整环紫色脉冲（跟着待机呼吸的相位一起起伏） */
    if (isDown(btns[B_MIDDLE])) {
      int v = (int)(70 + 95 * breath);
      for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, v, 0, v + 55);
    }

    /* 精确模式：整环暖白低亮 */
    if (isDown(btns[B_FINE]))
      for (int i = 0; i < LED_RING_COUNT; i++) ledMax(i, 95, 72, 42);

    /* 滚轮：金色彗星 + 4 颗渐暗拖尾 */
    if ((int32_t)(ledSpinUntil - now) > 0) {
      int head = (int)lroundf(ledSpinPos);
      for (int t = 0; t < 4; t++) {
        int v = 210 - t * 50;
        ledMax(head - ledSpinDir * t, v, (int)(v * 0.70f), 0);
      }
    }
  }

  for (int i = 0; i < LED_RING_COUNT; i++) {
    int r = ledR[i] > 255 ? 255 : (int)ledR[i];
    int g = ledG[i] > 255 ? 255 : (int)ledG[i];
    int b = ledB[i] > 255 ? 255 : (int)ledB[i];
    ledRing.setPixelColor(i, (uint8_t)r, (uint8_t)g, (uint8_t)b);
  }
  ledRing.show();
}
/* ------------------------------- 状态 ------------------------------- */
static uint32_t cursorHoldSince = 0;   // 光标方向开始按住的时刻
static bool     wasPaired        = false;
static float    pendingX         = 0;  // 已算好但还没发出的位移（像素），见 loop 里 move() 的用法
static float    pendingY         = 0;
static uint32_t lastSend         = 0;  // 上一次真正发出 move() 的时刻

/* 松开摇杆 / 断连时把运动状态清零：
   ① 不清 cursorHoldSince，重连瞬间会按“很久以前”的时刻算出顶速，光标一上来就飞；
   ② 不清 pending，松手后还会多滑一下。 */
static void resetMotion(uint32_t now) {
  cursorHoldSince = 0;
  pendingX = 0;
  pendingY = 0;
  lastSend = now;
}

/* 把累加好的位移真正发出一次。
   整数化时舍掉的零头（pendingX -= mx）留在累加器里带到下一次，慢速时不丢位移。 */
static void sendPending(uint32_t now) {
  lastSend = now;
  int mx = iround(pendingX);
  int my = iround(pendingY);
  if (mx >  127) mx =  127;            // 库对超范围值是静默丢弃的，这里先自己钳住
  if (mx < -127) mx = -127;
  if (my >  127) my =  127;
  if (my < -127) my = -127;
  pendingX -= (float)mx;
  pendingY -= (float)my;
  if (mx != 0 || my != 0) mouse.move((int16_t)mx, (int16_t)my);
}

void setup() {
  Serial.begin(115200);

  for (DebInput &d : dirs) pinMode(d.pin, INPUT_PULLUP);
  for (DebInput &b : btns) pinMode(b.pin, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  encPrev = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  ledBegin();          // 灯环初始化：清屏，后续由 ledTask() 刷新

  /* 50Hz(20ms) 一份报告，与 SEND_MS 严格对应。
     ⚠️ 别改回默认的 Hz125：库会按 7.5ms 硬发，主机收不完就丢报告 ⇒ 光标变慢且一顿一顿。
     真要更快就同时改 Hz100(10ms) + TICK_MS 5 + SEND_MS 10，三者必须一起动。 */
  mouse.setUpdateRate(HIDRate::Hz50);
  mouse.begin();
  Serial.println("[Arcade Mouse] 已启动，等待蓝牙配对...");
#if DEBUG_PINS
  Serial.printf("[PINMAP] UP=%d DOWN=%d LEFT=%d RIGHT=%d | L=%d R=%d MID=%d FINE=%d | ENC_A=%d ENC_B=%d\n",
                PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT,
                PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_ENC_SW, PIN_BTN_FINE,
                PIN_ENC_A, PIN_ENC_B);
  dbgPins(true);   // 上电瞬间先印一次：哪个方向“一上电就是按下”，会立刻暴露
#endif
}

void loop() {
  uint32_t now = millis();

  /* --- 输入采样：走 POLL_MS 的快速节拍（比运动节拍快），点击延迟≈消抖窗口本身 --- */
  static uint32_t lastPoll = 0;
  if (now - lastPoll >= POLL_MS) {
    lastPoll = now;
    for (DebInput &d : dirs) pollInput(d, now);
    for (DebInput &b : btns) pollInput(b, now);
  }

  /* --- 灯效刷新：内部按 LED_FRAME_MS 限速，放在 tick 之前，
         保证未配对 / 松手提前 return 时灯环也照常呼吸 --- */
  ledTask(now);

  /* --- 运动 / HID：走 TICK_MS 的固定节拍 --- */
  static uint32_t lastTick = 0;
  if (now - lastTick < TICK_MS) return;
  lastTick = now;

#if DEBUG_PINS
  dbgPins(false);   // 只在该变化时打印；放在配对判断之前，所以核对接线不需要先连蓝牙
#endif

  /* 配对状态变化时清空按键，避免“粘键” */
  bool paired = mouse.isPaired();
  if (wasPaired && !paired) {
    mouse.setButtons(0x00);
    Serial.println("[Arcade Mouse] 连接断开，已释放所有按键");
  } else if (!wasPaired && paired) {
    Serial.println("[Arcade Mouse] 已配对，可以开始操作");
  }
  wasPaired = paired;

  if (!paired) {                 // 未配对前不发任何输入
    encAccum = 0;
    resetMotion(now);
    return;
  }

  /* ------------------------- 鼠标按键 ------------------------- */
  mouse.setButton(MouseButton::Left,   isDown(btns[B_LEFT]));
  mouse.setButton(MouseButton::Right,  isDown(btns[B_RIGHT]));
  mouse.setButton(MouseButton::Middle, isDown(btns[B_MIDDLE]));

  /* ------------------------- 滚轮编码器 → 滚轮 ------------------------- */
  int8_t notches;
  if (encTakeNotches(notches)) {         // 关中断取走“整格数”，保留余数（见 encTakeNotches）
    int sign = ENC_INVERT ? 1 : -1;      // 默认顺时针 = 滚轮向下（库约定：负 = 向下）
    mouse.addScroll((int8_t)(notches * sign));
    ledOnEncoder(notches);               // 灯环上的金色彗星跟着转
#if DEBUG_ENC
    Serial.printf("[ENC] %+d 格 (sign=%d)\n", notches, sign);
#endif
  }

  /* ------------------------- 摇杆 → 光标 ------------------------- */
  int vx = (isDown(dirs[D_RIGHT]) ? 1 : 0) - (isDown(dirs[D_LEFT]) ? 1 : 0);
  int vy = (isDown(dirs[D_DOWN])  ? 1 : 0) - (isDown(dirs[D_UP])   ? 1 : 0);

  if (vx == 0 && vy == 0) {              // 松开
    sendPending(now);                    // 把这一份还没到点、但已经算出来的位移补发掉，
                                         // 否则每次松手都会白丢最后半份（20ms）的行程
    resetMotion(now);                    // 再清零 ⇒ 松手后不再滑
    return;
  }
  /* 「精确模式」是整条曲线的总缩放：连切入补步一起缩，否则点一下仍然会跳很远 */
  float fineMul = isDown(btns[B_FINE]) ? FINE_SCALE : 1.0f;

  if (cursorHoldSince == 0) {            // 方向刚切入的这一 tick：先补一小步
    cursorHoldSince = now;
    pendingX += (float)vx * KICK_PX * fineMul;
    pendingY += (float)vy * KICK_PX * fineMul;
  }

  /* 时间 → 速度：前 CRUISE_MS 恒定低速瞄准，之后按 t² 快速升到 V_MAX。
     t² 的好处是「刚过巡航段时升得慢、越接近顶速升得越猛」，接近鼠标的加速手感。 */
  uint32_t held = now - cursorHoldSince;
  float speed;
  if (held < CRUISE_MS) {
    speed = V_CRUISE;
  } else {
    float t = (held - CRUISE_MS >= ACCEL_TIME_MS)
                ? 1.0f : (float)(held - CRUISE_MS) / (float)ACCEL_TIME_MS;
    speed = V_CRUISE + (V_MAX - V_CRUISE) * t * t;
  }
  speed *= fineMul;

  float dx = (float)vx, dy = (float)vy;
  if (vx != 0 && vy != 0) { dx *= DIAG_SCALE; dy *= DIAG_SCALE; }

  /* 位移先累加，到 SEND_MS 才真正发一次。
     move() 是**覆盖**语义（不是累加），我们每份报告只调一次，正好一一对应不丢步。
     scale = TICK_MS/SEND_MS 把「像素/报告」换算成「像素/本 tick」。 */
  float scale = (float)TICK_MS / (float)SEND_MS;
  pendingX += dx * speed * scale;
  pendingY += dy * speed * scale;

  if (now - lastSend >= SEND_MS) sendPending(now);
}
