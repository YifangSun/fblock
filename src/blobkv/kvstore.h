#include <spdk/env.h>

#include <absl/container/flat_hash_map.h>
#include <string>
#include <vector>
#include <stdlib.h>

#include "bytes.h"
#include "coding.h"
#include "spdk_buffer.h"
#include "blob_file.h"

class kvstore {
public:
    kvstore() { 
      temporary_buf = (char*)malloc(64); 
      char* buf = (char*)spdk_malloc(67108864,
				  0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
				  SPDK_MALLOC_DMA);
      write_buf = spdk_buffer(buf, 67108864);
    }

    ~kvstore() {
      free(temporary_buf);
      spdk_free(write_buf.begin());
    }

    void put(std::string key, std::optional<std::string> value);

    void remove(std::string key);

    void apply_op(std::string key, std::optional<std::string> value);

    void persist();



    struct op {
      std::string key;
      std::optional<std::string> value;

      op(std::string&& key, std::optional<std::string>&& value)
          : key(std::move(key))
          , value(std::move(value)) {}

      uint32_t encoded_length() {
        uint32_t key_size = key.size();
        uint32_t val_size = value ? value->size() : 0;
        uint32_t encoded_len = VarintLength(key_size) + key_size 
                             + VarintLength(val_size) + val_size;
        return encoded_len;
      }

      size_t encode_header(char* buf) {
        uint32_t key_size = key.size();
        uint32_t val_size = value ? value->size() : 0;
        char* p = EncodeVarint32(buf, key_size);
        p = EncodeVarint32(p, val_size);
        return (p - buf);
      }
    };



    absl::flat_hash_map<std::string, std::string> table;
    std::vector<op> op_log;

public:
    /// TODO: append到相关，可以单独封装一个appender类
    char* temporary_buf; // 序列化时候会把op长度数字序列化，暂时放在这里
    spdk_buffer write_buf;

    struct blob_file* bf;
};