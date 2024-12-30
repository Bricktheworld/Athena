#pragma once
#include "types.h"

template <typename V>
struct Ok
{
  Ok(const V& val)
  {
    memmove(buffer, &val, sizeof(V));
  }

  alignas(V) byte buffer[sizeof(V)]{0};
};

template <>
struct Ok<void>
{
};

template <typename E>
struct Err
{
  Err(const E& val)
  {
    memmove(buffer, &val, sizeof(E));
  }

  alignas(E) byte buffer[sizeof(E)]{0};
};

template <typename V, typename E>
struct Result
{
  Result() = default;
  Result(const Ok<V>& v)
  {
    memmove(m_Ok, v.buffer, sizeof(V));
  }

  Result(const Err<E>& e)
  {
    memmove(m_Err, e.buffer, sizeof(E));
    m_HasError = true;
  }

  operator bool() const
  {
    return !m_HasError;
  }

  V& value()
  {
    ASSERT_MSG_FATAL(!m_HasError, "Attempting to unwrap result that has an error!");
    return *reinterpret_cast<V*>(m_Ok);
  }

  const V& value() const
  {
    ASSERT_MSG_FATAL(!m_HasError, "Attempting to unwrap result that has an error!");
    return *reinterpret_cast<V*>(m_Ok);
  }

  E& error()
  {
    ASSERT_MSG_FATAL(m_HasError, "Attempting to get error from result that is ok!");
    return *reinterpret_cast<E*>(m_Err);
  }

  const E& error() const
  {
    ASSERT_MSG_FATAL(m_HasError, "Attempting to get error from result that is ok!");
    return *reinterpret_cast<E*>(m_Err);
  }

  union
  {
    alignas(V) byte m_Ok[sizeof(V)];
    alignas(E) byte m_Err[sizeof(E)];
  };
  bool m_HasError = false;
};

template <typename E>
struct Result<void, E>
{
  Result() = default;
  Result(Ok<void> _) {}
  Result(const Err<E>& e)
  {
    memmove(m_Err, e.buffer, sizeof(E));
    m_HasError = true;
  }

  operator bool() const
  {
    return !m_HasError;
  }

  alignas(E) byte m_Err[sizeof(E)];
  bool m_HasError = false;
};

