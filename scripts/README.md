# 维护脚本

此目录仅存放持续维护的构建、打包或检查脚本。资源制作程序放在 `tools/`；
单次迁移和临时探针放在忽略的 `.cache/validation/<主题>`，不作为正式工具提交。
产品构建和回归使用 CMake / CTest，基础压缩包使用 CPack。
`package_linux.py` 将已部署目录封装为 Ubuntu DEB，不在系统中安装；
用法、Windows 安装模板和发行验收见[发行流程](../docs/development/releases.md)。
