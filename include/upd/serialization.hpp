#pragma once

#include "detail/endianess.hpp"
#include "detail/sfinae.hpp"
#include "detail/signed_representation.hpp"

namespace upd {

//! \brief Interpret a part of a byte sequence as a value of the given type
//! \tparam T Requested type
//! \tparam Endianess Endianess of the value representation in the byte sequence
//! \tparam Signed_Mode Signed number representation of the value representation in the byte sequence
//! \param begin Iterator to a byte sequence to interpret from
//! \return A copy of the value represented by the byte sequence
#ifdef DOXYGEN
template<typename It, typename T, endianess Endianess, signed_mode Signed_Mode>
T read_as(It begin);
#else
template<typename T, endianess Endianess, signed_mode, sfinae::require_unsigned_integer<T> = 0>
T read_as(const byte_t *sequence) {
  return detail::from_endianess<T, Endianess>(sequence, sizeof(T));
}
template<typename T, endianess Endianess, signed_mode Signed_Mode, sfinae::require_signed_integer<T> = 0>
T read_as(const byte_t *sequence) {
  auto tmp = detail::from_endianess<unsigned long long, Endianess>(sequence, sizeof(T));

  return detail::from_signed_mode<T, Signed_Mode>(tmp);
}
template<typename T, endianess Endianess, signed_mode Signed_Mode, sfinae::require_bounded_array<T> = 0>
array_wrapper<T> read_as(const byte_t *sequence) {
  array_wrapper<T> retval;

  using element_t = boost::remove_reference_t<decltype(*retval)>;
  constexpr auto size = retval.size;

  for (size_t i = 0; i < size; i++)
    retval[i] = read_as<element_t, Endianess, Signed_Mode>(sequence + i * sizeof(element_t));

  return retval;
}
template<typename T, endianess Endianess, signed_mode Signed_Mode, typename It, sfinae::require_not_pointer<It> = 0>
decltype(read_as<T, Endianess, Signed_Mode>(boost::declval<byte_t *>())) read_as(It it) {
  byte_t buf[sizeof(T)];
  for (byte_t &byte : buf)
    byte = *it++;
  return read_as<T, Endianess, Signed_Mode>(buf);
}
#endif

//! \brief Interpret a part of a byte sequence as a value of the given type at the given offset
//! \tparam T requested type
//! \tparam Endianess endianess of the value representation in the byte sequence
//! \tparam Signed_Mode signed number representation of the value representation in the byte sequence
//! \param begin iterator to a byte sequence to interpret from
//! \param offset offset from the start of the byte sequence to write at
//! \return A copy of the value represented by the byte sequence
#ifdef DOXYGEN
template<typename T, endianess Endianess, signed_mode Signed_Mode, typename It>
auto read_as(const It &begin, size_t offset)
#else
template<typename T, endianess Endianess, signed_mode Signed_Mode, typename It, sfinae::require_has_plus<It> = 0>
decltype(read_as<T, Endianess, Signed_Mode>(boost::declval<byte_t *>())) read_as(const It &begin, size_t offset) {
  return read_as<T, Endianess, Signed_Mode>(begin + offset);
}
template<typename T, endianess Endianess, signed_mode Signed_Mode, typename It, sfinae::require_has_not_plus<It> = 0>
decltype(read_as<T, Endianess, Signed_Mode>(boost::declval<byte_t *>())) read_as(It it, size_t offset) {
  for (; offset > 0; offset--)
    it++;
  return read_as<T, Endianess, Signed_Mode>(it);
}
#endif

//! \brief Serialize a value into a byte sequence
//! \tparam Endianess Endianess of the value representation in the byte sequence
//! \tparam Signed_Mode Signed number representation of the value representation in the byte sequence
//! \tparam T Serilized value's type
//! \param x Value to be serialized
//! \param begin Iterator to a byte sequence to write into
#ifdef DOXYGEN
    template<endianess Endianess, signed_mode Signed_Mode, typename T>
    void write_as(const T &x, It begin);
#else
template<endianess Endianess, signed_mode, typename T, sfinae::require_unsigned_integer<T> = 0>
void write_as(const T &x, byte_t *sequence) {
  detail::to_endianess<Endianess>(sequence, x, sizeof(x));
}
template<endianess Endianess, signed_mode Signed_Mode, typename T, sfinae::require_signed_integer<T> = 0>
void write_as(const T &x, byte_t *sequence) {
  auto tmp = detail::to_signed_mode<Signed_Mode>(x);

  detail::to_endianess<Endianess>(sequence, tmp, sizeof(x));
}
template<endianess Endianess, signed_mode Signed_Mode, typename T, sfinae::require_bounded_array<T> = 0>
void write_as(const T &array, byte_t *sequence) {
  using element_t = decltype(*array);
  constexpr auto array_size = sizeof(array) / sizeof(*array);
  for (size_t i = 0; i < array_size; i++)
    write_as<Endianess, Signed_Mode>(array[i], sequence + i * sizeof(element_t));
}
template<endianess Endianess, signed_mode Signed_Mode, typename T, typename It, sfinae::require_not_pointer<It> = 0>
void write_as(const T &value, It it) {
  byte_t buf[sizeof(T)];

  write_as<Endianess, Signed_Mode>(value, buf);
  for (const byte_t &byte : buf)
    *it++ = byte;
}
#endif

//! \brief Serialize a value into a byte sequence at the given offset
//! \tparam Endianess endianess of the value representation in the byte sequence
//! \tparam Signed_Mode signed number representation of the value representation in the byte sequence
//! \param x value to be serialized
//! \param begin iterator to a byte sequence to write into
//! \param offset offset from the start of the byte sequence to write at
#ifdef DOXYGEN
template<endianess Endianess, signed_mode Signed_Mode, typename T, typename It>
void write_as(const T &value, const It &begin, size_t offset) {
  write_as<Endianess, Signed_Mode>(begin + offset);
}
#else
template<endianess Endianess, signed_mode Signed_Mode, typename T, typename It, sfinae::require_has_plus<It> = 0>
void write_as(const T &value, const It &begin, size_t offset) {
  write_as<Endianess, Signed_Mode>(begin + offset);
}
template<endianess Endianess, signed_mode Signed_Mode, typename T, typename It, sfinae::require_has_not_plus<It> = 0>
void write_as(const T &value, It it, size_t offset) {
  for (; offset > 0; offset--)
    it++;
  write_as<Endianess, Signed_Mode>(value, it);
}
#endif

} // namespace upd
