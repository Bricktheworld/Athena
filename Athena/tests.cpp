#include "types.h"
#include "math/math.h"

// Thank u chatgpt for writing these tests for me <3
void test_vector_operators()
{
	{
		// Test addition operator
		Vec4 a(1, 2, 3, 4);
		Vec4 b(4, 3, 2, 1);
		Vec4 c = a + b;
		ASSERT(c.x == 5 && c.y == 5 && c.z == 5 && c.w == 5);
	
		// Test subtraction operator
		Vec4 d = a - b;
		ASSERT(d.x == -3 && d.y == -1 && d.z == 1 && d.w == 3);
	
		// Test multiplication operator
		Vec4 e = a * 2;
		ASSERT(e.x == 2 && e.y == 4 && e.z == 6 && e.w == 8);
	
		// Test division operator
		Vec4 f = a / 2;
		ASSERT(f.x == 0.5 && f.y == 1 && f.z == 1.5 && f.w == 2);
	
		// Test compound addition operator
		a += b;
		ASSERT(a.x == 5 && a.y == 5 && a.z == 5 && a.w == 5);
	
		// Test compound subtraction operator
		a -= b;
		ASSERT(a.x == 1 && a.y == 2 && a.z == 3 && a.w == 4);
	
		// Test compound multiplication operator
		a *= 2;
		ASSERT(a.x == 2 && a.y == 4 && a.z == 6 && a.w == 8);
	
		// Test compound division operator
		a /= 2;
		ASSERT(a.x == 1 && a.y == 2 && a.z == 3 && a.w == 4);

		// Test unary negative operator
		a = -a;
		ASSERT(a.x == -1 && a.y == -2 && a.z == -3 && a.w == -4);
	}

	// Test addition operator
	{
		Vec3 a(1.0f, 2.0f, 3.0f);
		Vec3 b(4.0f, 5.0f, 6.0f);
		Vec3 c = a + b;
		ASSERT(c.x == 5.0f && c.y == 7.0f && c.z == 9.0f);
	}

	// Test subtraction operator
	{
		Vec3 a(1.0f, 2.0f, 3.0f);
		Vec3 b(4.0f, 5.0f, 6.0f);
		Vec3 c = a - b;
		ASSERT(c.x == -3.0f && c.y == -3.0f && c.z == -3.0f);
	}

	// Test multiplication operator
	{
		Vec3 a(1.0f, 2.0f, 3.0f);
		Vec3 c = a * 2.0f;
		ASSERT(c.x == 2.0f && c.y == 4.0f && c.z == 6.0f);
	}

	// Test division operator
	{
		Vec3 a(2.0f, 4.0f, 6.0f);
		Vec3 c = a / 2.0f;
		ASSERT(c.x == 1.0f && c.y == 2.0f && c.z == 3.0f);
	}

	// Test compound assignment operators
	{
		Vec3 a(1.0f, 2.0f, 3.0f);
		Vec3 b(4.0f, 5.0f, 6.0f);
		a += b;
		ASSERT(a.x == 5.0f && a.y == 7.0f && a.z == 9.0f);

		a -= b;
		ASSERT(a.x == 1.0f && a.y == 2.0f && a.z == 3.0f);

		a *= 2.0f;
		ASSERT(a.x == 2.0f && a.y == 4.0f && a.z == 6.0f);

		a /= 2.0f;
		ASSERT(a.x == 1.0f && a.y == 2.0f && a.z == 3.0f);
	}
	{
		// Test case 1
		Vec4 a(1.0f, 0.0f, 0.0f, 0.0f);
		Vec4 b(0.0f, 1.0f, 0.0f, 0.0f);
		Vec4 result = cross_f32(a, b);
		ASSERT(result.x == 0.0f);
		ASSERT(result.y == 0.0f);
		ASSERT(result.z == 1.0f);
		ASSERT(result.w == 0.0f);

		// Test case 2
		a = Vec4(1.0f, 2.0f, 3.0f, 0.0f);
		b = Vec4(4.0f, 5.0f, 6.0f, 0.0f);
		result = cross_f32(a, b);
		ASSERT(result.x == -3.0f);
		ASSERT(result.y == 6.0f);
		ASSERT(result.z == -3.0f);
		ASSERT(result.w == 0.0f);

		// Test case 3
		a = Vec4(1.0f, 2.0f, 3.0f, 0.0f);
		b = Vec4(-4.0f, -5.0f, -6.0f, 0.0f);
		result = cross_f32(a, b);
		ASSERT(result.x == 3.0f);
		ASSERT(result.y == -6.0f);
		ASSERT(result.z == 3.0f);
		ASSERT(result.w == 0.0f);
	}

	{
		Mat4 m = Mat4::columns(_mm_set_ps(3, 2, 1, 0),
							_mm_set_ps(7, 6, 5, 4),
							_mm_set_ps(11, 10, 9, 8),
							_mm_set_ps(15, 14, 13, 12));
		f32x4 v = _mm_set_ps(3, 2, 1, 0);
		Vec4 result = m * v;
		ASSERT(result.x == 56);
		ASSERT(result.y == 62);
		ASSERT(result.z == 68);
		ASSERT(result.w == 74);
	}

	{
		Mat4 a = Mat4::columns(_mm_set_ps(0, 1, 2, 3), _mm_set_ps(4, 5, 6, 7), _mm_set_ps(8, 9, 10, 11), _mm_set_ps(12, 13, 14, 15));
		Mat4 b = Mat4::columns(_mm_set_ps(3, 2, 1, 0), _mm_set_ps(7, 6, 5, 4), _mm_set_ps(11, 10, 9, 8), _mm_set_ps(15, 14, 13, 12));
		Mat4 c = a * b;
	
		ASSERT(c.entries[0][0] == 74);
		ASSERT(c.entries[0][1] == 68);
		ASSERT(c.entries[0][2] == 62);
		ASSERT(c.entries[0][3] == 56);
		ASSERT(c.entries[1][0] == 218);
		ASSERT(c.entries[1][1] == 196);
		ASSERT(c.entries[1][2] == 174);
		ASSERT(c.entries[1][3] == 152);
		ASSERT(c.entries[2][0] == 362);
		ASSERT(c.entries[2][1] == 324);
		ASSERT(c.entries[2][2] == 286);
		ASSERT(c.entries[2][3] == 248);
		ASSERT(c.entries[3][0] == 506);
		ASSERT(c.entries[3][1] == 452);
		ASSERT(c.entries[3][2] == 398);
		ASSERT(c.entries[3][3] == 344);
	}
}

void run_all_tests()
{
	test_vector_operators();
}
