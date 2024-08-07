//! \file

#pragma once

#include <cstddef>
#include <type_traits>

#include "dispatcher.hpp"
#include "policy.hpp"
#include "type.hpp"
#include "unevaluated.hpp"
#include "upd.hpp"

#include "detail/io/immediate_process.hpp"
#include "detail/io/immediate_reader.hpp"
#include "detail/io/immediate_writer.hpp"
#include "detail/static_error.hpp"
#include "detail/type_traits/require.hpp"
#include "detail/type_traits/signature.hpp"
#include "detail/type_traits/typelist.hpp"

// IWYU pragma: no_forward_declare upd::detail::map_parameters_size

namespace upd {
namespace detail {

//! \brief How many bytes that would be needed to represent any action request of `Keyring`
template<typename Keyring>
using needed_input_buffer_size =
    std::integral_constant<std::size_t,
                           detail::max<detail::map_parameters_size<typename Keyring::signatures_t::type>>::value +
                               sizeof(typename Keyring::index_t)>;

//! \brief How many bytes that would be needed to represent any action response of `Keyring`
template<typename Keyring>
using needed_output_buffer_size = detail::max<detail::map_parameters_size<typename Keyring::signatures_t::type>>;

} // namespace detail

//! \brief Dispatcher with input / output storage
//!
//! Instances of this class may store input and output byte streams while they are received or sent. This allows the
//! user to load and unload the dispatcher byte after byte, whereas plain dispatchers cannot buffer their input and
//! output. A buffered dispatcher goes through the following states:
//!
//!   -# The input buffer is empty, ready to accept an action request.
//!   -# Once a full action request has been received, it is immediately fulfilled and the result is written to the
//!   output buffer. The input buffer is reset, thus it may receive a new request while the output buffer is unloaded.
//!   -# Once the output buffer is empty, it may be written again.
//!
//! \note It is possible to use a single buffer as input and output as long as the reading and the writing does
//! not occur at the same time. For that purpose, is_loaded() will indicate whether the output buffer is empty or not.
//!
//! This class is not self-sufficient and must be derived from according to the CRTP idiom.
//!
//! \tparam D Derived class
//! \tparam Dispatcher Type of the underlying dispatcher
template<typename D, typename Dispatcher>
class buffered_undispatcher : public detail::immediate_reader<buffered_undispatcher<D, Dispatcher>, packet_status>,
                            public detail::immediate_writer<buffered_undispatcher<D, Dispatcher>>,
                            public detail::immediate_process<buffered_undispatcher<D, Dispatcher>, packet_status> {
  using this_t = buffered_undispatcher<D, Dispatcher>;

  D &derived() { return reinterpret_cast<D &>(*this); }
  const D &derived() const { return reinterpret_cast<const D &>(*this); }

public:
  //! \copydoc dispatcher::index_t
  using index_t = typename Dispatcher::index_t;

  //! \copydoc dispatcher::action_t
  using action_t = typename Dispatcher::action_t;

  //! \copydoc dispatcher::keyring_t
  using keyring_t = typename Dispatcher::keyring_t;

  //! \brief Initialize the underlying plain dispatcher with a keyring
  //! \tparam Keyring \ref<keyring> keyring template instance
  //! \tparam Action_Features Allowed action features for the managed actions
  template<typename Keyring, action_features Action_Features>
  explicit buffered_undispatcher(Keyring, action_features_h<Action_Features>) : buffered_undispatcher{} {}

  //! \copydoc buffered_undispatcher::buffered_undispatcher
  buffered_undispatcher()
      : m_is_index_loaded{false}, m_load_count{sizeof(index_t)}, m_ibuf_next{0}, m_obuf_next{0}, m_obuf_bottom{0} {}

  //! \brief Indicates whether the output buffer contains data to send
  //! \return `true` if and only if the next call to put() or write_to() will have a visible effect
  bool is_loaded() const { return m_obuf_next != m_obuf_bottom; }

  using detail::immediate_reader<this_t, packet_status>::read_from;

  //! \brief Put bytes into the input buffer until a full action request is stored
  //! \copydoc ImmediateReader_CRTP
  //! \param src Byte getter
  //! \return one of the following :
  //!   - packet_status::DROPPED_PACKET: The received index was invalid and the input buffer content was therefore
  //!   discarded.
  //!   - packet_status::RESOLVED_PACKET: The packet was fully loaded and the associated action has been called (the
  //!   input buffer is empty and the output buffer contains the result of the action invocation).
  template<typename Src, UPD_REQUIREMENT(input_invocable, Src)>
  packet_status read_from(Src &&src) {
    packet_status status = packet_status::LOADING_PACKET;
    while (!m_is_index_loaded && status == packet_status::LOADING_PACKET)
      status = put(src());
    while (m_is_index_loaded)
      status = put(src());
    return status;
  }

  UPD_SFINAE_FAILURE_MEMBER(read_from, UPD_ERROR_NOT_INPUT(src))

  //! \brief Put one byte into the input buffer
  //! \copydoc Reader_CRTP
  //! \param byte Byte to put
  //! \return one of the following :
  //!   - packet_status::LOADING_PACKET: The packet is not yet fully loaded.
  //!   - packet_status::DROPPED_PACKET: The received index was invalid and the input buffer content was therefore
  //!   discarded.
  //!   - packet_status::RESOLVED_PACKET: The packet was fully loaded and the associated action has been called (the
  //!   input buffer is empty and the output buffer contains the result of the action invocation).
  packet_status put(byte_t byte) {
    derived().ibuf_begin()[m_ibuf_next++] = byte;

    if (--m_load_count > 0)
      return packet_status::LOADING_PACKET;

    if (m_is_index_loaded) {
      m_is_index_loaded = false;
      m_load_count = sizeof(index_t);
      m_ibuf_next = 0;
      return packet_status::RESOLVED_PACKET;
    } else {
      auto *ibuf_ptr = derived().ibuf_begin();
      auto index = get_index([&]() { return *ibuf_ptr++; });
      if (index < m_dispatcher.size) {
        m_load_count = m_dispatcher[index].input_size();
        m_is_index_loaded = true;

        if (m_load_count == 0) {
          m_is_index_loaded = false;
          m_load_count = sizeof(index_t);
          m_ibuf_next = 0;
          return packet_status::RESOLVED_PACKET;
        } else {
          return packet_status::LOADING_PACKET;
        }
      } else {
        m_is_index_loaded = false;
        m_load_count = sizeof(index_t);
        m_ibuf_next = 0;
        return packet_status::DROPPED_PACKET;
      }
    }
  }

  using detail::immediate_writer<this_t>::write_to;

  //! \brief Completely output the output buffer content
  //! \copydoc ImmediateWriter_CRTP
  //! \param dest Byte putter
  template<typename Dest, UPD_REQUIREMENT(output_invocable, Dest)>
  void write_to(Dest &&dest) {
    while (is_loaded())
      dest(get());
  }

  UPD_SFINAE_FAILURE_MEMBER(write_all, UPD_ERROR_NOT_OUTPUT(dest))

  //! \brief Output one byte from the output buffer
  //!
  //! If the output buffer is empty, the function will return an arbitrary value.
  //!
  //! \copydoc Writer_CRTP
  //! \return the next byte in the output buffer (if it is not empty) or an arbitrary value
  byte_t get() { return is_loaded() ? derived().obuf_begin()[m_obuf_next++] : byte_t{}; }

  //! \copydoc dispatcher::replace(unevaluated<F,Ftor>)
  template<index_t Index, typename F, F Ftor>
  void replace(unevaluated<F, Ftor>) {
    m_dispatcher.template replace<Index>(unevaluated<F, Ftor>{});
  }

  using detail::immediate_process<this_t, packet_status>::operator();

  //! \brief Call read_from() then write_to()
  //!
  //! write_to() is called if and only the output buffer has been populated by read_from().
  //! \copydoc ImmediateProcess_CRTP
  //!
  //! \param src Byte getter
  //! \param dest Byte putter
  //! \return the \ref<packet_status> enumerator instance resulting from the read_from() call
  template<typename Src, typename Dest, UPD_REQUIREMENT(input_invocable, Src), UPD_REQUIREMENT(output_invocable, Dest)>
  packet_status operator()(Src &&src, Dest &&dest) {
    auto status = read_from(UPD_FWD(src));
    if (status == packet_status::RESOLVED_PACKET)
      write_to(UPD_FWD(dest));
    return status;
  }

#if __cplusplus >= 201703L
  //! \copydoc dispatcher::replace()
  template<index_t Index, auto &Ftor>
  void replace() {
    m_dispatcher.template replace<Index, Ftor>();
  }
#endif // __cplusplus >= 201703L

  //! \copydoc dispatcher::replace(F&&)
  template<index_t Index, typename F>
  void replace(F &&ftor) {
    m_dispatcher.template replace<Index>(UPD_FWD(ftor));
  }

  //! \brief Forward the content of the output buffer to another dispatcher as an action request
  //!
  //! This function send an action request to the other dispatcher, which is supposed to process directly the content of
  //! the output buffer. The requested action must accept a single byte buffer as sole argument (the size of the buffer
  //! can be choosen at compile time). The content of the received byte sequence is the same as if it was written by
  //! write_to(), so it can be deserialized inside the requested action by using the key that was used to populate the
  //! output buffer.
  //!
  //! This function can only be used if the data in the output buffer is complete (i.e. if no call to get() has been
  //! made since the last packet resolution) and if the requested action buffer is large enough to hold all the data to
  //! send. When this function has finished executing, the output buffer is empty.
  //!
  //! \param output Output byte stream of any kind
  //! \param k Key of the action to request on the other dispatcher
  //! \return `true` if and only if the content of the output buffer has been written to the output byte stream
  template<typename Output, typename Key, UPD_REQUIREMENT(key, typename std::decay<Key>::type)>
  bool reply(Output &&output, Key k) {
    constexpr auto buf_size = k.payload_length - sizeof k.index;
    if (!(m_obuf_next == 0 && m_obuf_bottom <= buf_size))
      return false;

    using buf_t = typename Key::tuple_t::template arg_t<0>;

    buf_t buf;
    write_to(std::begin(buf));
    k(buf).write_to(UPD_FWD(output));

    return true;
  }

  UPD_SFINAE_FAILURE_MEMBER(reply, UPD_ERROR_INVALID_KEY(key));

  //! \copydoc dispatcher::operator[](index_t)
  action_t &operator[](index_t index) { return m_dispatcher[index]; }

  //! \copydoc operator[]
  const action_t &operator[](index_t index) const { return m_dispatcher[index]; }

private:

  //! \copydoc dispatcher::get_index
  template<typename Src>
  index_t get_index(Src &&fetch_byte) const {
    return m_dispatcher.get_index(UPD_FWD(fetch_byte));
  }

  Dispatcher m_dispatcher;
  bool m_is_index_loaded;
  std::size_t m_load_count, m_ibuf_next, m_obuf_next, m_obuf_bottom;
};

//! \brief Implements a dispatcher using a single buffer for input and output
//!
//! The buffer is allocated statically as a plain array. Its size is as small as possible for holding any action request
//! and any action response.
//!
//!\warning It is not possible to read a request and write a response at the same time. If you
//! need to do that, use \ref<double_buffered_undispatcher> double_buffered_undispatcher instead.
//!
//! \tparam Dispatcher Underlying dispatcher type
template<typename Dispatcher>
class single_buffered_undispatcher : public buffered_undispatcher<single_buffered_undispatcher<Dispatcher>, Dispatcher> {
  using base_t = buffered_undispatcher<single_buffered_undispatcher<Dispatcher>, Dispatcher>;

  friend base_t;
  byte_t *ibuf_begin() { return m_buf; }
  byte_t *obuf_begin() { return m_buf; }

  using keyring_t = typename base_t::keyring_t;

public:
  //! \brief Equals the size of the buffer
  constexpr static auto buffer_size =
      detail::max_p<detail::needed_input_buffer_size<keyring_t>, detail::needed_output_buffer_size<keyring_t>>::value;

  //! \brief Initialize the underlying dispatcher
  //!
  //! \tparam Keyring Keyring which holds the actions to be managed by the dispatcher
  //! \tparam Action_Features Features of the actions managed by the dispatcher
  template<typename Keyring, action_features Action_Features>
  explicit single_buffered_undispatcher(Keyring, action_features_h<Action_Features>) : single_buffered_undispatcher{} {}

  //! \copybrief single_buffered_undispatcher::single_buffered_undispatcher
  single_buffered_undispatcher() = default;

private:
  byte_t m_buf[buffer_size];
};

#if __cplusplus >= 201703L
template<typename Keyring, action_features Action_Features>
single_buffered_undispatcher(Keyring, action_features_h<Action_Features>)
    -> single_buffered_undispatcher<dispatcher<Keyring, Action_Features>>;
#endif // __cplusplus >= 201703L

//! \brief Make a single buffered dispatcher
//! \related single_buffered_undispatcher
#if defined(DOXYGEN)
template<typename Keyring, action_features Action_Features>
auto make_single_buffered_dispatcher(Keyring, action_features_h<Action_Features>);
#else  // defined(DOXYGEN)
template<typename Keyring, action_features Action_Features>
single_buffered_undispatcher<dispatcher<Keyring, Action_Features>>
make_single_buffered_undispatcher(Keyring, action_features_h<Action_Features>) {
  return single_buffered_undispatcher<dispatcher<Keyring, Action_Features>>{Keyring{},
                                                                          action_features_h<Action_Features>{}};
}
#endif // defined(DOXYGEN)

//! \brief Implements a dispatcher using separate buffers for input and output
//!
//! The buffers are allocated statically as plain arrays. Their sizes are as small as possible for holding any action
//! request and any action response.
//!
//!\note If you would rather having a single buffer and do not mind reading and writing at different moment, consider
//! using \ref<single_buffered_undispatcher> single_buffered_undispatcher instead.
//!
//! \tparam Dispatcher Underlying dispatcher type
template<typename Dispatcher>
class double_buffered_undispatcher : public buffered_undispatcher<double_buffered_undispatcher<Dispatcher>, Dispatcher> {
  using base_t = buffered_undispatcher<double_buffered_undispatcher<Dispatcher>, Dispatcher>;

  friend base_t;
  byte_t *ibuf_begin() { return m_ibuf; }
  byte_t *obuf_begin() { return m_obuf; }

  using keyring_t = typename base_t::keyring_t;

public:
  //! \brief Equals the size of the input buffer
  constexpr static auto input_buffer_size = detail::needed_input_buffer_size<keyring_t>::value;

  //! \brief Equals the size of the output buffer
  constexpr static auto output_buffer_size = detail::needed_output_buffer_size<keyring_t>::value;

  //! \brief Initialize the underlying dispatcher
  //!
  //! \tparam Keyring Keyring which holds the actions to be managed by the dispatcher
  //! \tparam Action_Features Features of the actions managed by the dispatcher
  template<typename Keyring, action_features Action_Features>
  explicit double_buffered_undispatcher(Keyring, action_features_h<Action_Features>) : double_buffered_undispatcher{} {}

  //! \copybrief double_buffered_undispatcher::double_buffered_undispatcher
  double_buffered_undispatcher() = default;

private:
  byte_t m_ibuf[input_buffer_size], m_obuf[output_buffer_size];
};

#if __cplusplus >= 201703L
template<typename Keyring, action_features Action_Features>
double_buffered_undispatcher(Keyring, action_features_h<Action_Features>)
    -> double_buffered_undispatcher<dispatcher<Keyring, Action_Features>>;
#endif // __cplusplus >= 201703L

//! \brief Make a double buffered dispatcher
//! \related double_buffered_undispatcher
#if defined(DOXYGEN)
template<typename Keyring, action_features Action_Features>
auto make_double_buffered_dispatcher(Keyring, action_features_h<Action_Features>);
#else  // defined(DOXYGEN)
template<typename Keyring, action_features Action_Features>
double_buffered_undispatcher<dispatcher<Keyring, Action_Features>>
make_double_buffered_undispatcher(Keyring, action_features_h<Action_Features>) {
  return double_buffered_undispatcher<dispatcher<Keyring, Action_Features>>{Keyring{},
                                                                          action_features_h<Action_Features>{}};
}
#endif // defined(DOXYGEN)

} // namespace upd
