#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <string>
#include <string_view>

using bytes = std::basic_string<char8_t>;
using bytes_view = std::basic_string_view<char8_t>;

struct bytes_type_hash {
    using is_transparent = std::true_type;
    // NOTE: you hash a fragmented buffer with a linearized buffer
    //       unless you make a copy and linearize first. Our fragmented buffer
    //       correctly implements boost::hash_combine between fragments which
    //       would be missing altogether from a linearize buffer which is simply
    //       the std::hash<std::string_view>()
    //
    //   size_t operator()(const iobuf& k) const;
    //
    size_t operator()(const bytes& k) const;
    size_t operator()(const bytes_view&) const;
};

struct bytes_type_eq {
    using is_transparent = std::true_type;
    bool operator()(const bytes& lhs, const bytes_view& rhs) const;
    bool operator()(const bytes& lhs, const bytes& rhs) const;
};

// std::string to_hex(bytes_view b) {
//     static constexpr std::string_view digits{"0123456789abcdef"};
//     std::string out;
//     out.reserve(b.size() * 2);
//     const auto end = b.size();
//     for (size_t i = 0; i != end; ++i) {
//         uint8_t x = b[i];
//         out[2 * i] = digits[x >> uint8_t(4)];
//         out[2 * i + 1] = digits[x & uint8_t(0xf)];
//     }
//     return out;
// }

// namespace std {

// template<>
// struct hash<bytes_view> {
//     size_t operator()(bytes_view v) const {
//         return hash<std::string_view>()(
//           // NOLINTNEXTLINE
//           {reinterpret_cast<const char*>(v.data()), v.size()});
//     }
// };

// std::ostream& operator<<(std::ostream& os, const std::basic_string_view& b);

// } // namespace std

inline size_t bytes_type_hash::operator()(const bytes_view& k) const {
    return absl::Hash<bytes_view>{}(k);
}

inline size_t bytes_type_hash::operator()(const bytes& k) const {
    return absl::Hash<bytes>{}(k);
}

inline bool
bytes_type_eq::operator()(const bytes& lhs, const bytes& rhs) const {
    return lhs == rhs;
}
inline bool
bytes_type_eq::operator()(const bytes& lhs, const bytes_view& rhs) const {
    return bytes_view(lhs) == rhs;
}


