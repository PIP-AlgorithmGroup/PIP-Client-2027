#include "video_decode_devices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QSettings>
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <dxgi1_2.h>
#endif

namespace {
QString readText(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString{};
}

// CUDA Driver API 为可选动态依赖；函数指针仅在 library 存活的本次枚举中使用。
// 显式声明 Win32 ABI，不将 SDK 或原生上下文暴露到业务层。
#ifdef Q_OS_WIN
#define PIP_CUDA_CALL __stdcall
#else
#define PIP_CUDA_CALL
#endif
void appendCuda(std::vector<VideoDecodeDevice>& devices) {
#ifdef Q_OS_WIN
    QLibrary library("nvcuda");
#else
    QLibrary library("libcuda.so.1");
#endif
    using Init = int(PIP_CUDA_CALL*)(unsigned int);
    using Count = int(PIP_CUDA_CALL*)(int*);
    using Name = int(PIP_CUDA_CALL*)(char*, int, int);
    using Uuid = int(PIP_CUDA_CALL*)(void*, int);
    const auto init = reinterpret_cast<Init>(library.resolve("cuInit"));
    const auto count_devices = reinterpret_cast<Count>(library.resolve("cuDeviceGetCount"));
    const auto name_device = reinterpret_cast<Name>(library.resolve("cuDeviceGetName"));
    const auto uuid_device = reinterpret_cast<Uuid>(library.resolve("cuDeviceGetUuid"));
    int count = 0;
    if (!init || !count_devices || !name_device || !uuid_device || init(0) != 0 ||
        count_devices(&count) != 0)
        return;
    for (int index = 0; index < count; ++index) {
        char name[256]{};
        char uuid[16]{};
        if (name_device(name, sizeof(name), index) != 0 || uuid_device(uuid, index) != 0) continue;
        devices.push_back({"cuda:" + QString::fromLatin1(QByteArray(uuid, 16).toHex()), "cuda",
                           QString::number(index), QString::fromUtf8(name) + " · CUDA"});
    }
}
#undef PIP_CUDA_CALL
}  // namespace

std::vector<VideoDecodeDevice> videoDecodeDevices() {
    std::vector<VideoDecodeDevice> devices;
#ifdef Q_OS_WIN
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(
            CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                const auto luid =
                    (quint64(quint32(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;
                devices.push_back({"d3d11va:" + QString::number(luid, 16), "d3d11va",
                                   QString::number(index),
                                   QString::fromWCharArray(desc.Description) + " · D3D11VA [GPU " +
                                       QString::number(index) + "]"});
            }
            adapter->Release();
        }
        factory->Release();
    }
#elif defined(Q_OS_LINUX)
    const auto nodes =
        QDir("/dev/dri").entryList({"renderD*"}, QDir::System | QDir::Files, QDir::Name);
    for (const auto& node : nodes) {
        const auto sys_device = "/sys/class/drm/" + node + "/device";
        const auto canonical = QFileInfo(sys_device).canonicalFilePath();
        const auto identity = canonical.isEmpty() ? node : QFileInfo(canonical).fileName();
        const auto vendor = readText(sys_device + "/vendor");
        const auto name = vendor == "0x8086"   ? "Intel GPU"
                          : vendor == "0x10de" ? "NVIDIA GPU"
                          : vendor == "0x1002" ? "AMD GPU"
                                               : "GPU";
        devices.push_back({"vaapi:" + identity, "vaapi", "/dev/dri/" + node,
                           QString(name) + " " + readText(sys_device + "/device") + " · VAAPI · " +
                               node + " [" + identity + "]"});
    }
#endif
    appendCuda(devices);
    return devices;
}

QString videoCpuName() {
#ifdef Q_OS_WIN
    QSettings cpu("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                  QSettings::NativeFormat);
    const auto name = cpu.value("ProcessorNameString").toString().trimmed();
#else
    QString name;
    for (const auto& line : readText("/proc/cpuinfo").split('\n')) {
        if (line.startsWith("model name")) {
            name = line.section(':', 1).trimmed();
            break;
        }
    }
#endif
    return name.isEmpty() ? QSysInfo::currentCpuArchitecture() : name;
}
