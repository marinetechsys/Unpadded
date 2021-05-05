#pragma once

#include "ct_magic.hpp"

#include "upd/format.hpp"
#include "upd/type.hpp"

#include "detail/signed_representation.hpp"

#include "array_wrapper.hpp"
#include "concept.hpp"

/*!
  \file
  \brief Definition of the unaligned_data class
*/

namespace upd {

/*!
  \brief Unaligned storage enabling reading and writing at any offset
  \details
    The class holds an array of bytes used to store the serialized representation of values without padding due
    to memory alignment.
    The only reasonably serializable value are integer values; Thus the implementation of unaligned_data only support
    unsigned integer values. It doesn't handle signed representation yet.
    The user shall provide the target endianess so that the unsigned integer are serialized without depending on the
    platform endianess.
  \tparam N Size of the content in bytes
*/
template<size_t N>
class unaligned_data {
public:
  /*!
    \brief Storage size in byte
  */
  constexpr static auto size = N;

  /*!
    \name Iterators
    @{
  */

  /*!
    \brief Iterator for accessing unaligned_data internal storage
    \details It satisfies the forward iterator requirement.
  */
  class iterator {
  public:
    iterator() : ptr{nullptr} {}

    explicit iterator(byte_t* ptr) : ptr{ptr} {}

    iterator operator++() {
      iterator previous;
      previous.ptr = ptr;
      ptr++;
      return previous;
    }

    byte_t& operator*() const { return *ptr; }
    bool operator!=(const iterator& other) const { return ptr != other.ptr; }

  private:
    byte_t* ptr;

  };

  /*!
    \brief Construct the object without initializing the object content
    \details
      It satisfies the forward iterator requirement.
      It is a constant iterator, meaning it permits read-only access to the container.
  */
  class const_iterator {
  public:
    const_iterator() : ptr{nullptr} {}

    explicit const_iterator(const byte_t* ptr) : ptr{ptr} {}

    const_iterator operator++() {
      const_iterator previous;
      previous.ptr = ptr;
      ptr++;
      return previous;
    }

    const byte_t& operator*() const { return *ptr; }
    bool operator!=(const iterator& other) const { return ptr != other.ptr; }

  private:
    const byte_t* ptr;

  };
  //! @}

  /*!
    \brief Default initialize the object content
    \param endianess Target endianess for serialization
  */
  explicit unaligned_data(endianess data_endianess, signed_mode data_signed_mode) :
    m_endianess{data_endianess},
    m_signed_mode{data_signed_mode}
  {};

  /*!
    \brief Initialize the object content of the object by copying from a buffer
    \details The Nth first bytes of the buffer are copied.
    \param raw_data Pointer to the buffer
    \param endianess Target endianess for serialization
  */
  explicit unaligned_data(
    const byte_t* raw_data,
    endianess data_endianess,
    signed_mode data_signed_mode) :
    m_endianess{data_endianess},
    m_signed_mode{data_signed_mode}
  {
    memcpy(m_raw_data, raw_data, N);
  }

  /*!
    \name Iterability
    @{
  */
  iterator begin() { return iterator{m_raw_data}; }
  iterator end() { return iterator{m_raw_data + N}; }
  const_iterator begin() const { return const_iterator{m_raw_data}; }
  const_iterator end() const { return const_iterator{m_raw_data + N}; }
  //! @}

  /*!
    \brief Access the object content
    \details There is no bound check performed.
    \param i Index of the accessed byte
  */
  byte_t& operator[](size_t i) { return m_raw_data[i]; }

  /*!
    \brief Access the object content
    \details There is no bound check performed.
    \param i Index of the accessed byte
  */
  const byte_t& operator[](size_t i) const { return m_raw_data[i]; }

  /*!
    \brief Interpret a part of the object content as the given type
    \details
      There is no bound check performed.
      This overload kicks in when T is an unsigned integer type.
    \tparam T Requested type
    \param offset Offset of the object content to be interpreted
    \return A copy of the value represented by the raw data at the given offset
  */
#ifdef DOXYGEN
  template<typename T> T interpret_as(size_t offset) const;
#else
  template<typename T>
  concept::require_unsigned_integer<T, T>
  interpret_as(size_t offset) const { return interpret_with_endianess<T>(offset, sizeof(T)); }
#endif

  /*!
    \brief Interpret a part of the object content as the given type
    \details
      There is no bound check performed.
      This overload kicks in when T is an signed integer type.
    \tparam T Requested type
    \param offset Offset of the object content to be interpreted
    \return A copy of the value represented by the raw data at the given offset
  */
#ifdef DOXYGEN
  template<typename T> T interpret_as(size_t offset) const;
#else
  template<typename T>
  concept::require_signed_integer<T, T>
  interpret_as(size_t offset) const {
    auto tmp = interpret_with_endianess<unsigned long long>(offset, sizeof(T));

    switch(m_signed_mode) {
      case signed_mode::ONE_COMPLEMENT: return detail::interpret_from_one_complement<T>(tmp);
      case signed_mode::TWO_COMPLEMENT: return detail::interpret_from_two_complement<T>(tmp);
      default: return 0; // other representation not yet supported
    }
  }
#endif

  /*!
    \brief Interpret a part of the object content as the given type
    \details
      There is no bound check performed.
      This overload kicks in when T is an array type.
    \tparam T Requested type
    \param offset Offset of the object content to be interpreted
    \return An array_wrapper object containing a copy of the requested array
  */
#ifdef DOXYGEN
  template<typename T> array_wrapper<T> interpret_as(size_t offset) const;
#else
  template<typename T>
  concept::require_array<T, array_wrapper<T>>
  interpret_as(size_t offset) const {
    array_wrapper<T> retval;

    using element_t = typename decltype(retval)::type;
    constexpr auto size = decltype(retval)::size;

    for (size_t i = 0; i < size; i++)
      retval[i] = interpret_as<element_t>(offset + i * sizeof(element_t));

    return retval;
  }
#endif

  /*!
    \brief Serialize a value into the object's content
    \details
      There is no bound check performed.
      This overload kicks in when T is an unsigned integer type.
    \tparam T Serilized value's type
    \param x Value to be serialized
    \param offset Offset where the value will be serialized
  */
#ifdef DOXYGEN
  template<typename T> void write(const T& x, size_t offset);
#else
  template<typename T>
  concept::require_unsigned_integer<T>
  write(const T& x, size_t offset) { write_with_endianess(x, offset, sizeof(x)); }
#endif

  /*!
    \brief Serialize a value into the object's content
    \details
      There is no bound check performed.
      This overload kicks in when T is an unsigned integer type.
    \tparam T Serilized value's type
    \param x Value to be serialized
    \param offset Offset where the value will be serialized
  */
#ifdef DOXYGEN
  template<typename T> void write(const T& x, size_t offset);
#else
  template<typename T>
  concept::require_signed_integer<T>
  write(const T& x, size_t offset) {
    auto tmp = 0ull;

    switch(m_signed_mode) {
      case signed_mode::ONE_COMPLEMENT:
        tmp = detail::interpret_to_one_complement(x);
        break;
      case signed_mode::TWO_COMPLEMENT:
        tmp = detail::interpret_to_two_complement(x);
        break;
      default:
        return; // not yet implemented
    }

    write_with_endianess(tmp, offset, sizeof(x));
  }
#endif

  /*!
    \brief Serialize a value into the object's content
    \details
      There is no bound check performed.
      This overload kicks in when T is an array type.
    \tparam T Serilized value's type
    \param x Value to be serialized
    \param offset Offset where the value will be serialized
  */
#ifdef DOXYGEN
  template<typename T> void write(const T& array, size_t offset)
#else
  template<typename T>
  concept::require_array<T>
  write(const T& array, size_t offset) {
    using element_t = ctm::element_t<T>;
    constexpr auto array_size = ctm::introspect_array<T>::size;
    for (size_t i = 0; i < array_size; i++) write(array[i], offset + i * sizeof(element_t));
  }
#endif

  /*!
    \brief Give a read-only view to the object's content
    \return A pointer to the beginning of the object's content
  */
  const byte_t* raw_data() const { return m_raw_data; }

private:
  template<typename T>
  T interpret_with_endianess(size_t offset, size_t n) const {
    T retval = 0;
    size_t shift = 0;

    switch(m_endianess) {
      case endianess::LITTLE:
        for (size_t i = 0; i < n; i++, shift += 8)
          retval |= static_cast<T>(m_raw_data[offset + i]) << shift;
        break;
      case endianess::BIG:
        for (size_t i = 0; i < n; i++, shift += 8)
          retval |= static_cast<T>(m_raw_data[offset + (n - i - 1)]) << shift;
        break;
    }

    return retval;
  }

  template<typename T>
  void write_with_endianess(T x, size_t offset, size_t n) {
    switch(m_endianess) {
      case endianess::LITTLE:
        for (size_t i = 0; i < n; i++, x >>= 8)
          m_raw_data[offset + i] = x & 0xff;
        break;
      case endianess::BIG:
        for (size_t i = 0; i < n; i++, x >>= 8)
          m_raw_data[offset + (n - i - 1)] = x & 0xff;
        break;
    }
  }

  byte_t m_raw_data[N];
  endianess m_endianess;
  signed_mode m_signed_mode;

};

/*!
  \brief Construct an unaligned_data object provided a lvalue to a bounded array
  \tparam N Size of the bounded array
  \param raw_data Array used to initiliaze the return value
  \param endianess Target endianess for serialization
  \return A unaligned_data object which content is equal to raw_data
*/
template<size_t N>
unaligned_data<N> make_unaligned_data(const byte_t (&raw_data)[N], endianess data_endianess) {
  return unaligned_data<N>{raw_data, data_endianess};
}

} // namespace upd
