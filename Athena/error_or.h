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
	Result(const Ok<V>& v)
	{
		memmove(ok, v.buffer, sizeof(V));
	}

	Result(const Err<E>& e)
	{
		memmove(err, e.buffer, sizeof(E));
		has_error = true;
	}

	operator bool() const
	{
		return !has_error;
	}

	V& value()
	{
		ASSERT(!has_error);
		return *reinterpret_cast<V*>(ok);
	}

	const V& value() const
	{
		ASSERT(!has_error);
		return *reinterpret_cast<V*>(ok);
	}

	union
	{
		alignas(V) byte ok[sizeof(V)];
		alignas(E) byte err[sizeof(E)];
	};
	bool has_error = false;
};

template <typename E>
struct Result<void, E>
{
	Result() {}
	Result(Ok<void> _) {}
	Result(const Err<E>& e)
	{
		memmove(err, e.buffer, sizeof(E));
		has_error = true;
	}

	operator bool() const
	{
		return !has_error;
	}

	alignas(E) byte err[sizeof(E)];
	bool has_error = false;
};


