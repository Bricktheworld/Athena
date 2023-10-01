#include "types.h"
#include "math/math.h"
#include "ring_buffer.h"
#include "pool_allocator.h"
#include "job_system.h"
#include "hash_table.h"
#include "render_graph.h"

void
test_vector_operators()
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

//static Result<void, int> test_error_or_void(bool fail)
//{
//  if (fail)
//  {
//    return 0;
//  }
//  else
//  {
//    return Ok();
//  }
//}
//
//static void test_error_or()
//{
//  auto res = test_error_or_void(true);
//  ASSERT(!res && res.);
//}


static void
test_ring_buffer()
{
  const int LEN = 3;
  // We put _one_ more in the ring buffer because of the length-1 nature of the ring buffer.
  MemoryArena arena = alloc_memory_arena(sizeof(int) * (LEN + 1));
  defer { free_memory_arena(&arena); };

  RingBuffer buffer = init_ring_buffer(&arena, alignof(int));

  for (int i = 0; i < LEN; i++)
  {
    ring_buffer_push(&buffer, &i, sizeof(int));
  }

  int data = 0;
  ASSERT(!try_ring_buffer_push(&buffer, &data, sizeof(int)));
//  ASSERT(ring_buffer_is_full(buffer));

  for (int i = 0; i < LEN; i++)
  {
    int data = -1;
    ring_buffer_pop(&buffer, sizeof(int), &data);
    ASSERT(data == i);
  }
  ASSERT(ring_buffer_is_empty(buffer));

  ASSERT(!try_ring_buffer_pop(&buffer, sizeof(int), nullptr));

  for (int i = 0; i < LEN - 1; i++)
  {
    ring_buffer_push(&buffer, &i, sizeof(int));
  }

//  ASSERT(!ring_buffer_is_full(buffer));
  ASSERT(!ring_buffer_is_empty(buffer));

  ring_buffer_pop(&buffer, sizeof(int), &data);
  ASSERT(data == 0);
}

static void
test_pool_allocator()
{
  static constexpr int POOL_SIZE = 100;
  MemoryArena arena = alloc_memory_arena((sizeof(int) + sizeof(int*)) * POOL_SIZE);
  defer { free_memory_arena(&arena); };
  auto pa = init_pool<int>(&arena, POOL_SIZE);

  int* val = pool_alloc(&pa);
  *val = 1024;
  pool_free(&pa, val);

  int* allocated[POOL_SIZE];
  for (int i = 0; i < POOL_SIZE; i++)
  {
    allocated[i] = pool_alloc(&pa);
    *allocated[i] = i;
  }

  ASSERT(pa.free_count == 0);

  for (int i = ARRAY_LENGTH(allocated) - 1; i >= 0; i--)
  {
    pool_free(&pa, allocated[i]);
    for (int j = 0; j < i; j++)
    {
      ASSERT(*allocated[j] == j);
    }
  }

  ASSERT(pa.free_count == POOL_SIZE);
}

__declspec(noinline) static void
test_job_entry(uintptr_t param)
{
   int* data = reinterpret_cast<int*>(param);
   (*data)++;
   dbgln("Test");
}

static void
test_fiber()
{
  MemoryArena memory_arena = alloc_memory_arena(KiB(64) + 16);
  defer { free_memory_arena(&memory_arena); };
  void* stack = push_memory_arena_aligned(&memory_arena, KiB(64), 16);

  int data = 0;
  Fiber fiber = init_fiber(stack, KiB(64), &test_job_entry, &data);
  launch_fiber(&fiber);

  ASSERT(data == 1);
}

static void
test_hash_table()
{
  MemoryArena memory_arena = alloc_memory_arena(MiB(64));
  defer { free_memory_arena(&memory_arena); };

  // Test inserting a key-value pair into an empty HashTable
  const u64 HASH_TABLE_SIZE = 5000;
  auto table = init_hash_table<int, int>(&memory_arena, HASH_TABLE_SIZE);
  int key = 42;
  int* value = hash_table_insert(&table, key);
  *value = 100;
  ASSERT(*unwrap(hash_table_find(&table, key)) == 100);

  // Test inserting a key-value pair when the key already exists
  key = 42;
  value = hash_table_insert(&table, key);
  ASSERT(*value == 100);
  *value = 200;
  ASSERT(*unwrap(hash_table_find(&table, key)) == 200);
  ASSERT(hash_table_erase(&table, key));
  ASSERT(!hash_table_erase(&table, key));

  for (int i = 0; i < HASH_TABLE_SIZE; i++)
  {
    value = hash_table_insert(&table, i);
    *value = i;
    ASSERT(table.used == i + 1);
  }

  for (int i = 0; i < HASH_TABLE_SIZE; i++)
  {
    ASSERT(*unwrap(hash_table_find(&table, i)) == i);
  }

  for (int i = 0; i < HASH_TABLE_SIZE; i++)
  {
    ASSERT(hash_table_erase(&table, i));
  }

  for (int i = 0; i < HASH_TABLE_SIZE; i++)
  {
    ASSERT(!hash_table_erase(&table, i));
    ASSERT(!hash_table_find(&table, i));
  }

  ASSERT(!hash_table_find(&table, 0));

  // Test inserting a key-value pair with a custom type
  struct CustomType
  {
    int value;
    auto operator<=>(const CustomType& rhs) const = default;
  };

  auto custom_table = init_hash_table<CustomType, int>(&memory_arena, 32);
  CustomType custom_key;
  custom_key.value = 42;
  int* val = hash_table_insert(&custom_table, custom_key);
  ASSERT(val != nullptr);
  *val = 100;
  ASSERT(*unwrap(hash_table_find(&custom_table, custom_key)) == 100);
}

// Define a small tolerance value to account for floating-point precision errors
const f32 kF32Tolerance = 1e-6;

// Helper function to compare floating-point values with tolerance
static bool
is_close(f32 a, f32 b)
{
  return abs(a - b) < kF32Tolerance;
}

static void
test_quaternions()
{
  Quat q1(1.0f, 0.0f, 0.0f, 0.0f);
  Quat q2(0.0f, 1.0f, 0.0f, 0.0f);
  Quat result = quat_mul(q1, q2);
  Quat expected_result(0.0f, 1.0f, 0.0f, 0.0f);
  ASSERT(result.w == expected_result.w);
  ASSERT(result.x == expected_result.x);
  ASSERT(result.y == expected_result.y);
  ASSERT(result.z == expected_result.z);

  // Test case 2
  q1 = Quat(1.0f, 2.0f, 3.0f, 4.0f);
  q2 = Quat(5.0f, 6.0f, 7.0f, 8.0f);
  result = quat_mul(q1, q2);
  expected_result = Quat(-60.0f, 12.0f, 30.0f, 24.0f);
  ASSERT(result.w == expected_result.w);
  ASSERT(result.x == expected_result.x);
  ASSERT(result.y == expected_result.y);
  ASSERT(result.z == expected_result.z);
}

void test_inverse_mat4()
{
  // Test Case 1: Identity matrix should return itself
  Mat4 identity;
  Mat4 result1 = inverse_mat4(identity);
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      if (i == j)
      {
        ASSERT(fabs(result1.entries[i][j] - 1.0f) < 1e-6); // Close to 1.0
      }
      else
      {
        ASSERT(fabs(result1.entries[i][j]) < 1e-6); // Close to 0.0
      }
    }
  }

  // Test Case 2: Non-singular matrix
  Mat4 nonSingular;
  nonSingular.cols[0] = _mm_set_ps(1, 1, 1, -1);
  nonSingular.cols[1] = _mm_set_ps(1, 1, -1, 1);
  nonSingular.cols[2] = _mm_set_ps(1, -1, 1, 1);
  nonSingular.cols[3] = _mm_set_ps(-1, 1, 1, 1);
  Mat4 result2 = inverse_mat4(nonSingular);
    
  // Define the expected inverse manually or compute it using another method
  Mat4 expectedInverse;
  expectedInverse.cols[0] = _mm_set_ps(
                            0.25, 0.25, 0.25, -0.25
                            );
  expectedInverse.cols[1] = _mm_set_ps(
                            0.25, 0.25, -0.25, 0.25
                            );
  expectedInverse.cols[2] = _mm_set_ps(
                            0.25, -0.25, 0.25, 0.25
                            );
  expectedInverse.cols[3] = _mm_set_ps(
                            -0.25, 0.25, 0.25, 0.25
                            );

  // Check if the result is close to the expected inverse
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      ASSERT(fabs(result2.entries[i][j] - expectedInverse.entries[i][j]) < 1e-6); // Close to expected value
    }
  }
}

void
run_all_tests()
{
  test_quaternions();
  test_vector_operators();
  test_ring_buffer();
  test_pool_allocator();
  test_fiber();
  test_hash_table();
  test_inverse_mat4();
}
