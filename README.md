# TweakSettings（简体中文 / Rootless）

这是基于 [CreatureSurvive/TweakSettings](https://github.com/CreatureSurvive/TweakSettings) 的个人维护分支，增加简体中文界面，并针对 Rootless 越狱环境构建 arm64e 安装包。

> 本分支的目标是尽量保持上游原有功能，只维护必要的中文化、Rootless 构建和兼容性修复。

## 当前维护状态

- 维护分支：`chinese-rootless`
- 上游分支：`master`（保留上游同步用途，不在此分支直接开发）
- 构建方式：GitHub Actions
- 构建目标：Rootless / `iphoneos-arm64e`
- 中文语言：简体中文（`zh_CN`）
- 当前控制文件版本：以 `layout/DEBIAN/control` 为准。

### 已验证情况

| 版本 | 验证情况 |
| --- | --- |
| 1.0.8 | 可安装、可打开；中文正常，但操作功能无效，不建议使用 |
| 1.1.0 | 用户实机验证：中文及常规功能正常；插件注入开关仍有问题 |
| 1.1.1 / 1.1.2 | 曾进行后续修复尝试，尚不能视为用户实机验证通过 |

**注意：能成功构建不等于已通过设备实测。** 在插件注入开关完成实机验证前，不要把 1.1.2 标记为稳定版。

## 项目结构

- `TweakSettings-App/`：iOS 应用界面及功能
- `TweakSettings-Utility/`：特权操作辅助程序
- `Resources/`：应用资源和权限配置
- `Frameworks/`：编译所需的系统框架接口
- `layout/`：Debian 安装包目录及控制信息
- `.github/workflows/build-rootless.yml`：Rootless arm64e 自动构建
- `Releases/`：上游仓库原有的历史安装包，不作为本分支最新构建产物

## 构建

1. 打开 GitHub 仓库的 **Actions**。
2. 选择 **Build Rootless DEB**（对应 `.github/workflows/build-rootless.yml`）。
3. 选择 **Run workflow**，分支选择 `chinese-rootless`。
4. 构建成功后，在该次运行的 **Artifacts** 下载 `TweakSettings-zh_CN-rootless-arm64e`。
5. 解压 Artifact，取得 DEB 安装包。

每次构建都应从对应的 Actions 运行记录下载产物；不要把旧版本 DEB 误当成最新包。

## 维护约定

1. 日常修改只提交到 `chinese-rootless`，`master` 仅用于保留和同步上游代码。
2. 一次提交尽量只解决一个问题，并使用能说明改动内容的提交信息。
3. 修复前先以最后一个用户实测可用版本为基准，避免顺手重构正常功能。
4. 新版本必须经过构建检查和设备实测后，才能标记为稳定。
5. 不把临时测试包、重复构建产物或失败构建文件提交到源码目录；正式构建产物保留在 Actions Artifacts 或 GitHub Releases。
6. 不随意删除历史提交、上游文件或旧版本资料；清理前先确认用途。

## 上游与版权

本项目基于 CreatureSurvive 的 TweakSettings。原作者及上游项目版权信息以原仓库为准：
[CreatureSurvive/TweakSettings](https://github.com/CreatureSurvive/TweakSettings)。
