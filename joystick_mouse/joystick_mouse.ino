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
 *  说明：旧的 ESP32-BLE-Mouse(T-vK) 在 core 3.x 上编译不过，不要用。
 *
 *  功能：
 *    - 摇杆推方向 → 光标按“加速度斜坡”移动：轻推慢、推得越久越快；压两个微动可走斜向
 *    - 左键 / 右键按钮（按住不放即为拖动）
 *    - 滚轮旋转 → 滚轮；滚轮按下 → 中键
 *    - 按住“精确模式”键 → 光标降为 35% 速度（可选键）
 *
 *  装好后必须校准（详见下方参数）：
 *    - 滚轮方向：顺时针转一格，若与预期相反 → ENC_INVERT 改 1
 *    - 滚轮步进：一格跳太多/太少 → 调 ENC_STEP（默认 4）
 *    - 手感：太飘就降 CURSOR_MAX_SPEED，太慢就升
 * ==========================================================================*/

#include <HijelHID_BLEMouse.h>

/* ------------------------------- 引脚 ------------------------------- */
#define PIN_JOY_UP        25
#define PIN_JOY_DOWN      26
#define PIN_JOY_LEFT      27
#define PIN_JOY_RIGHT     14
#define PIN_BTN_LEFT      13
#define PIN_BTN_RIGHT      4
#define PIN_ENC_A         32
#define PIN_ENC_B         33
#define PIN_ENC_SW        16
#define PIN_BTN_FINE      18

/* ------------------------------- 参数 ------------------------------- */
#define TICK_MS            8        // 主循环节拍 ≈125Hz，与 BLE HID 报告率对齐
#define DEBOUNCE_MS        6        // 微动开关消抖时间
#define CURSOR_MIN_SPEED   2        // 光标起速（像素/报告）
#define CURSOR_MAX_SPEED   22       // 光标最高速（像素/报告）
#define ACCEL_TIME_MS      700      // 从起速升到最高速所需按住时长
#define FINE_SCALE         0.35f    // 精确模式下的速度系数
#define DIAG_SCALE         0.7071f  // 斜向速度补偿（1/√2，避免斜着走更快）
#define ENC_STEP           4        // 四倍频：4 个电平跳变 = 滚轮 1 格
#define ENC_INVERT         0        // 滚轮方向反了就把这里改成 1
#define DEBUG_ENC          1        // 校准滚轮时打开，串口会打印每格方向；定型后可改 0

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

static int iround(float v) { return (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

/* --------------------- 滚轮编码器（中断 + 四倍频） ------------------ */
// 格雷码状态机：把 A/B 的每次跳变按方向累加，机械抖动产生的非法跳变计 0。
// 机械式与光学式输出都是方波，解码逻辑通用。
static const int8_t QTAB[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
volatile int8_t  encAccum = 0;   // 累积跳变数，正负代表旋转方向
volatile uint8_t encPrev  = 0;

static void IRAM_ATTR encISR() {
  uint8_t cur = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encAccum += QTAB[(encPrev << 2) | cur];
  encPrev = cur;
}

/* ------------------------------- 状态 ------------------------------- */
static uint32_t cursorHoldSince = 0;   // 光标方向开始按住的时刻
static bool     wasPaired        = false;

void setup() {
  Serial.begin(115200);

  for (DebInput &d : dirs) pinMode(d.pin, INPUT_PULLUP);
  for (DebInput &b : btns) pinMode(b.pin, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  mouse.setUpdateRate(HIDRate::Hz125);   // 125Hz，BLE 允许的最快报告率
  mouse.begin();
  Serial.println("[Arcade Mouse] 已启动，等待蓝牙配对...");
}

void loop() {
  static uint32_t lastTick = 0;
  uint32_t now = millis();
  if (now - lastTick < TICK_MS) return;   // 固定节拍
  lastTick = now;

  for (DebInput &d : dirs) pollInput(d, now);
  for (DebInput &b : btns) pollInput(b, now);

  /* 配对状态变化时清空按键，避免“粘键” */
  bool paired = mouse.isPaired();
  if (wasPaired && !paired) {
    mouse.setButtons(0x00);
    Serial.println("[Arcade Mouse] 连接断开，已释放所有按键");
  } else if (!wasPaired && paired) {
    Serial.println("[Arcade Mouse] 已配对，可以开始操作");
  }
  wasPaired = paired;
  if (!paired) { encAccum = 0; return; }   // 未配对前不发任何输入

  /* ------------------------- 鼠标按键 ------------------------- */
  mouse.setButton(MouseButton::Left,   isDown(btns[B_LEFT]));
  mouse.setButton(MouseButton::Right,  isDown(btns[B_RIGHT]));
  mouse.setButton(MouseButton::Middle, isDown(btns[B_MIDDLE]));

  /* ------------------------- 滚轮编码器 → 滚轮 ------------------------- */
  int8_t acc = encAccum;                  // int8_t 单字节读写是原子的，无需关中断
  if (acc >= ENC_STEP || acc <= -ENC_STEP) {
    int8_t notches = acc / ENC_STEP;              // 正 = 顺时针
    encAccum = acc - notches * ENC_STEP;          // 保留余数，避免慢转丢步
    int sign = ENC_INVERT ? 1 : -1;               // 默认顺时针 = 滚轮向下
    mouse.addScroll((int8_t)(notches * sign));
#if DEBUG_ENC
    Serial.printf("[ENC] %+d 格 (acc=%d, sign=%d)\n", notches, acc, sign);
#endif
  }

  /* ------------------------- 摇杆 → 光标 ------------------------- */
  int vx = (isDown(dirs[D_RIGHT]) ? 1 : 0) - (isDown(dirs[D_LEFT]) ? 1 : 0);
  int vy = (isDown(dirs[D_DOWN])  ? 1 : 0) - (isDown(dirs[D_UP])   ? 1 : 0);

  if (vx == 0 && vy == 0) { cursorHoldSince = 0; return; }
  if (cursorHoldSince == 0) cursorHoldSince = now;

  uint32_t held = now - cursorHoldSince;
  float t = (held >= ACCEL_TIME_MS) ? 1.0f : (float)held / (float)ACCEL_TIME_MS;
  float speed = CURSOR_MIN_SPEED + t * (CURSOR_MAX_SPEED - CURSOR_MIN_SPEED);
  if (isDown(btns[B_FINE])) speed *= FINE_SCALE;

  float dx = (float)vx, dy = (float)vy;
  if (vx != 0 && vy != 0) { dx *= DIAG_SCALE; dy *= DIAG_SCALE; }

  int mx = iround(dx * speed);
  int my = iround(dy * speed);
  if (mx != 0 || my != 0) mouse.move(mx, my);
}
