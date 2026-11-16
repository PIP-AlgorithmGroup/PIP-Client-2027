#pragma once

#include <memory>

#include "input/input_state.h"

class QWindow;

/** 原生输入适配器；构造/析构仅 GUI 线程，独立线程直接维护共享输入状态。
 * window 必须在本对象之后销毁原生 surface；析构停止捕获、恢复指针并 join。
 * 平台资源局限在实现中；不读取 QML，不创建第二个 MQTT 连接。
 */
class NativeInput {
   public:
    /** GUI 线程提取窗口句柄；不支持的显示插件报告诊断且不捕获。 */
    NativeInput(QWindow& window, std::shared_ptr<InputState> state);
    /** 有界轮询退出；先恢复系统光标/约束再释放原生输入资源。 */
    ~NativeInput();
    NativeInput(const NativeInput&) = delete;
    NativeInput& operator=(const NativeInput&) = delete;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
