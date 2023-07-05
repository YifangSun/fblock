#include <spdk/env.h>
#include <sys/uio.h>
#include <spdk/log.h>

using iovecs = std::vector<::iovec>;

class spdk_buffer {
public:
    spdk_buffer() = default;
    spdk_buffer(char* buf, size_t sz) : buf(buf), size(sz), used(0) {}

    char* begin() { return buf; }

    char* end() { return buf + size; }

    char* get_write() { return buf + used; }

    void memcpy(char* in, size_t len) { 
      if (len > remain()) {
        SPDK_NOTICELOG("spdk_buffer no memory, size: %lu, len:%lu \n", size, len);
      }
      std::memcpy(get_write(), in, len); 
    }
    
    void refresh() { used = 0; }

    size_t remain() { 
      return size > used ? size - used : 0; 
    }

    size_t size;
    size_t used;
    char* buf;
    // 暂时不加
    // deleter _deleter;
};


// 偷懒的做法，不想封装迭代器，直接继承vector
class buffer_list : public std::vector<spdk_buffer> {
public:
  constexpr static size_t default_alloc = 4096;

  buffer_list() = default;

  void add_buffer(size_t size = default_alloc) {
    char* buf = (char*)spdk_malloc(size, 0x1000, NULL, SPDK_ENV_LCORE_ID_ANY, SPDK_MALLOC_DMA);
    /// TODO: 此处应该加入错误处理

    emplace_back(buf, size);
    total += size;
  }

  // 需要用户保证这块内存不会析构
  iovecs to_iovec() {
    iovecs iovs;
    size_t remain = total;
    for (auto it = begin(); it != end(); it++) {
      iovec iov;
      
      iov.iov_base = it->buf;
      iov.iov_len = std::min(it->used, remain);     
      iovs.push_back(iov);

      if (remain > it->used)
          remain -= it->used;
      else
          break;
    }
    return iovs;
  }

private:
  size_t total;
};


/*

hello_context->write_buff = (char*)spdk_malloc(4096,
				0x1000, NULL, SPDK_ENV_LCORE_ID_ANY,
				SPDK_MALLOC_DMA);
memset(hello_context->write_buff, 0x5a, 4096);

*/