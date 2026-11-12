# 五作的本地合作预览

运行 `npm start`，打开 [本地预览](http://127.0.0.1:8144/preview.html)。选择作品、难度与两名玩家的机体，然后点击开始。P1 使用方向键、Z、Shift、X 和 Esc，P2 为 AI。AI 实力与配合方式可随时调整，高级选项保留各项参数。

先使用 Emscripten 构建每作的本地预览内核。

```sh
npm run build:preview -- --game=th08 --source=/path/to/eagler-touhou --emsdk=/path/to/emsdk
npm run package:preview -- --game=th08 --assets=build/preview-assets.json
```

作品参数支持 `th08`、`th10`、`th11`、`th12`、`th15`。资源配置使用本机路径，例如

```json
{
  "fonts": "/private/fonts",
  "titles": {
    "th08": {"archive": "/private/th08.dat"},
    "th10": {"archive": "/private/th10.dat"},
    "th11": {"archive": "/private/th11.dat"},
    "th12": {"archive": "/private/prepared/th12.data"},
    "th15": {"archive": "/private/th15.dat"}
  }
}
```

字体目录使用上游 SDL 打包所需的 `msgothic.ttc`、`unifont.otf`、`simhei.ttf`、`blend.bin`、`codepages.bin` 和 `cp932.bin`。星莲船使用上游准备好的 DATA。其他四作使用原版日文 DAT。可给各作配置 `music` 数组，每项包含本机 `file` 和原生挂载 `path`，省略时使用无音乐模式。

构建在独立的 `build/preview-modules` 源码副本中安装本地输入通道。真人输入和 AI 输入在同一帧提交给原生多人输入历史，然后进入原来的模拟和结算流程。预览内核在本地直接准备两人会话，服务器连接入口仅作为本地启动流程使用。扩展的线上构建使用 `build/online-modules`。

本机游戏资源、打包文件及字体保存在 Git 忽略的 `web/previews` 下。浏览器存档按各作原生存储命名空间保存。
