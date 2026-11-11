#pragma once

/**
 * 在 QGuiApplication 创建后、QML engine 创建前注册随包中文无衬线字体并设置应用默认字体。
 * 仅在 GUI 线程调用一次；字体数据由 Qt 字体数据库持有至应用销毁，不依赖系统字体安装。
 * 返回 false 表示资源缺失或字体无法注册，调用者应停止启动并记录错误。
 */
bool initializeUiFonts();
