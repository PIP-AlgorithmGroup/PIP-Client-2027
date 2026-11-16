#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMutex>
#include <QProcess>
#include <QScopeGuard>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <thread>

#include "support/video_test_packets.h"
#include "video/hevc_decoder.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

using Microsoft::WRL::ComPtr;

namespace {
qint64 nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 实验只支持 SDR 8bit NV12、BT.601/709（未标注 HD 按 BT.709 解释）。
// 全屏三角形在 GPU 完成转色；没有 CPU 像素上传、纹理数组 slice 误采样或软件回退。
constexpr char kShader[] = R"(
Texture2D<float> yPlane : register(t0);
Texture2D<float2> uvPlane : register(t1);
SamplerState sampleLinear : register(s0);
cbuffer Color : register(b0) { float4 ranges; float4 coefficients; };
struct Vertex { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.pos = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float4 ps(Vertex v) : SV_Target {
    float y = (yPlane.Sample(sampleLinear, v.uv) - ranges.x) * ranges.y;
    float2 uv = (uvPlane.Sample(sampleLinear, v.uv) - ranges.z) * ranges.w;
    return float4(y + coefficients.x*uv.y,
                  y - coefficients.y*uv.x - coefficients.z*uv.y,
                  y + coefficients.w*uv.x, 1);
})";

/** Windows 手动实验的独立呈交器；全部方法及析构仅在建窗的工作线程运行。
 * 借用 FFmpeg device/context，但通过同一设备锁串行化 immediate context。
 * 交换链/平面纹理拥有 COM 引用；源 AVFrame 保持至复制提交，Flush 后由同 context 排序。
 * 不是产品后端：无 HUD、缩放重建、HDR、设备丢失恢复或跨平台实现。
 */
class Presenter {
   public:
    ~Presenter() {
        if (window) DestroyWindow(window);
    }
    HWND window = nullptr;
    QString error;
    int validation_error = -1;

    bool initialize(const VideoFrame& frame, const QRect& geometry) {
        if (!frame.pixels || frame.pixels->format != AV_PIX_FMT_D3D11 ||
            !frame.pixels->hw_frames_ctx)
            return fail("要求真实 D3D11VA 输出，禁止 CPU 回退");
        const auto* pixels = frame.pixels.get();
        auto* frames = reinterpret_cast<AVHWFramesContext*>(pixels->hw_frames_ctx->data);
        device_ = static_cast<AVD3D11VADeviceContext*>(frames->device_ctx->hwctx);
        context_ = device_->device_context;
        auto* source = reinterpret_cast<ID3D11Texture2D*>(pixels->data[0]);
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_NV12) return fail("实验只接受 NV12");
        size_ = frame.size();
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PipNativePresentProbe";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return fail("原生窗口类注册失败");
        window = CreateWindowExW(0, wc.lpszClassName, L"PIP independent video probe",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 geometry.x(), geometry.y(), geometry.width(), geometry.height(),
                                 nullptr, nullptr, wc.hInstance, nullptr);
        if (!window) return fail("原生窗口创建失败");
        RECT client{};
        GetClientRect(window, &client);
        width_ = client.right;
        height_ = client.bottom;
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(device_->device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
            FAILED(dxgi_device->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
            return fail("无法获取解码设备对应的 DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = width_;
        swap_desc.Height = height_;
        swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChainForHwnd(device_->device, window, &swap_desc, nullptr,
                                                   nullptr, &swap_)))
            return fail("flip-discard 交换链创建失败");
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back_))) ||
            FAILED(device_->device->CreateRenderTargetView(back_.Get(), nullptr, &target_)))
            return fail("交换链输出视图创建失败");
        desc.Width = size_.width();
        desc.Height = size_.height();
        desc.ArraySize = desc.MipLevels = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device_->device->CreateTexture2D(&desc, nullptr, &local_)))
            return fail("NV12 采样纹理创建失败");
        for (int i = 0; i < 2; ++i) {
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = i ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8_UNORM;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            if (FAILED(device_->device->CreateShaderResourceView(local_.Get(), &view, &planes_[i])))
                return fail("NV12 平面视图创建失败");
        }
        ComPtr<ID3DBlob> vs, ps, messages;
        if (FAILED(D3DCompile(kShader, sizeof(kShader), nullptr, nullptr, nullptr, "vs", "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &messages)) ||
            FAILED(D3DCompile(kShader, sizeof(kShader), nullptr, nullptr, nullptr, "ps", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &messages)) ||
            FAILED(device_->device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                                       nullptr, &vs_)) ||
            FAILED(device_->device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                                      nullptr, &ps_)))
            return fail("原生 YUV shader 编译/创建失败");
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->device->CreateSamplerState(&sampler, &sampler_)))
            return fail("采样器创建失败");
        D3D11_BUFFER_DESC color{};
        color.ByteWidth = 32;
        color.Usage = D3D11_USAGE_DEFAULT;
        color.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->device->CreateBuffer(&color, nullptr, &color_)))
            return fail("色彩参数缓冲创建失败");
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        return true;
    }

    bool present(const VideoFrame& frame) {
        if (frame.size() != size_) return fail("实验不支持运行中变更规格");
        if (!frame.pixels || frame.pixels->format != AV_PIX_FMT_D3D11 ||
            !frame.pixels->hw_frames_ctx)
            return fail("稳态硬件路径已失效，禁止 CPU 回退");
        const auto* pixels = frame.pixels.get();
        auto* pool = reinterpret_cast<AVHWFramesContext*>(pixels->hw_frames_ctx->data);
        if (static_cast<AVD3D11VADeviceContext*>(pool->device_ctx->hwctx)->device !=
            device_->device)
            return fail("实验不支持运行中切换解码设备");
        const bool is_601 =
            pixels->colorspace == AVCOL_SPC_BT470BG || pixels->colorspace == AVCOL_SPC_SMPTE170M;
        if (!is_601 && pixels->colorspace != AVCOL_SPC_BT709 &&
            pixels->colorspace != AVCOL_SPC_UNSPECIFIED)
            return fail("实验仅接受 BT.601/709 SDR");
        const bool is_full = pixels->color_range == AVCOL_RANGE_JPEG;
        const float kr = is_601 ? .299f : .2126f, kb = is_601 ? .114f : .0722f;
        const float kg = 1 - kr - kb;
        const float color[8]{is_full ? 0.f : 16.f / 255.f,
                             is_full ? 1.f : 255.f / 219.f,
                             128.f / 255.f,
                             is_full ? 1.f : 255.f / 224.f,
                             2 * (1 - kr),
                             2 * kb * (1 - kb) / kg,
                             2 * kr * (1 - kr) / kg,
                             2 * (1 - kb)};
        device_->lock(device_->lock_ctx);
        auto unlock = qScopeGuard([&] { device_->unlock(device_->lock_ctx); });
        const D3D11_BOX crop{0, 0, 0, UINT(size_.width()), UINT(size_.height()), 1};
        context_->CopySubresourceRegion(
            local_.Get(), 0, 0, 0, 0, reinterpret_cast<ID3D11Texture2D*>(frame.pixels->data[0]),
            UINT(reinterpret_cast<intptr_t>(frame.pixels->data[1])), &crop);
        auto* target = target_.Get();
        ID3D11ShaderResourceView* views[]{planes_[0].Get(), planes_[1].Get()};
        auto* sampler = sampler_.Get();
        context_->OMSetRenderTargets(1, &target, nullptr);
        const D3D11_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
        context_->RSSetViewports(1, &viewport);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(), nullptr, 0);
        context_->PSSetShader(ps_.Get(), nullptr, 0);
        context_->PSSetShaderResources(0, 2, views);
        context_->PSSetSamplers(0, 1, &sampler);
        context_->UpdateSubresource(color_.Get(), 0, nullptr, color, 0, 0);
        auto* color_buffer = color_.Get();
        context_->PSSetConstantBuffers(0, 1, &color_buffer);
        context_->Draw(3, 0);
        // 仅首帧在测量阶段前下载输出作颜色验证，稳态帧没有 Map/CPU 下载。
        if (validation_error < 0) {
            auto capture = captureOutput();
            device_->unlock(device_->lock_ctx);
            const auto reference = frame.toImage();
            device_->lock(device_->lock_ctx);
            if (capture.isNull() || reference.isNull()) return fail("首帧参考下载失败");
            validation_error = 0;
            for (auto p : {QPointF(.2, .2), QPointF(.8, .2), QPointF(.2, .8), QPointF(.8, .8)}) {
                const auto actual = capture.pixelColor(int(width_ * p.x()), int(height_ * p.y()));
                const auto expected = reference.pixelColor(int(reference.width() * p.x()),
                                                           int(reference.height() * p.y()));
                for (auto delta : {actual.red() - expected.red(), actual.green() - expected.green(),
                                   actual.blue() - expected.blue()})
                    validation_error = qMax(validation_error, qAbs(delta));
            }
            if (validation_error > 8) return fail("首帧 GPU/CPU 参考颜色误差超标");
        }
        const auto result = swap_->Present(0, 0);
        context_->Flush();
        if (FAILED(result)) return fail(QString("Present 失败 %1").arg(quint32(result), 0, 16));
        return true;
    }

   private:
    bool fail(const QString& message) {
        error = message;
        return false;
    }
    QImage captureOutput() {
        D3D11_TEXTURE2D_DESC desc{};
        back_->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->device->CreateTexture2D(&desc, nullptr, &staging))) return {};
        context_->CopyResource(staging.Get(), back_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
        const auto result = QImage(static_cast<const uchar*>(mapped.pData), width_, height_,
                                   mapped.RowPitch, QImage::Format_ARGB32)
                                .copy();
        context_->Unmap(staging.Get(), 0);
        return result;
    }
    AVD3D11VADeviceContext* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    QSize size_;
    int width_ = 0, height_ = 0;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11Texture2D> local_, back_;
    ComPtr<ID3D11RenderTargetView> target_;
    ComPtr<ID3D11ShaderResourceView> planes_[2];
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> color_;
};
}  // namespace

/** 受控架构实验：命令参数 stream.hevc output.json cover.exe stage_ms。
 * 直接定时向同一 HevcDecoder 喂 AU，绕过 UDP/重组；不能把 source→Present 当首包延迟。
 * GUI 只安排窗口阶段；解码与原生建窗/呈交各有独立线程和单槽邮箱。
 * 不接入产品，不声明原生 Ubuntu/Intel/HDR 通过；输出是 Present 返回而非真实亮屏。
 */
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 5) return 2;
    QFile input(args[1]);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024 * 1024) return 2;
    const auto units = video_test::audUnits(input.readAll());
    const int stage_ms = args[4].toInt();
    if (units.isEmpty() || stage_ms < 5000 || stage_ms > 30000 || !QFile::exists(args[3])) return 2;
    const auto available = app.primaryScreen()->availableGeometry();
    const QRect geometry(available.topLeft() + QPoint(30, 30), QSize(960, 600));
    const HANDLE signal = CreateEventW(nullptr, false, false, nullptr);
    if (!signal) return 2;
    const auto close_signal = qScopeGuard([&] { CloseHandle(signal); });
    QMutex mutex;
    VideoFrame latest;
    quint64 revision = 0;
    std::atomic<HWND> window{nullptr};
    std::atomic<bool> validated{false};
    QVariantList frames, sent_times, phases, states;
    QString decoder_error, render_error, backend;
    int validation_error = -1;
    // 两个线程只写各自的结果容器；主线程在 join 后才读取。邮箱及 revision 单独持锁。
    std::jthread source([&](std::stop_token stop) {
        HevcDecoder decoder("d3d11va");
        const auto timer =
            CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer) {
            decoder_error = "高精度发送计时器创建失败";
            return;
        }
        const auto close_timer = qScopeGuard([&] { CloseHandle(timer); });
        QFile validation(PIP_VIDEO_HD_FIXTURE);
        if (!validation.open(QIODevice::ReadOnly)) {
            decoder_error = "无法读取独立颜色验证码流";
            return;
        }
        // 静态四象限避免动态图案边缘缩放造成参考坐标误差；校验和下载全部发生于阶段前。
        auto first = decoder.decode(validation.readAll());
        if (first.isNull()) {
            decoder_error = decoder.error();
            return;
        }
        {
            QMutexLocker lock(&mutex);
            latest = std::move(first);
            ++revision;
        }
        SetEvent(signal);
        for (int i = 0; i < 200 && !validated && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!validated) return;
        decoder.reset();
        qint64 next = nowUs();
        quint64 id = 0;
        while (!stop.stop_requested()) {
            const auto began = nowUs();
            sent_times.append(began);
            auto frame = decoder.decode(units[id % units.size()], began, began, int(id % 65536));
            ++id;
            if (!frame.isNull()) {
                frame.decoded_us = nowUs();
                backend = decoder.backend();
                {
                    QMutexLocker lock(&mutex);
                    latest = std::move(frame);
                    ++revision;
                }
                SetEvent(signal);
            }
            next += 16667;
            const auto remaining = next - nowUs();
            if (remaining > 0) {
                LARGE_INTEGER due;
                due.QuadPart = -remaining * 10;
                if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, false) ||
                    WaitForSingleObject(timer, 100) != WAIT_OBJECT_0) {
                    decoder_error = "高精度发送等待失败";
                    return;
                }
            }
        }
        if (!decoder.hardwareActive()) decoder_error = decoder.error();
    });
    std::jthread render([&](std::stop_token stop) {
        Presenter presenter;
        VideoFrame retained;
        quint64 presented = 0;
        while (!stop.stop_requested()) {
            MsgWaitForMultipleObjectsEx(1, &signal, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            quint64 current = 0;
            {
                QMutexLocker lock(&mutex);
                retained = latest;
                current = revision;
            }
            if (retained.isNull() || current == presented) continue;
            if (!presenter.window) {
                if (!presenter.initialize(retained, geometry)) break;
                window = presenter.window;
            }
            if (IsIconic(presenter.window)) continue;
            const auto picked = nowUs();
            if (!presenter.present(retained)) break;
            validated = true;
            const auto swapped = nowUs();
            frames.append(QVariantMap{{"firstUs", retained.first_packet_us},
                                      {"decodedUs", retained.decoded_us},
                                      {"pickedUs", picked},
                                      {"swapUs", swapped},
                                      {"frameId", retained.frame_id},
                                      {"skipped", current - presented - 1}});
            presented = current;
        }
        render_error = presenter.error;
        validation_error = presenter.validation_error;
        window = nullptr;
        // 保留最后一个 AVFrame，使 presenter 释放 COM/窗口前 FFmpeg 设备仍存活。
    });
    QProcess cover;
    const auto stop_cover = [&] {
        if (cover.state() != QProcess::NotRunning) {
            cover.terminate();
            if (!cover.waitForFinished(1000)) {
                cover.kill();
                cover.waitForFinished(1000);
            }
        }
    };
    QString failure;
    QTimer sample;
    sample.setInterval(250);
    QObject::connect(&sample, &QTimer::timeout, &app, [&] {
        const auto handle = window.load();
        states.append(QVariantMap{{"atUs", nowUs()},
                                  {"active", bool(handle && GetForegroundWindow() == handle)},
                                  {"minimized", bool(handle && IsIconic(handle))}});
    });
    sample.start();
    const QStringList names{"foreground", "unfocused", "covered",
                            "minimized",  "recovered", "gui-blocked"};
    // 首帧颜色校验和设备创建留 2 秒预热，避免混入阶段分布。
    for (int i = 0; i <= names.size(); ++i) {
        QTimer::singleShot(2000 + i * stage_ms, &app, [&, i] {
            if (i == names.size()) {
                app.quit();
                return;
            }
            const auto handle = window.load();
            if (!handle) {
                failure = "独立呈交窗口未就绪";
                app.quit();
                return;
            }
            const auto name = names[i];
            if (name == "foreground" || name == "recovered") {
                stop_cover();
                ShowWindowAsync(handle, SW_RESTORE);
                SetForegroundWindow(handle);
            } else if (name == "unfocused" || name == "covered") {
                stop_cover();
                const auto rect = name == "covered"
                                      ? available
                                      : QRect(available.right() - 200, available.top(), 180, 160);
                cover.start(args[3],
                            {QString::number(rect.x()), QString::number(rect.y()),
                             QString::number(rect.width()), QString::number(rect.height())});
                if (!cover.waitForStarted(2000)) {
                    failure = cover.errorString();
                    app.quit();
                }
                if (cover.state() == QProcess::Running)
                    AllowSetForegroundWindow(DWORD(cover.processId()));
            } else if (name == "minimized") {
                ShowWindowAsync(handle, SW_MINIMIZE);
            }
            phases.append(QVariantMap{{"name", name}, {"atUs", nowUs()}});
            if (name == "gui-blocked")
                QTimer::singleShot(stage_ms / 2, &app, [&] {
                    phases.append(QVariantMap{{"name", "block-start"}, {"atUs", nowUs()}});
                    QThread::msleep(250);
                    phases.append(QVariantMap{{"name", "block-end"}, {"atUs", nowUs()}});
                });
        });
    }
    app.exec();
    const auto ended = nowUs();
    stop_cover();
    render.request_stop();
    SetEvent(signal);
    render.join();
    source.request_stop();
    source.join();
    if (!render_error.isEmpty()) failure = render_error;
    if (!decoder_error.isEmpty()) failure = decoder_error;
    if ((validation_error < 0 || frames.isEmpty()) && failure.isEmpty())
        failure = "未完成 GPU 首帧校验/呈交";
    QFile output(args[2]);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(
            QJsonDocument::fromVariant(QVariantMap{{"phases", phases},
                                                   {"endedUs", ended},
                                                   {"frames", frames},
                                                   {"senderUs", sent_times},
                                                   {"windowStates", states},
                                                   {"validationMaxChannelError", validation_error},
                                                   {"backend", backend},
                                                   {"failure", failure},
                                                   {"stageMs", stage_ms},
                                                   {"noUdp", true}})
                .toJson(QJsonDocument::Compact)) < 0)
        return 2;
    return failure.isEmpty() ? 0 : 1;
}
