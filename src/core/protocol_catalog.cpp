#include "core/protocol_catalog.h"

#include <google/protobuf/descriptor.h>

#include <QSet>
#include <cmath>

#include "client_to_server.pb.h"
#include "server_to_client.pb.h"

namespace {

// 规则从生成描述符建立，字段号不在传输层重复定义；有歧义的外层禁止接入。
const QMap<QString, ProtocolRule>& rules() {
    static const auto catalog = [] {
        QMap<QString, ProtocolRule> result;
        const QSet<QString> streams = {"Event", "PenaltyInfo",  "CustomByteBlock",
                                       "Buff",  "MapClickInfo", "SentryCtrlResult"};
        const auto add = [&](const google::protobuf::FileDescriptor* file, bool outgoing) {
            for (int index = 0; index < file->message_type_count(); ++index) {
                const auto* descriptor = file->message_type(index);
                const auto name = QString::fromStdString(descriptor->name());
                if (name == "RadarInfoToClient" || name == "RadarSingleRobotInfo") {
                    continue;
                }
                ProtocolRule rule;
                rule.topic = name;
                rule.prototype =
                    google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
                rule.is_outgoing = outgoing;
                rule.is_stream = streams.contains(name);
                if (outgoing) {
                    const bool realtime = name == "KeyboardMouseControl" || name == "CustomControl";
                    // ceil(1e6/75)，只允许实际发送时间向后偏移，不在延迟后突发补帧。
                    rule.minimum_interval_us = realtime                ? 13334
                                               : name == "MapClickCmd" ? 500000
                                                                       : 100000;
                    rule.qos = realtime ? 0 : 1;
                }
                result.insert(name, rule);
            }
        };
        add(rm_server_to_client::GameStatus::descriptor()->file(), false);
        add(rm_client_to_server::KeyboardMouseControl::descriptor()->file(), true);
        return result;
    }();
    return catalog;
}

}  // namespace

const ProtocolRule* protocolRule(const QString& topic) {
    const auto& catalog = rules();
    const auto it = catalog.constFind(topic);
    return it == catalog.cend() ? nullptr : &it.value();
}

QMap<QString, QString> officialReceiveBindings() {
    QMap<QString, QString> result;
    for (const auto& rule : rules()) {
        if (!rule.is_outgoing) {
            result.insert(rule.topic,
                          QString::fromStdString(rule.prototype->GetDescriptor()->full_name()));
        }
    }
    return result;
}

QString validateProtocolMessage(const google::protobuf::Message& message,
                                quint32 expected_robot_id) {
    const auto name = QString::fromStdString(message.GetDescriptor()->name());
    const auto* rule = protocolRule(name);
    if (!rule || rule->prototype->GetDescriptor() != message.GetDescriptor()) {
        return "Unsupported official message: " + name;
    }
    const auto* descriptor = message.GetDescriptor();
    const auto* reflection = message.GetReflection();
    const auto range = [&](const char* field_name, quint32 low, quint32 high) {
        const auto* field = descriptor->FindFieldByName(field_name);
        if (!field || !reflection->HasField(message, field)) {
            return true;
        }
        const auto value = reflection->GetUInt32(message, field);
        return value >= low && value <= high;
    };
    const auto all_ranges = [&](std::initializer_list<const char*> fields, quint32 low,
                                quint32 high) {
        for (const auto* field : fields) {
            if (!range(field, low, high)) {
                return false;
            }
        }
        return true;
    };
    bool valid = true;
    if (name == "CustomControl" || name == "CustomByteBlock") {
        const auto* field = descriptor->FindFieldByName("data");
        const auto size = reflection->GetString(message, field).size();
        if (size > (name == "CustomControl" ? 60U : 300U)) {
            return name + (name == "CustomControl" ? ": data exceeds 60 bytes"
                                                   : ": data exceeds 300 bytes");
        }
    } else if (name == "KeyboardMouseControl") {
        valid = range("keyboard_value", 0, 65535);
    } else if (name == "GameStatus") {
        valid = range("current_round", 1, UINT32_MAX) && range("current_stage", 0, 5);
        const auto* stage = descriptor->FindFieldByName("current_stage");
        if (reflection->HasField(message, stage)) {
            const bool settlement = reflection->GetUInt32(message, stage) == 5;
            valid = valid && range("game_result", settlement ? 0 : 255, settlement ? 2 : 255) &&
                    range("end_reason", settlement ? 1 : 255, settlement ? 7 : 255);
        }
    } else if (name == "RobotStaticStatus") {
        valid = all_ranges({"connection_state", "field_state"}, 0, 1) &&
                range("alive_state", 0, 2) &&
                all_ranges({"performance_system_shooter", "performance_system_chassis"}, 1, 2);
        const auto* field = descriptor->FindFieldByName("robot_id");
        if (expected_robot_id && reflection->HasField(message, field) &&
            reflection->GetUInt32(message, field) != expected_robot_id) {
            return "RobotStaticStatus robot_id does not match selected identity";
        }
    } else if (name == "Event") {
        const auto* field = descriptor->FindFieldByName("event_id");
        if (reflection->HasField(message, field)) {
            const auto id = reflection->GetInt32(message, field);
            valid = id >= 1 && id <= 16;
        }
    } else if (name == "RobotModuleStatus") {
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const auto* field = descriptor->field(i);
            valid = valid && range(field->name().c_str(), 0, field->number() == 11 ? 1 : 2);
        }
    } else if (name == "GlobalSpecialMechanism") {
        const auto* ids = descriptor->FindFieldByName("mechanism_id");
        const auto* times = descriptor->FindFieldByName("mechanism_time_sec");
        valid = reflection->FieldSize(message, ids) == reflection->FieldSize(message, times);
        for (int i = 0; i < reflection->FieldSize(message, ids); ++i) {
            const auto id = reflection->GetRepeatedUInt32(message, ids, i);
            valid = valid && id >= 1 && id <= 6;
        }
    } else if (name == "GlobalUnitStatus") {
        const auto* field = descriptor->FindFieldByName("robot_health");
        valid = reflection->FieldSize(message, field) == 0 ||
                reflection->FieldSize(message, field) == 8;
        valid = valid && all_ranges({"base_status", "enemy_base_status"}, 0, 2) &&
                all_ranges({"base_shield", "enemy_base_shield"}, 1, 2) &&
                all_ranges({"outpost_R_status", "outpost_B_status"}, 1, 4);
    } else if (name == "RobotPathPlanInfo") {
        for (const auto* axis : {"offset_x", "offset_y"}) {
            const auto* field = descriptor->FindFieldByName(axis);
            valid = valid && reflection->FieldSize(message, field) == 49;
            for (int i = 0; i < reflection->FieldSize(message, field); ++i) {
                const auto value = reflection->GetRepeatedInt32(message, field, i);
                valid = valid && value >= -128 && value <= 127;
            }
        }
        valid = valid && range("intention", 1, 3);
    } else if (name == "MapClickCmd" || name == "MapClickInfo") {
        const auto* field = descriptor->FindFieldByName("robot_id");
        valid = (!reflection->HasField(message, field) ||
                 reflection->GetString(message, field).size() == 7) &&
                range("is_send_all", 0, 2) && range("mode", 1, 4) && range("type", 1, 2);
        const auto* mode = descriptor->FindFieldByName("mode");
        const auto* ascii = descriptor->FindFieldByName("ascii");
        if (reflection->HasField(message, mode) && reflection->GetUInt32(message, mode) == 4 &&
            reflection->HasField(message, ascii)) {
            const auto value = reflection->GetUInt32(message, ascii);
            valid = valid && ((value >= 'C' && value <= 'L') || (value >= 'N' && value <= 'O') ||
                              (value >= 'Q' && value <= 'Z'));
        }
    } else if (name == "AssemblyCommand") {
        valid = range("operation", 0, 2);
    } else if (name == "RobotPerformanceSelectionCommand") {
        valid = all_ranges({"shooter", "chassis"}, 1, 2) && range("sentry_control", 0, 1);
    } else if (name == "RuneActivateCommand") {
        valid = range("activate", 1, 1);
    } else if (name == "DartCommand" || name == "DartSelectTargetStatusSync") {
        valid = range("target_id", 1, 5);
        if (name == "DartSelectTargetStatusSync") {
            valid = valid && range("open", 0, 2);
        }
    } else if (name == "CommonCommand") {
        valid = range("cmd_type", 1, 6);
        const auto* command = descriptor->FindFieldByName("cmd_type");
        const auto* param = descriptor->FindFieldByName("param");
        if (reflection->HasField(message, command) &&
            reflection->GetUInt32(message, command) == 1 && reflection->HasField(message, param)) {
            valid = valid && reflection->GetUInt32(message, param) % 10 == 0;
        }
    } else if (name == "SentryCtrlCommand") {
        const auto* field = descriptor->FindFieldByName("command_id");
        valid = range("command_id", 0, 12) && (!reflection->HasField(message, field) ||
                                               reflection->GetUInt32(message, field) != 2);
    } else if (name == "PenaltyInfo") {
        valid = range("penalty_type", 1, 6);
    } else if (name == "Buff") {
        valid = range("buff_type", 1, 7);
    } else if (name == "RuneStatusSync") {
        valid = range("rune_status", 1, 3);
    } else if (name == "SentryStatusSync") {
        valid = range("posture_id", 1, 3);
    }
    // 浮点 NaN/Inf 不能进入业务坐标或数值；不补造文档未给出的有限值上限。
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const auto* field = descriptor->field(i);
        if (!field->is_repeated() &&
            field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT &&
            reflection->HasField(message, field)) {
            valid = valid && std::isfinite(reflection->GetFloat(message, field));
        }
    }
    return valid ? QString{} : name + ": field value or array length violates protocol";
}
