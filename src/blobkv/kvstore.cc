#include <iostream>

#include "kvstore.h"

// put只是放在op_log数组中，可以直接move进来
void kvstore::put(std::string key, std::optional<std::string> value) {
    op_log.emplace_back(std::move(key), std::move(value));
}

// remove只是放在op_log数组中，可以直接move进来
void kvstore::remove(std::string key) {
    op_log.emplace_back(std::move(key), std::nullopt);
}

// 此处应该是已经写到磁盘之后才apply_op的，所以可以直接move进来
void kvstore::apply_op(std::string key, std::optional<std::string> value) {
    auto it = table.find(key);
    if (it != table.end()) {
        // 如果找到了
        if (value) {
            it->second = std::move(*value);
        } else {
            table.erase(it);
        }
    } else {
        if (value) {
            table.emplace(std::move(key), std::move(*value));
        } else {
            SPDK_WARNLOG("error deleting non existent key: %s", key.c_str());
        }
    }
}   

void kvstore::persist() {
    auto ops = std::exchange(op_log, {});

    for (auto& op : ops) {
        write_buf.refresh();

        size_t sz = op.encode_header(temporary_buf);
        write_buf.memcpy(temporary_buf, sz);
        write_buf.memcpy(op.key.data(), op.key.size());
        if (op.value) {
            write_buf.memcpy(op.value->data(), op.value->size());
        }
    }

    
}