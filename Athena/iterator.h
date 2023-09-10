#pragma once

// I hate life... fucking kill me.
template <typename Container, typename T>
struct Iterator
{
  bool is_end() const { return m_index == Iterator::end(m_container).m_index; }
  size_t index() const { return m_index; }

  bool operator==(Iterator other) const { return m_index == other.m_index; }
  bool operator!=(Iterator other) const { return m_index != other.m_index; }
  bool operator<(Iterator other) const { return m_index < other.m_index; }
  bool operator>(Iterator other) const { return m_index > other.m_index; }
  bool operator<=(Iterator other) const { return m_index <= other.m_index; }
  bool operator>=(Iterator other) const { return m_index >= other.m_index; }

  Iterator operator+(ptrdiff_t delta) const { return Iterator{m_container, m_index + delta}; }
  Iterator operator-(ptrdiff_t delta) const { return Iterator{m_container, m_index - delta}; }

  ptrdiff_t operator-(Iterator other) const { return static_cast<ptrdiff_t>(m_index) - other.m_index; }

  Iterator operator++()
  {
    m_index = m_container->m_increment_idx(m_index);
    return *this;
  }
  Iterator operator++(int)
  {
    size_t orig = m_index;
    m_index = m_container->m_increment_idx(m_index);
    return Iterator(m_container, orig);
  }

  Iterator operator--()
  {
    m_index = m_container->m_decrement_idx(m_index);
    return *this;
  }

  Iterator operator--(int)
  {
    size_t orig = m_index;
    m_index = m_container->m_decrement_idx(m_index);
    return Iterator(m_container, orig);
  }

  const T &operator*() const { return (*m_container)[m_index]; }
  T &operator*() { return (*m_container)[m_index]; }

  auto operator->() const { return m_container + m_index; }
  auto operator->() { return m_container + m_index; }

  Iterator &operator=(const Iterator &other)
  {
    if (this == &other)
      return *this;

    m_index = other.m_index;
    return *this;
  }

  Iterator(const Iterator &obj) = default;

  static Iterator begin(Container *container) { return Iterator(container, container->m_begin_idx()); }
  static Iterator end(Container *container)
  {
    return Iterator(container, container->m_end_idx());
  }

  Iterator(Container *container, size_t index) : m_container(container), m_index(index) {}
  Container* m_container = nullptr;
  size_t m_index = 0;
};

#define USE_ITERATOR(Container, T) \
  Iterator<Container, T> begin() { return Iterator<Container, T>::begin(this);  } \
  Iterator<Container, T> end() { return Iterator<Container, T>::end(this);  } \
  Iterator<const Container, const T> begin() const { return Iterator<const Container, const T>::begin(this);  } \
  Iterator<const Container, const T> end() const { return Iterator<const Container, const T>::end(this);  } \
  private: \
  friend Iterator<Container, T>; \
  friend Iterator<const Container, const T>; \
  size_t m_increment_idx(size_t idx) const { return idx + 1; } \
  size_t m_decrement_idx(size_t idx) const { return idx - 1; } \
  size_t m_begin_idx() const { return 0; }\
  size_t m_end_idx() const { return size; }


#define USE_CONST_ITERATOR(Container, T) \
  Iterator<const Container, const T> begin() const { return Iterator<const Container, const T>::begin(this);  } \
  Iterator<const Container, const T> end() const { return Iterator<const Container, const T>::end(this);  } \
  private: \
  friend Iterator<const Container, const T>; \
  size_t m_increment_idx(size_t idx) const { return idx + 1; } \
  size_t m_decrement_idx(size_t idx) const { return idx - 1; } \
  size_t m_begin_idx() const { return 0; }\
  size_t m_end_idx() const { return size; }
