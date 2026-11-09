# 在自己的主站页面使用 AI

这个 Edge／Chrome 扩展在自己的多人运行页加载 AI 运行包，接管自己已加入房间的座位。按键通过原联机流程发送，房间人数、伤害、资源与判定规则沿用作品。

扩展支持红魔乡（TH06），并提供永夜抄（TH08）、风神录（TH10）、地灵殿（TH11）、星莲船（TH12）和绀珠传（TH15）的模块运行包。妖妖梦（TH07）使用仓库里的本地试玩入口。

## 使用

1. 打开 `edge://extensions` 或 `chrome://extensions`，开启“开发人员模式”。
2. 点“加载解压缩的扩展”，选择本仓库的 `build/browser-extension` 文件夹。
3. 打开或刷新 [TouhouOnline](https://touhouonline.com/)，从主站重新启动对应作品的多人游戏，照常进入房间。
4. 点击游戏右上角的“手动操作”开启接管，红魔乡也支持 F8。扩展图标里可选择实力、跟随／分别压制和高级参数。

AI 初始关闭，只接管自己的方向、低速、射击与放雷。菜单、对话和暂停仍由本人操作。关闭接管后恢复正常设备输入；新运行页重新从关闭状态开始。切回原运行包时关闭扩展并重新启动游戏。

入口支持 `touhouonline.com`、旧域名 `touhou.vip`、两者的子域以及本机 `127.0.0.1`／`localhost`。更新扩展后，在扩展管理页点一次重新加载，再刷新主站并重新启动游戏。模块运行包只匹配构建时指定的主站版本。真实双端联机由使用者检查。

## 构建

准备 Emscripten、CMake、Ninja，以及 SDL、SDL_image、SDL_ttf、FreeType。已构建本地红魔乡试玩版时，脚本复用 `build/deps` 中固定版本的依赖。

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/Ritosa2333/eagler-touhou.git build/netplay-upstream
git -C build/netplay-upstream sparse-checkout set common launcher tools/build ports/th06 ports/th08-mp ports/th10-mp ports/th11-mp ports/th12-mp ports/th15-mp
npm run build:th06-online -- --source=build/netplay-upstream --emsdk=/path/to/emsdk --unifont=/path/to/unifont.otf
```

`--cmake`、`--ninja`、`--jobs` 可指定工具位置和并发数。提供 `--unifont` 启用原生 thcrap 语言支持；字体继续由主站安装，不打进扩展。省略时构建原文运行包。

五个模块运行包分别构建一次。以永夜抄为例，在主站的 `runtime-manifest.json` 中找到 `root` 为 `runtime/th08/multiplayer/` 的分组，将 `--generation` 设为该分组的 `current.generation`，并使用对应上游源码。

```sh
npm run build:online -- --game=th08 --generation="MULTIPLAYER_GENERATION" --source=build/netplay-upstream --emsdk=/path/to/emsdk
```

将 `--game` 换成 `th10`、`th11`、`th12` 或 `th15` 即可构建对应作品。省略 `--generation` 时只匹配不带版本号的本地页面。构建检查生成的 Wasm 和加载器是否都提供 AI 接口，所有已构建作品汇总到 `build/browser-extension`。产物是运行代码与 Wasm，游戏资源由主站提供。脚本在 `build/` 下的源码副本中安装钩子。

## 客户端输入路径

- 扩展在目标 iframe 的 `document_start` 注册内核加载钩子。红魔乡沿用原脚本替换入口，其余五作通过 import map 将当前版本的 Wasm 模块加载器指向扩展。原页面负责资源挂载、房间和存档。
- 各作在原生联机输入采集点读取当前玩家、弹幕、激光和道具，并让 AI 生成自己的按键。预测使用原作的方形或圆形碰撞尺寸，结合当前速度外推。弯折、变形或脚本生成的后续弹幕仍可能超出预测。
- 采集后的按键由原联机流程排队发送。回滚重演沿用已采集输入，录像播放和观战不会重新计算 AI 输入。
- AI 的预测状态位于客户端适配器，不加入游戏的回滚快照；只改变输入来源，不直接写入玩家坐标、敌弹、掉落、资源或游戏 RNG。
- 实力、动作持续、延迟与评分参数沿用本地试玩版；弱弱？！跟随时保留输出站位权重 2。分别压制且无 Boss 时关闭边缘惩罚，Boss 出现后恢复设置值。

联机兼容性以运行包的原生 gameplay ABI 为准。站点升级玩法或房间接口后，按对应上游版本重新构建扩展。
