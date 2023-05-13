#pragma once
#include "types.h"

struct NoneT 
{
	explicit constexpr NoneT() = default;
};

static constexpr NoneT None = NoneT();

template <typename T>
struct Option
{
	Option() = default;
	Option(NoneT none) {}
	Option(const T& v) : value(v), m_has_value(true) {}

	operator bool() const { return m_has_value; }

	T value;

private:
	bool m_has_value = false;
};

template <typename T>
struct Option<T*>
{
	Option() = default;
	Option(NoneT none) {}
	Option(T* ptr) : value(ptr) {}

	operator bool() const { return value != nullptr; }

	T* value = nullptr;
};


template <typename T>
T& unwrap(Option<T>& optional)
{
	ASSERT(optional);
	return optional.value;
}

template <typename T>
T unwrap(Option<T>&& optional)
{
	ASSERT(optional);
	return optional.value;
}

template <typename T>
T unwrap(const Option<T>& optional)
{
	ASSERT(optional);
	return optional.value;
}

template <typename T>
T unwrap_or(Option<T> optional, T replacement)
{
	if (!optional)
		return replacement;

	return optional.value;
}

template <typename T, typename F>
T unwrap_or(Option<T> optional, F callback)
{
	if (!optional)
		return callback();

	return optional.value;
}

#define unwrap_else(optional) optional.value; if (optional) {} else