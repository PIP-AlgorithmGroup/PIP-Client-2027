#include "platform/background_timer_resolution.h"

#include <QScopeGuard>
#include <QTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

/** 原生策略位及多拥有者恢复回归；仅改变本测试进程，不访问用户进程或全局电源设置。 */
class BackgroundTimerResolutionTest : public QObject {
    Q_OBJECT

   private slots:
    void restoresOnlyAfterLastOwner() {
#ifdef Q_OS_WIN
        PROCESS_POWER_THROTTLING_STATE original{};
        original.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        QVERIFY(GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &original,
                                      sizeof(original)));
        const auto restore = qScopeGuard([original] {
            auto value = original;
            SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &value,
                                  sizeof(value));
        });
        auto requested = original;
        constexpr auto flag = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        requested.ControlMask |= flag;
        requested.StateMask |= flag;
        if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &requested,
                                   sizeof(requested)))
            QSKIP("当前Windows不支持忽略计时精度策略，产品保留Qt原始行为并记录原因");
        const auto is_ignored = [] {
            PROCESS_POWER_THROTTLING_STATE value{};
            value.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            if (!GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &value,
                                       sizeof(value)))
                return -1;
            return (value.StateMask & PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION) ? 1 : 0;
        };
        {
            BackgroundTimerResolution first;
            QVERIFY2(first.error().isEmpty(), qPrintable(first.error()));
            QCOMPARE(is_ignored(), 0);
            {
                BackgroundTimerResolution second;
                QVERIFY(second.error().isEmpty());
                QCOMPARE(is_ignored(), 0);
            }
            QCOMPARE(is_ignored(), 0);
        }
        QCOMPARE(is_ignored(), 1);
#else
        // Ubuntu没有Windows窗口遮挡策略；守卫不申请额外的系统定时或电源接口。
        BackgroundTimerResolution first;
        BackgroundTimerResolution second;
        QVERIFY(first.error().isEmpty() && second.error().isEmpty());
#endif
    }
};

QTEST_GUILESS_MAIN(BackgroundTimerResolutionTest)
#include "background_timer_resolution_test.moc"
