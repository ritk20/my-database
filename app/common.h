#pragma once

#include <stdint.h>
#include <stddef.h>

#define container_of(ptr, type, member) \
  (reinterpret_cast<type *>(            \
      reinterpret_cast<char *>(ptr) - offsetof(type, member)))

inline std::uint32_t str_hash(const uint8_t *data, size_t len)
{
  std::uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++)
  {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

enum
{
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_DBL = 4,
  SER_ARR = 5,
};
