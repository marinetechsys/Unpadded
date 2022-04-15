//! \file

#pragma once

#include <cstddef>
#include <memory>

#include <upd/format.hpp>
#include <upd/tuple.hpp>
#include <upd/type.hpp>

#include "detail/function_reference.hpp"
#include "detail/io/immediate_process.hpp"
#include "detail/static_error.hpp"
#include "detail/type_traits/flatten_tuple.hpp"
#include "detail/type_traits/input_tuple.hpp"
#include "detail/type_traits/require.hpp"
#include "detail/type_traits/signature.hpp"
#include "unevaluated.hpp"

#include "detail/def.hpp"

// IWYU pragma: no_include "upd/detail/value_h.hpp"

namespace k2o {
namespace detail {

//! \brief Input invocable erased type
using src_t = abstract_function<upd::byte_t()>;

//! \brief Output invocable erased type
using dest_t = abstract_function<void(upd::byte_t)>;

//! \brief Serialize `value` as a sequence of byte then call `dest` on every byte of that sequence
template<upd::endianess Endianess, upd::signed_mode Signed_Mode, typename T, REQUIREMENT(not_tuple, T)>
void insert(dest_t &dest, const T &value) {
  using namespace upd;

  auto output = make_tuple(endianess_h<Endianess>{}, signed_mode_h<Signed_Mode>{}, value);
  for (upd::byte_t byte : output)
    dest(byte);
}

//! \brief Invoke `ftor` on the unserialized arguments from `src`
template<typename Tuple, typename F>
void call(src_t &src, F &&ftor) {
  Tuple input_args;
  for (auto &byte : input_args)
    byte = src();
  input_args.invoke(FWD(ftor));
}

//! \copydoc call
template<typename Tuple, typename F, REQUIREMENT(is_void, detail::return_t<F>)>
void call(src_t &src, dest_t &, F &&ftor) {
  Tuple input_args;
  for (auto &byte : input_args)
    byte = src();
  input_args.invoke(FWD(ftor));
}

//! \copydoc call
template<typename Tuple, typename F, REQUIREMENT(not_void, detail::return_t<F>)>
void call(src_t &src, dest_t &dest, F &&ftor) {
  Tuple input_args;
  for (auto &byte : input_args)
    byte = src();

  return insert<Tuple::storage_endianess, Tuple::storage_signed_mode>(dest, input_args.invoke(FWD(ftor)));
}

//! \brief Implementation of the `order` class behaviour
//!
//! This class holds the functor passed to the `order` constructor and is used to deduce the appropriate `upd::tuple`
//! template instance for holding the functor parameters.
template<typename F, upd::endianess Endianess, upd::signed_mode Signed_Mode>
struct order_model_impl {
  using tuple_t = input_tuple<Endianess, Signed_Mode, F>;

  explicit order_model_impl(F &&ftor) : ftor{FWD(ftor)} {}

  F ftor;
};

//! \brief Abstract class used for setting up type erasure in the `order` class
struct order_concept {
  virtual ~order_concept() = default;
  virtual void operator()(src_t &&, dest_t &&) = 0;

  size_t input_size;
  size_t output_size;
};

//! \brief Derived class used for setting up type erasure in the `order` class
//!
//! This class is derived from the `order_concept` as a the "Model" class in the type erasure pattern.
//! This implementation of `order_concept` uses the `src_t` and `dest_t` functors to fetch the arguments for calling
//! the functor as a byte sequence and serialize the expression resulting from that call.
template<typename F, upd::endianess Endianess, upd::signed_mode Signed_Mode>
class order_model : public order_concept {
  using impl_t = order_model_impl<F, Endianess, Signed_Mode>;
  using tuple_t = typename impl_t::tuple_t;

public:
  explicit order_model(F &&ftor) : m_impl{FWD(ftor)} {
    order_model::input_size = tuple_t::size;
    order_model::output_size = flatten_tuple_t<Endianess, Signed_Mode, return_t<F>>::size;
  }

  void operator()(src_t &&src, dest_t &&dest) final { return detail::call<tuple_t>(src, dest, FWD(m_impl.ftor)); }

private:
  impl_t m_impl;
};

} // namespace detail

//! \brief Wrapper around an invocable object which serialize / unserialize parameters and return value
//!
//! Given a byte sequence generated by a key, an order of same signature (i.e. whose underlying invocable object has the
//! same signature as the aforesaid key) is able to unserialize the parameters from that byte sequence, call the
//! underlying invocable object and serialize the return value as a byte sequence which can later be unserialized the
//! key to obtain the return value.
class order : public detail::immediate_process<order, void> {
public:
  //! \brief Wrap a copy of the provided invocable object
  //! \tparam Endianess Byte order of the integers in the generated packets
  //! \tparam Signed_Mode Representation of signed integers in the generated packets
  //! \param ftor Invocable object to be wrapped
  template<upd::endianess Endianess, upd::signed_mode Signed_Mode, typename F, REQUIREMENT(invocable, F)>
  explicit order(F &&ftor, upd::endianess_h<Endianess>, upd::signed_mode_h<Signed_Mode>)
      : m_concept_uptr{new detail::order_model<F, Endianess, Signed_Mode>{FWD(ftor)}} {}

  //! \copybrief order::order
  //! \param ftor Invocable object to be wrapped
  template<typename F, REQUIREMENT(invocable, F)>
  explicit order(F &&ftor) : order{FWD(ftor), upd::builtin_endianess, upd::builtin_signed_mode} {}

  K2O_SFINAE_FAILURE_CTOR(order, K2O_ERROR_NOT_INVOCABLE(ftor))

  using detail::immediate_process<order, void>::operator();

  //! \brief Invoke the held invocable object and serialize / unserialize the parameters / return value
  //! \copydoc ImmediateProcess_CRTP
  //!
  //! \param src Input invocable object
  //! \param dest Output invocable object
  template<typename Src, typename Dest, REQUIREMENT(input_invocable, Src), REQUIREMENT(output_invocable, Dest)>
  void operator()(Src &&src, Dest &&dest) const {
    return (*m_concept_uptr)(detail::make_function_reference(src), detail::make_function_reference(dest));
  }

  K2O_SFINAE_FAILURE_MEMBER(operator(), K2O_ERROR_NOT_INPUT(src) " OR " K2O_ERROR_NOT_OUTPUT(dest))

  //! \copybrief operator()
  //! \param input Input byte sequence
  template<typename Input>
  void operator()(Input &&input) const {
    operator()(FWD(input), [](upd::byte_t) {});
  }

  //! \brief Get the size in bytes of the payload needed to invoke the wrapped object
  //! \return The size of the payload in bytes
  size_t input_size() const { return m_concept_uptr->input_size; }

  //! \brief Get the size in bytes of the payload representing the return value of the wrapped invocable object
  //! \return The size of the payload in bytes
  size_t output_size() const { return m_concept_uptr->output_size; }

private:
  std::unique_ptr<detail::order_concept> m_concept_uptr;
};

//! \brief Order which does not manage storage for its underlying functor
//!
//! Orders without storage must be given plain function or invocable with static storage duration. On the other hand,
//! they do not rely on dynamic allocation and are lighter than standard orders.
class no_storage_order : public detail::immediate_process<no_storage_order, void> {
public:
  using detail::immediate_process<no_storage_order, void>::operator();

  //! \brief Create an order holding the provided invocable object
  //! \tparam Ftor Reference to an invocable object with static storage duration
  //! \tparam Endianess Byte order of the integers in the generated packets
  //! \tparam Signed_Mode Representation of signed integers in the generated packets
  template<typename F, F Ftor, upd::endianess Endianess, upd::signed_mode Signed_Mode>
  explicit no_storage_order(unevaluated<F, Ftor>, upd::endianess_h<Endianess>, upd::signed_mode_h<Signed_Mode>)
      : wrapper{+[](detail::src_t &&src, detail::dest_t &&dest) {
          detail::input_tuple<Endianess, Signed_Mode, F> parameters_tuple;
          for (auto &byte : parameters_tuple)
            byte = src();
          auto return_tuple = upd::make_tuple(
              upd::endianess_h<Endianess>{}, upd::signed_mode_h<Signed_Mode>{}, parameters_tuple.invoke(Ftor));
          for (auto byte : return_tuple)
            dest(byte);
        }} {}

  //! \copybrief no_storage_order::no_storage_order
  //! \tparam Ftor Reference to an invocable object with static storage duration
  template<typename F, F Ftor>
  explicit no_storage_order(unevaluated<F, Ftor>)
      : no_storage_order{unevaluated<F, Ftor>{}, upd::builtin_endianess, upd::builtin_signed_mode} {}

  //! \brief Invoke the held invocable object and serialize / unserialize the parameters / return value
  //! \copydoc ImmediateProcess_CRTP
  //!
  //! \param src Input invocable object
  //! \param dest Output invocable object
  template<typename Src, typename Dest, REQUIREMENT(input_invocable, Src), REQUIREMENT(output_invocable, Dest)>
  void operator()(Src &&src, Dest &&dest) const {
    wrapper(detail::make_function_reference(src), detail::make_function_reference(dest));
  }

  K2O_SFINAE_FAILURE_MEMBER(operator(), K2O_ERROR_NOT_INPUT(src) " OR " K2O_ERROR_NOT_OUTPUT(dest))

private:
  void (*wrapper)(detail::src_t &&, detail::dest_t &&);
};
} // namespace k2o

#include "detail/undef.hpp" // IWYU pragma: keep
