//! \file
//! \brief SFINAE utilities

#pragma once

#include <boost/type_traits.hpp>
#include <boost/type_traits/is_bounded_array.hpp>

namespace upd {
namespace sfinae {

//! \brief Alias for typename boost::enable_if_<T, U>::type;
template<bool B, typename U = void>
using enable_t = typename boost::enable_if_<B, U>::type;

//!  \brief Utility class for SFINAE to check if a type is an unsigned integer
template<typename T, typename U = void>
using require_unsigned_integer = typename boost::enable_if_<boost::is_unsigned<T>::value, U>::type;

//! \brief Utility class for SFINAE to check if a type is an signed integer
template<typename T, typename U = void>
using require_signed_integer = typename boost::enable_if_<boost::is_signed<T>::value, U>::type;

//! \brief Utility class for SFINAE to check if a type is an array type
template<typename T, typename U = int>
using require_bounded_array = typename boost::enable_if_<boost::is_bounded_array<T>::value, U>::type;

//! \brief Require the provided type not to be an array type
template<typename T, typename U = int>
using require_not_bounded_array = typename boost::enable_if_<!boost::is_bounded_array<T>::value, U>::type;

//! \brief Require the provided pack not to be empty
template<typename... Ts>
using require_not_empty_pack = typename boost::enable_if_<sizeof...(Ts), int>::type;

} // namespace sfinae
} // namespace upd