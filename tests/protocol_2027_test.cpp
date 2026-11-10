#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "client_to_server.pb.h"
#include "server_to_client.pb.h"

namespace {

// 使用异常报告失败，确保 Release 构建也执行检查，不依赖可能被禁用的 assert。
void require(bool is_valid, const std::string& context) {
    if (!is_valid) {
        throw std::runtime_error(context);
    }
}

// 快照独立于生成代码，覆盖完整消息清单、字段号、类型、packed 和 optional 存在性。
void checkSchema(const char* schema_path) {
    std::ifstream schema(schema_path);
    require(schema.is_open(), "Cannot open protocol schema fixture");
    const auto* pool = google::protobuf::DescriptorPool::generated_pool();
    std::set<std::string> expected_messages;
    std::string line;
    while (std::getline(schema, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string message_name;
        fields >> message_name;
        require(expected_messages.insert(message_name).second,
                "Duplicate fixture: " + message_name);
        const auto* message = pool->FindMessageTypeByName(message_name);
        require(message != nullptr, "Missing message: " + message_name);
        int field_number = 0;
        std::string token;
        while (fields >> token) {
            ++field_number;
            const auto* field = message->FindFieldByNumber(field_number);
            const std::string context = message_name + " field " + std::to_string(field_number);
            require(field != nullptr, "Missing " + context);
            const bool is_repeated = token.ends_with("[]");
            const std::string actual =
                field->name() + ":" + field->type_name() + (field->is_repeated() ? "[]" : "");
            require(actual == token, context + ": expected " + token + ", got " + actual);
            require(field->is_packed() == is_repeated, "Packed encoding changed: " + context);
            // proto3 optional 的 synthetic oneof 用于保留显式的零值、false 和空字节串。
            google::protobuf::FieldDescriptorProto field_proto;
            field->CopyTo(&field_proto);
            require(field_proto.proto3_optional() == !is_repeated, "Presence changed: " + context);
        }
        require(message->field_count() == field_number, "Unexpected field in " + message_name);
    }
    // 直接引用两个生成类，确保静态库链接器保留两份文件的描述符。
    const google::protobuf::FileDescriptor* files[] = {
        rm_client_to_server::KeyboardMouseControl::descriptor()->file(),
        rm_server_to_client::GameStatus::descriptor()->file(),
    };
    std::set<std::string> actual_messages;
    for (const auto* file : files) {
        for (int index = 0; index < file->message_type_count(); ++index) {
            actual_messages.insert(file->message_type(index)->full_name());
        }
    }
    require(actual_messages == expected_messages,
            "Unexpected or removed protocol messages present");
}

// 固定字节向量来自字段号和 Protobuf wire format，避免只验证同一份代码的自序列化。
void checkWireFormat() {
    rm_server_to_client::GameStatus status;
    require(status.ParseFromString(""), "Empty optional message must parse");
    require(!status.has_current_stage(), "Absent current_stage lost presence");
    require(status.ParseFromString(std::string("\x28\x00\x40\x00", 4)), "Zero fields failed");
    require(status.has_current_stage() && status.current_stage() == 0,
            "Explicit zero lost presence");
    require(status.has_is_paused() && !status.is_paused(), "Explicit false lost presence");

    rm_server_to_client::GlobalUnitStatus units;
    // 4/9 是固定 R/B 前哨血量，15/16 是新增控制度，0x80 0x01 为字段 16 的 tag。
    const std::string unit_wire("\x20\x64\x48\x32\x78\x07\x80\x01\x09", 9);
    require(units.ParseFromString(unit_wire), "2027 unit status failed to parse");
    const auto* reflection = units.GetReflection();
    const std::pair<const char*, unsigned int> expected_units[] = {
        {"outpost_R_health", 100U},
        {"outpost_B_health", 50U},
        {"total_CP_ally", 7U},
        {"total_CP_enemy", 9U},
    };
    for (const auto& [name, expected] : expected_units) {
        const auto* field = units.GetDescriptor()->FindFieldByName(name);
        require(field != nullptr, std::string("Missing 2027 field: ") + name);
        require(reflection->GetUInt32(units, field) == expected,
                std::string("Incorrect 2027 wire value: ") + name);
    }

    rm_server_to_client::RobotPathPlanInfo path;
    require(path.ParseFromString(std::string("\x22\x02\x00\x7f\x2a\x02\x01\x02", 8)),
            "Packed path failed to parse");
    require(path.offset_x_size() == 2 && path.offset_x(1) == 127 && path.offset_y(1) == 2,
            "Packed path values changed");
    path.clear_offset_x();
    path.add_offset_x(-128);
    rm_server_to_client::RobotPathPlanInfo negative_path;
    require(negative_path.ParseFromString(path.SerializeAsString()) &&
                negative_path.offset_x(0) == -128,
            "Signed path offset failed to round-trip");

    rm_client_to_server::CustomControl control;
    const std::string binary_data = std::string("\0\xff", 2) + std::string(58, 'x');
    control.set_data(binary_data);
    const std::string control_wire = std::string("\x0a\x3c", 2) + binary_data;
    require(control.SerializeAsString() == control_wire, "60-byte binary control changed");
    rm_client_to_server::CustomControl received_control;
    require(
        received_control.ParseFromString(control_wire) && received_control.data() == binary_data,
        "60-byte binary payload was truncated");
    // 大小上限属于收发边界校验；Protobuf 自身不会替应用拒绝 61 字节。
    require(!received_control.ParseFromString(control_wire.substr(0, control_wire.size() - 1)),
            "Truncated payload must fail parsing");

    const std::string unknown_wire = std::string("\x28\x04\xa0\x06\x01", 5);
    require(status.ParseFromString(unknown_wire) && status.current_stage() == 4,
            "Unknown fields must not discard known fields");
    require(status.SerializeAsString() == unknown_wire, "Unknown field was not preserved");
    require(!status.ParseFromString(std::string("\x28\x80", 2)),
            "Truncated varint must fail parsing");
}

}  // namespace

/** 运行离线协议一致性检查；参数为字段快照路径，失败打印上下文并返回非零值。 */
int main(int argc, char* argv[]) {
    try {
        require(argc == 2, "Usage: protocol_2027_test <schema fixture>");
        checkSchema(argv[1]);
        checkWireFormat();
        std::cout << "2027 protocol schema and wire checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
