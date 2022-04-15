//! \file

#pragma once

#include <cstddef>
#include <functional>

#include <tl/expected.hpp>
#include <upd/format.hpp>
#include <upd/type.hpp>

#include "detail/io/immediate_process.hpp"
#include "detail/type_traits/require.hpp"
#include "detail/type_traits/smallest.hpp"
#include "keyring.hpp"
#include "order.hpp"
#include "policy.hpp"
#include "unevaluated.hpp" // IWYU pragma: keep

#include "detail/def.hpp"

// IWYU pragma: no_forward_declare unevaluated

namespace k2o {
namespace detail {

//! \name
//! \brief Make an order, with or without storage depending on the value of `Order_Features`
//! @{

template<order_features Order_Features,
         typename F,
         F Ftor,
         upd::endianess Endianess,
         upd::signed_mode Signed_Mode,
         REQUIRE(Order_Features == order_features::STATIC_STORAGE_DURATION_ONLY)>
no_storage_order make_order() {
  return no_storage_order{unevaluated<F, Ftor>{}, upd::endianess_h<Endianess>{}, upd::signed_mode_h<Signed_Mode>{}};
}

template<order_features Order_Features,
         typename F,
         F Ftor,
         upd::endianess Endianess,
         upd::signed_mode Signed_Mode,
         REQUIRE(Order_Features == order_features::ANY)>
order make_order() {
  return order{Ftor, upd::endianess_h<Endianess>{}, upd::signed_mode_h<Signed_Mode>{}};
}

//! @}

//! \brief Alias for `order` if `Order_Features` is `order_features::ANY`, `no_storage_order` otherwise
template<order_features Order_Features>
using order_t = decltype(make_order<Order_Features, int, 0, upd::endianess::BUILTIN, upd::signed_mode::BUILTIN>());

//! \brief Extract the index from a byte sequence
template<typename Src, typename Index>
Index get_index(Src &&src, Index (&read_index)(const upd::byte_t *)) {
  upd::byte_t index_buf[sizeof(Index)];
  for (auto &byte : index_buf)
    byte = FWD(src)();

  return read_index(index_buf);
}

//! \brief Dispatcher implementation
//! \note The reason for putting the implementation apart is a GCC compiler bug in C++17 with deduction guide.
template<size_t N, order_features Order_Features>
struct dispatcher_impl {
  using index_t = detail::smallest_unsigned_t<N>;
  using index_reader_t = index_t(const upd::byte_t *);
  using order_t = detail::order_t<Order_Features>;

  order_t orders[N];
  index_reader_t &read_index;

  //! \brief Get the functors held by the provided keyring
  template<upd::endianess Endianess, upd::signed_mode Signed_Mode, typename... Fs, Fs &...Ftors>
  explicit dispatcher_impl(keyring<Endianess, Signed_Mode, unevaluated<Fs, Ftors>...>)
      : orders{detail::make_order<Order_Features, Fs, Ftors, Endianess, Signed_Mode>()...},
        read_index{static_cast<index_reader_t &>(upd::read_as<index_t, Endianess, Signed_Mode>)} {}

  //! \brief Get the index and arguments from an input byte stream and call the matching order
  template<typename Src, typename Dest>
  index_t operator()(Src &&src, Dest &&dest) {
    auto index = get_index(src);

    if (index < N)
      orders[index](src, dest);

    return index;
  }

  //! \brief Get the order indicated by the byte sequence
  template<typename Src>
  tl::expected<std::reference_wrapper<order_t>, index_t> get_order(Src &&src) {
    using return_t = tl::expected<std::reference_wrapper<order>, index_t>;

    auto index = get_index(FWD(src));
    return index < N ? return_t{orders[index]} : tl::make_unexpected(index);
  }

  //! \brief Unserialize an index from `src` with the stored serialization parameters
  template<typename Src>
  index_t get_index(Src &&src) const {
    return detail::get_index(FWD(src), read_index);
  }
};

} // namespace detail

//! \brief Order containers able to unserialize byte sequence serialized by an key
//!
//! A dispatcher is constructed from a keyring and is able to unserialize a payload serialized by
//! an key created from the same keyring. When it happens, the dispatcher calls the
//! function associated with the key, forwarding the arguments from the payload to the function. The
//! functions are internally held as 'order' objects.
//!
//! \tparam N the number of functions held by the keyring
//! \tparam Endianess Byte order of the integers in the received packets
//! \tparam Signed_Mode Signed number representation of the data in the received packets
//! \tparam Order_Features Restriction on stored orders
template<size_t N, upd::endianess Endianess, upd::signed_mode Signed_Mode, order_features Order_Features>
class dispatcher : public detail::immediate_process<dispatcher<N, Endianess, Signed_Mode, Order_Features>,
                                                    typename detail::dispatcher_impl<N, Order_Features>::index_t> {
public:
  //! \copydoc detail::dispatcher_impl::index_t
  using index_t = typename detail::dispatcher_impl<N, Order_Features>::index_t;

  //! \copydoc detail::order_t
  using order_t = detail::order_t<Order_Features>;

  //! \brief Number of managed orders
  constexpr static auto size = N;

  dispatcher() = default;

  //! \brief Construct the object from the provided keyring
  //!
  //! 'N' can be deduced from the number of functions held by the keyring.
  //! \note The strange structure of this function is due to a GCC compiler bug with deduction guide in C++17.
  //!
  //! \param kring Keyring containing the orders to dispatch to
  template<typename Keyring, REQUIREMENT(is_keyring, Keyring)>
  explicit dispatcher(Keyring kring, order_features_h<Order_Features>) : m_impl{kring} {}

  using detail::immediate_process<dispatcher<N, Endianess, Signed_Mode, Order_Features>, index_t>::operator();

  //! \copydoc detail::dispatcher_impl::operator()
  //! \copydoc ImmediateProcess_CRTP
  //! \param src functor behaving as an input byte stream, from which the payload is fetched
  //! \param dest functor behaving as an output byte stream, in which the function call return value will be put
  //! \return the index of the called order
  template<typename Src, typename Dest, REQUIREMENT(input_invocable, Src), REQUIREMENT(output_invocable, Dest)>
  index_t operator()(Src &&src, Dest &&dest) {
    return m_impl(FWD(src), FWD(dest));
  }

  //! \copydoc detail::dispatcher_impl::get_order()
  //! \param src Functor acting as an input byte stream
  //! \return Either a reference to the order if it exists or the index that obtained from the byte sequence
  template<typename Src, REQUIREMENT(input_invocable, Src)>
  tl::expected<std::reference_wrapper<order_t>, index_t> get_order(Src &&src) {
    return m_impl.get_order(FWD(src));
  }

  //! \copydoc dispatcher_impl::get_index
  //! \param src Input functor to a byte sequence
  template<typename Src, REQUIREMENT(input_invocable, Src)>
  index_t get_index(Src &&src) const {
    return m_impl.get_index(FWD(src));
  }

  //! \brief Replace with a functor with static storage duration
  //! \tparam Ftor Functor with static storage duration
  //! \param index Index of the order associated with this new functor
  template<typename F, F Ftor>
  void replace(index_t index, unevaluated<F, Ftor>) {
    m_impl.orders[index] = detail::make_order<Order_Features, F, Ftor, Endianess, Signed_Mode>();
  }

#if __cplusplus >= 201703L
  //! \brief (C++17) Replace with a functor with static storage duration
  //! \tparam Ftor Functor with static storage duration
  template<auto &Ftor>
  void replace(index_t index) {
    replace(index, unevaluated<decltype(Ftor) &, Ftor>{});
  }
#endif // __cplusplus >= 201703L

  //! \brief Replace with a functor of any kind
  //! \param index Index of the order associated with this new functor
  //! \param ftor Functor of any kind
  template<typename F, REQUIRE_CLASS(Order_Features == order_features::ANY)>
  void replace(index_t index, F &&ftor) {
    m_impl.orders[index] = order{FWD(ftor), upd::endianess_h<Endianess>{}, upd::signed_mode_h<Signed_Mode>{}};
  }

  //! \brief Get one of the stored orders
  //! \param index Index of an order
  //! \return the order associated with that index
  //! \warning There are no bound check performed.
  order_t &operator[](index_t index) { return m_impl.orders[index]; }

  //! \copydoc operator[]
  const order_t &operator[](index_t index) const { return m_impl.orders[index]; }

private:
  detail::dispatcher_impl<N, Order_Features> m_impl;
};

#if __cplusplus >= 201703L
template<upd::endianess Endianess,
         upd::signed_mode Signed_Mode,
         order_features Order_Features,
         typename... Fs,
         Fs... Ftors>
dispatcher(keyring<Endianess, Signed_Mode, unevaluated<Fs, Ftors>...>, order_features_h<Order_Features>)
    -> dispatcher<sizeof...(Fs), Endianess, Signed_Mode, Order_Features>;
#endif // __cplusplus >= 201703L

//! \brief Make a dispatcher
//! \related dispatcher
template<upd::endianess Endianess,
         upd::signed_mode Signed_Mode,
         order_features Order_Features,
         typename... Fs,
         Fs... Ftors>
dispatcher<sizeof...(Fs), Endianess, Signed_Mode, Order_Features>
make_dispatcher(keyring<Endianess, Signed_Mode, unevaluated<Fs, Ftors>...> kring, order_features_h<Order_Features>) {
  return dispatcher<sizeof...(Fs), Endianess, Signed_Mode, Order_Features>{kring, {}};
}

} // namespace k2o

#include "detail/undef.hpp" // IWYU pragma: keep
