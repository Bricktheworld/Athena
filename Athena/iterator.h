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
		++m_index;
		return *this;
	}
	Iterator operator++(int)
	{
		++m_index;
		return Iterator(m_container, m_index - 1);
	}

	Iterator operator--()
	{
		--m_index;
		return *this;
	}
	Iterator operator--(int)
	{
		--m_index;
		return Iterator(m_container, m_index + 1);
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

	static Iterator begin(Container *container) { return Iterator(container, 0); }
	static Iterator end(Container *container)
	{
		return Iterator(container, container->size);
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
