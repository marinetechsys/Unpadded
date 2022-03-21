//! \file
//! \brief Order request storage and processing

#pragma once

#include <cstddef>

#include <upd/type.hpp>

#include "detail/io.hpp"
#include "detail/sfinae.hpp"
#include "dispatcher.hpp"

#include "detail/def.hpp"

namespace k2o {

//! \brief Dispatcher with input / output storage
//! \details
//!   Instances of this class may store input and output byte sequences as they are received / sent. This allows the
//!   user to load and unload the dispatcher byte after byte, whereas plain dispatchers cannot buffer their input and
//!   ouput, therefore they must receive and send byte sequences all at once. Buffered dispatchers do not own their
//!   buffers directly. They must be provided through iterators. A buffered dispatcher state goes through the following
//!   states:
//!     -# The input buffer is empty, ready to load an order request in the input buffer.
//!     -# Once a full order request has been received, it is immediately fulfilled and the result is written in the
//!     output buffer. The input buffer is reset, thus it may receive a new request while the output buffer is unloaded.
//!     -# Once the output buffer is empty, it may be written again.
//!   \note Input buffer reset is soft, in other word, its content is not erased. As a result, it is possible to use a
//!   single buffer as input and output as long as byte sequence reading and writting does not occur at the same time.
//!   For that purpose, 'is_loaded' will indicate whether the output buffer is empty or not.
//! \tparam N Number of stored orders
//! \tparam Input_Iterator Type of the iterator to the input buffer
//! \tparam Output_Iterator Type of the iterator to the output buffer
template<std::size_t N, typename Input_Iterator, typename Output_Iterator>
class buffered_dispatcher : public detail::reader<buffered_dispatcher<N, Input_Iterator, Output_Iterator>, void>,
                            detail::writer<buffered_dispatcher<N, Input_Iterator, Output_Iterator>> {
  using this_t = buffered_dispatcher<N, Input_Iterator, Output_Iterator>;

public:
  //! \copydoc dispatcher::index_t
  using index_t = typename dispatcher<N>::index_t;

  //! \brief Initialize the underlying plain dispatcher with a keyring and hold iterators to the buffers
  //! \tparam Keyring Type of the keyring
  //! \param input_it Start of the input buffer
  //! \param output_it Start of the output buffer
  template<typename Keyring, sfinae::require_is_deriving_from_keyring<Keyring> = 0>
  buffered_dispatcher(Keyring, Input_Iterator input_it, Output_Iterator output_it)
      : m_dispatcher{Keyring{}}, m_is_index_loaded{false}, m_load_count{sizeof(index_t)}, m_ibuf_begin{input_it},
        m_ibuf_next{input_it}, m_obuf_begin{output_it}, m_obuf_next{output_it}, m_obuf_bottom{output_it} {}

  //! \brief Indicates whether the output buffer contains data to send
  //! \return true if and only if the next call to 'write' or 'write_all' will have a visible effect
  bool is_loaded() const { return m_obuf_next != m_obuf_bottom; }

  using detail::immediate_reader<this_t, void>::read_all;

  //! \brief Put bytes into the input buffer until a full order request is stored
  //! \param src_ftor Input functor to a byte sequence
  template<typename Src_F, sfinae::require_input_ftor<Src_F> = 0>
  void read_all(Src_F &&src_ftor) {
    while (!m_is_index_loaded)
      read(src_ftor);
    while (m_is_index_loaded)
      read(src_ftor);
  }

  using detail::reader<this_t, void>::read;

  //! \brief Put one byte into the input buffer
  //! \param src_ftor Input functor to a byte sequence
  template<typename Src_F, sfinae::require_input_ftor<Src_F> = 0>
  void read(Src_F &&src_ftor) {
    *m_ibuf_next++ = FWD(src_ftor)();
    if (--m_load_count == 0) {
      auto ibuf_it = m_ibuf_begin;
      auto get_index_byte = [&]() { return *ibuf_it++; };
      if (m_is_index_loaded) {
        m_is_index_loaded = false;
        m_load_count = sizeof(index_t);
        m_ibuf_next = m_ibuf_begin;

        auto index = get_index(get_index_byte);
        if (index >= N)
          return;
        m_obuf_bottom = m_obuf_begin;
        m_obuf_next = m_obuf_begin;

        m_dispatcher[index]([&]() { return *ibuf_it++; }, [&](upd::byte_t byte) { *m_obuf_bottom++ = byte; });
      } else {
        m_is_index_loaded = true;
        m_load_count = m_dispatcher[get_index(get_index_byte)].input_size();
      }
    }
  }

  using detail::immediate_writer<this_t>::write_all;

  //! \brief Completely output the output buffer content
  //! \param dest_ftor Output functor for writing byte sequences
  template<typename Dest_F, sfinae::require_output_ftor<Dest_F> = 0>
  void write_all(Dest_F &&dest_ftor) {
    while (is_loaded())
      write(dest_ftor);
  }

  using detail::writer<this_t>::write;

  //! \brief Output one byte from the output buffer
  //! \param dest_ftor Output functor for writing byte sequences
  template<typename Dest_F, sfinae::require_output_ftor<Dest_F> = 0>
  void write(Dest_F &&dest_ftor) {
    if (is_loaded())
      FWD(dest_ftor)(*m_obuf_next++);
  }

private:
  //! \copydoc dispatcher::get_index
  template<typename Src_F>
  index_t get_index(Src_F &&fetch_byte) const {
    return m_dispatcher.get_index(FWD(fetch_byte));
  }

  dispatcher<N> m_dispatcher;
  bool m_is_index_loaded;
  std::size_t m_load_count;
  Input_Iterator m_ibuf_begin, m_ibuf_next;
  Output_Iterator m_obuf_begin, m_obuf_next, m_obuf_bottom;
};

#if __cplusplus >= 201703L

template<typename Keyring, typename Input_Iterator, typename Output_Iterator>
buffered_dispatcher(Keyring, Input_Iterator, Output_Iterator)
    -> buffered_dispatcher<decltype(dispatcher{Keyring{}})::size, Input_Iterator, Output_Iterator>;

#endif // __cplusplus >= 201703L

} // namespace k2o

#include "detail/undef.hpp" // IWYU pragma: keep
