/**
 * @file concepts.h
 * @author Keunlas
 * @brief 一些特定的概念模板
 * @date 2026-08-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BECHAT_UTILS_CONCEPTS_H_
#define BECHAT_UTILS_CONCEPTS_H_

#include <concepts>
#include <type_traits>

template <typename T>
concept OneByte = sizeof(T) == 1 && std::is_trivially_copyable_v<T> &&
                  !std::is_same_v<std::remove_cv_t<T>, bool>;

template <typename T>
concept Integer = std::is_integral_v<T>;

#endif  // !BECHAT_UTILS_CONCEPTS_H_
