#ifndef CHICKADEE_CBYTESWAP_HH
#define CHICKADEE_CBYTESWAP_HH
#if defined(CHICKADEE_KERNEL) || defined(CHICKADEE_PROCESS)

template <typename T>
inline T to_le(T x) {
    return x;
}
template <typename T>
inline T from_le(T x) {
    return x;
}

#else
#include <sys/types.h>
#ifdef __APPLE__
# include <libkern/OSByteOrder.h>
# define htole16(x) OSSwapHostToLittleInt16((x))
# define htole32(x) OSSwapHostToLittleInt32((x))
# define htole64(x) OSSwapHostToLittleInt64((x))
# define le16toh(x) OSSwapLittleToHostInt16((x))
# define le32toh(x) OSSwapLittleToHostInt32((x))
# define le64toh(x) OSSwapLittleToHostInt64((x))
#else
# include <endian.h>
#endif

inline uint16_t to_le(uint16_t x) {
    return htole16(x);
}
inline uint32_t to_le(uint32_t x) {
    return htole32(x);
}
inline int32_t to_le(int32_t x) {
    return htole32(x);
}
inline uint64_t to_le(uint64_t x) {
    return htole64(x);
}
inline uint16_t from_le(uint16_t x) {
    return le16toh(x);
}
inline uint32_t from_le(uint32_t x) {
    return le32toh(x);
}
inline int32_t from_le(int32_t x) {
    return le32toh(x);
}
inline uint64_t from_le(uint64_t x) {
    return le64toh(x);
}

#endif
#endif
