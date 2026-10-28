# 接入 Eagler Touhou

AI 是本地队友输入的生产者。作品继续拥有自机更新、伤害、道具、多人规则和录像。

## 每个逻辑帧的顺序

1. 读取本帧输入提交前的游戏状态。
2. 为每个 AI 座位建立自己的 `World`，使用该机体的真实高低速、判定半径、移动边界和无敌时间。
3. 遍历当前有碰撞判定的敌弹、敌机、直线和曲线激光，向 AI 提交危险物。
4. 调用一次 `decide`，将方向、低速和射击映射为作品输入。
5. 把结果提交到该 AI 座位，经过与人类玩家相同的录像与游戏更新路径。

搜索和输入提交必须跟随 **60 Hz 逻辑帧**。144 Hz 显示回调不能额外计算或提交 AI 输入。暂停、对话、录像播放和联网模式停止 AI 决策；新一局、Retry、切换关卡和座位替换调用 `reset`。回放直接读取录制输入，不重新运行 AI。

## 浏览器调用

```js
import {AvoidanceAI} from './avoidance-ai.mjs';

const ai = await AvoidanceAI.load({horizon:12,beamWidth:8});
// 新的一局开始时调用 ai.reset()。
ai.begin({
  active:true,
  player:{x:192,y:380,radius:2,fastSpeed:4,slowSpeed:2,invulnerableFrames:0},
  bounds:{left:8,top:8,right:376,bottom:440},
  goal:{x:192,y:360},
});
ai.hazard({shape:'circle',x:190,y:340,vx:0,vy:3,radius:3});
ai.hazard({shape:'capsule',x:100,y:0,endX:120,endY:448,radius:3,
  activeFrom:8,activeUntil:20});
const result=ai.finish();
// 只对这个 AI 座位提交 result.buttons；下一帧重新读取游戏状态。
```

`finish()` 返回复用对象，保存历史时自行复制。`begin/hazard/finish` 直接写复用的 Wasm 内存，避免每帧另建一个完整的弹幕数组，也不需要每颗子弹调用一次 Wasm 函数。小型适配器也可直接使用 `decide({player,bounds,goal,hazards})`。

`web/eagler-adapter.mjs` 提供 `LocalAiSeat`：作品实现 `world`、`hazards`、`commit`、`release`，并提供自己的按键位映射。调用者提供 `localSession/networked/replay/paused/dialogue/playing/generation` 上下文。默认关闭，只允许明确的本地游玩上下文。它是接入接口，当前没有改动主仓库的启动器或正式游戏。

不要把 AI 按键合并进真人座位。若作品还没有本地多自机更新，需要先增加本地座位；单纯注入人类输入只能构成自动操作，不能构成 AI 队友。

## 危险物

统一使用游戏场内坐标，单位为像素与逻辑帧；角度为弧度。不要从绘制贴图大小反推碰撞大小。

| 类型 | 字段 | 预测 |
| --- | --- | --- |
| 圆弹／敌机圆判定 | x/y、radius、vx/vy、ax/ay | 匀加速，逐帧相对轨迹判定 |
| 旋转矩形 | x/y、halfWidth/halfHeight、angle、angularVelocity、vx/vy、ax/ay | 匀加速、匀角速，旋转范围保守扩张 |
| 直线／曲线激光片段 | x/y、endX/endY、radius、vx/vy、endVx/endVy | 两端独立匀速，胶囊形判定 |

`activeFrom/activeUntil` 为相对当前状态的有效时间，闭区间；当前状态为 0，首次执行后的状态为 1。预警线必须填实际生效时刻，不能直接作为当前实心激光。曲线激光按游戏自己的有效节点分段，节点停用、裁剪区间和碰撞宽度由作品适配器负责。极长激光建议在作品的有效判定长度处分段。

发生反弹、突然变速、追踪或脚本瞬移时，当前匀速模型不包含未来脚本。首版可缩短预判、增加安全余量，或为适配器增加作品专属预测。预测不能推进真实游戏、更改 RNG 或触发生成、消弹、得分逻辑。

TH12 适配时应使用核心 `GameState` / `LaserWorld` 的有效判定状态，而不是 ANM 绘制序列。需要区分出生中／正常弹，直线激光生效段和曲线节点；玩家机体、Bomb 减速、关底减速应进入实际每帧速度。UFO 收集、擦弹、追踪道具和放雷策略属于之后独立的目标层。

## 有限工作量

默认每个 AI 每次决策：12 帧，8 条保留路线，动作保持 3 帧；最多 40,000 次精细判定、120,000 次树节点访问，最多 2,048 个相关危险物。硬上限为 24 帧、16 条路线和 4,096 个输入危险物。原生实例独立，Web 模块可同时容纳 8 个实例。

多队友应**拆分团队总预算**，不能每增加一个队友就复制一份全量预算。演示页共享 40,000 次判定和 120,000 次树访问预算。

| flags | 解释 | 接入处理 |
| --- | --- | --- |
| 1 | 计算预算已用尽 | 返回已验证的最长前缀；观察 completedFrames / safeFrames 后调预算 |
| 2 | 危险物超量、非法或快照缺项 | 默认停止接管并通知，不能声称这条路径安全 |
| 4 | 未找到能走完预判窗口的路线 | 可返回已验证前缀，后续可单独加入放雷策略 |
| 8 | 自机、边界等快照无效 | 停止接管 |
| 16 | 不在有效游玩状态 | 释放该座位输入 |

固定运算次数是可重复的工作量边界，**不是毫秒级性能保证**。演示页显示本机实际计算用时、预算耗尽次数和中弹次数，供人工调整。当前未做运行测试或性能测量。

## C++ 与打包

```cmake
add_subdirectory(/path/to/eagler-touhou-ai ai)
target_link_libraries(your_title PRIVATE eagler::ai)
```

调用 `<eagler/ai/Planner.hpp>` 的 `Planner::decide(World)`，无需 SDL、浏览器或网络依赖。浏览器分发 `web/avoidance-ai.mjs`、`web/eagler-adapter.mjs` 和构建生成的 `web/runtime/avoidance.mjs/.wasm`；使用同源静态托管。主仓库可将此库纳入 `common`，或固定来源提交作为独立依赖。
