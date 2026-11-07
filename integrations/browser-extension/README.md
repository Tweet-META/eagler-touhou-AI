# 在自己的主站页面使用 AI

这个 Edge／Chrome 扩展在自己的红魔乡多人运行页加载 AI 运行包，接管自己已加入房间的座位。主站和队友保持原版本；按键通过原联机流程发送，房间人数、伤害、资源与判定规则沿用作品。

## 使用

1. 打开 `edge://extensions` 或 `chrome://extensions`，开启“开发人员模式”。
2. 点“加载解压缩的扩展”，选择本仓库的 `build/browser-extension` 文件夹。
3. 打开或刷新 `https://touhouonline.com/`，从主站重新启动红魔乡多人游戏，照常进入房间。
4. 游戏右上角出现“手动操作 · F8”后，点击它或按 F8 开启接管。扩展图标里可选择实力、跟随／分别压制和高级参数。

AI 初始关闭，只接管自己的方向、低速、射击与放雷。菜单、对话和暂停仍由本人操作。关闭接管后恢复正常设备输入；新运行页重新从关闭状态开始。切回原运行包时关闭扩展并重新启动游戏。

当前入口支持 `touhouonline.com`、兼容旧域名 `touhou.vip`，也支持两者的子域以及本机 `127.0.0.1`／`localhost` 的红魔乡多人运行页。更新扩展后，在扩展管理页点一次重新加载，再刷新主站并重新启动游戏。其他作品、单人页和观战回放没有 AI 接管。构建在打包前验证加载器能用随包的 Wasm 导出 AI 接口。真实双端联机由使用者检查。

## 构建

准备 Emscripten、CMake、Ninja，以及 SDL、SDL_image、SDL_ttf、FreeType。已构建本地红魔乡试玩版时，脚本复用 `build/deps` 中固定版本的依赖。

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/Ritosa2333/eagler-touhou.git build/netplay-upstream
git -C build/netplay-upstream sparse-checkout set common ports/th06
npm run build:th06-online -- --source=build/netplay-upstream --emsdk=/path/to/emsdk --unifont=/path/to/unifont.otf
```

`--cmake`、`--ninja`、`--jobs` 可指定工具位置和并发数。提供 `--unifont` 启用原生 thcrap 语言支持；字体继续由主站安装，不打进扩展。省略时构建原文运行包。

输出位于 `build/browser-extension`，包含运行代码与 Wasm，不包含原版 DAT、字体、音乐或录像。脚本复制上游红魔乡源码到 `build/th06-online/source`，在副本中安装输入采集钩子，不修改上游工作目录。

## 客户端输入路径

- 扩展在目标 iframe 的 `document_start` 注册内核加载钩子；主站建好画布、Module、消息接口和资源缓存后，在模板替换游戏脚本的位置加载本地内核。保留原页面、URL、主站初始化代码、epoch、房间与存档接口。
- 资源从主站已有的 DATA 中读取，按原运行包资源表校验后挂载；不执行原脚本，不向扩展后台传送游戏数据，不解包到存档目录。
- `Th06Ai::captureNetplay` 在 `CaptureLocalInput` 中生成本机所属座位的按键。输入仍进入 `ScheduleLocalInput` 和原网络发送流程。
- 以会话代次及采集帧缓存实际输入。同一帧不会因重新采样、重发或参数变化被改写；重演／录像播放不运行 AI，也不推进 AI 的延迟队列和动作锁定。
- AI 的预测状态位于客户端适配器，不加入游戏的回滚快照；只改变输入来源，不直接写入玩家坐标、敌弹、掉落、资源或游戏 RNG。
- 实力、动作持续、延迟与评分参数沿用本地试玩版；弱弱？！跟随时保留输出站位权重 2。分别压制且无 Boss 时关闭边缘惩罚，Boss 出现后恢复设置值。

联机兼容性以运行包的原生 gameplay ABI 为准。站点升级玩法或房间接口后，按对应上游版本重新构建扩展。
