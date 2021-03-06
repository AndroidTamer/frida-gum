/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <ole.andre.ravnas@tillitech.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testutil.h"

#include "gummemory-priv.h"

#define MEMORY_TESTCASE(NAME) \
    void test_memory_ ## NAME (void)
#define MEMORY_TESTENTRY(NAME) \
    TEST_ENTRY_SIMPLE ("Core/Memory", test_memory, NAME)

TEST_LIST_BEGIN (memory)
  MEMORY_TESTENTRY (read_from_valid_address_should_succeed)
  MEMORY_TESTENTRY (read_from_invalid_address_should_fail)
  MEMORY_TESTENTRY (write_to_valid_address_should_succeed)
  MEMORY_TESTENTRY (write_to_invalid_address_should_fail)
  MEMORY_TESTENTRY (match_pattern_from_string_does_proper_validation)
  MEMORY_TESTENTRY (scan_range_finds_three_exact_matches)
  MEMORY_TESTENTRY (scan_range_finds_three_wildcarded_matches)
  MEMORY_TESTENTRY (is_memory_readable_handles_mixed_page_protections)
  MEMORY_TESTENTRY (alloc_n_pages_returns_aligned_rw_address)
  MEMORY_TESTENTRY (alloc_n_pages_near_returns_aligned_rw_address_within_range)
  MEMORY_TESTENTRY (mprotect_handles_page_boundaries)
TEST_LIST_END ()

typedef struct _TestForEachContext {
  gboolean value_to_return;
  guint number_of_calls;

  gpointer expected_address[3];
  guint expected_size;
} TestForEachContext;

static gboolean match_found_cb (GumAddress address, gsize size,
    gpointer user_data);

MEMORY_TESTCASE (read_from_valid_address_should_succeed)
{
  guint8 magic[2] = { 0x13, 0x37 };
  gsize n_bytes_read;
  guint8 * result;

  result = gum_memory_read (GUM_ADDRESS (magic), sizeof (magic), &n_bytes_read);
  g_assert (result != NULL);

  g_assert_cmpint (n_bytes_read, ==, sizeof (magic));

  g_assert_cmphex (result[0], ==, magic[0]);
  g_assert_cmphex (result[1], ==, magic[1]);

  g_free (result);
}

MEMORY_TESTCASE (read_from_invalid_address_should_fail)
{
  GumAddress invalid_address = 0x42;
  g_assert (gum_memory_read (invalid_address, 1, NULL) == NULL);
}

MEMORY_TESTCASE (write_to_valid_address_should_succeed)
{
  guint8 bytes[3] = { 0x00, 0x00, 0x12 };
  guint8 magic[2] = { 0x13, 0x37 };
  gboolean success;

  success = gum_memory_write (GUM_ADDRESS (bytes), magic, sizeof (magic));
  g_assert (success);

  g_assert_cmphex (bytes[0], ==, 0x13);
  g_assert_cmphex (bytes[1], ==, 0x37);
  g_assert_cmphex (bytes[2], ==, 0x12);
}

MEMORY_TESTCASE (write_to_invalid_address_should_fail)
{
  guint8 bytes[3] = { 0x00, 0x00, 0x12 };
  GumAddress invalid_address = 0x42;
  g_assert (gum_memory_write (invalid_address, bytes, sizeof (bytes)) == FALSE);
}

#define GUM_PATTERN_NTH_TOKEN(p, n) \
    ((GumMatchToken *) g_ptr_array_index (p->tokens, n))
#define GUM_PATTERN_NTH_TOKEN_NTH_BYTE(p, n, b) \
    (g_array_index (((GumMatchToken *) g_ptr_array_index (p->tokens, \
        n))->bytes, guint8, b))

MEMORY_TESTCASE (match_pattern_from_string_does_proper_validation)
{
  GumMatchPattern * pattern;

  pattern = gum_match_pattern_new_from_string ("1337");
  g_assert (pattern != NULL);
  g_assert_cmpuint (pattern->size, ==, 2);
  g_assert_cmpuint (pattern->tokens->len, ==, 1);
  g_assert_cmpuint (GUM_PATTERN_NTH_TOKEN (pattern, 0)->bytes->len, ==, 2);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 0, 0), ==, 0x13);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 0, 1), ==, 0x37);
  gum_match_pattern_free (pattern);

  pattern = gum_match_pattern_new_from_string ("13 37");
  g_assert (pattern != NULL);
  g_assert_cmpuint (pattern->size, ==, 2);
  g_assert_cmpuint (pattern->tokens->len, ==, 1);
  g_assert_cmpuint (GUM_PATTERN_NTH_TOKEN (pattern, 0)->bytes->len, ==, 2);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 0, 0), ==, 0x13);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 0, 1), ==, 0x37);
  gum_match_pattern_free (pattern);

  pattern = gum_match_pattern_new_from_string ("1 37");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("13 3");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("13+37");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("13 ?? 37");
  g_assert (pattern != NULL);
  g_assert_cmpuint (pattern->size, ==, 3);
  g_assert_cmpuint (pattern->tokens->len, ==, 3);
  g_assert_cmpuint (GUM_PATTERN_NTH_TOKEN (pattern, 0)->bytes->len, ==, 1);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 0, 0), ==, 0x13);
  g_assert_cmpuint (GUM_PATTERN_NTH_TOKEN (pattern, 1)->bytes->len, ==, 1);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 1, 0), ==, 0x42);
  g_assert_cmpuint (GUM_PATTERN_NTH_TOKEN (pattern, 2)->bytes->len, ==, 1);
  g_assert_cmphex (GUM_PATTERN_NTH_TOKEN_NTH_BYTE (pattern, 2, 0), ==, 0x37);
  gum_match_pattern_free (pattern);

  pattern = gum_match_pattern_new_from_string ("13 ? 37");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("??");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("?? 13");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("13 ??");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string (" ");
  g_assert (pattern == NULL);

  pattern = gum_match_pattern_new_from_string ("");
  g_assert (pattern == NULL);
}

MEMORY_TESTCASE (scan_range_finds_three_exact_matches)
{
  guint8 buf[] = {
    0x13, 0x37,
    0x12,
    0x13, 0x37,
    0x13, 0x37
  };
  GumMemoryRange range;
  GumMatchPattern * pattern;
  TestForEachContext ctx;

  range.base_address = GUM_ADDRESS (buf);
  range.size = sizeof (buf);

  pattern = gum_match_pattern_new_from_string ("13 37");
  g_assert (pattern != NULL);

  ctx.expected_address[0] = buf + 0;
  ctx.expected_address[1] = buf + 2 + 1;
  ctx.expected_address[2] = buf + 2 + 1 + 2;
  ctx.expected_size = 2;

  ctx.number_of_calls = 0;
  ctx.value_to_return = TRUE;
  gum_memory_scan (&range, pattern, match_found_cb, &ctx);
  g_assert_cmpuint (ctx.number_of_calls, ==, 3);

  ctx.number_of_calls = 0;
  ctx.value_to_return = FALSE;
  gum_memory_scan (&range, pattern, match_found_cb, &ctx);
  g_assert_cmpuint (ctx.number_of_calls, ==, 1);

  gum_match_pattern_free (pattern);
}

MEMORY_TESTCASE (scan_range_finds_three_wildcarded_matches)
{
  guint8 buf[] = {
    0x12, 0x11, 0x13, 0x37,
    0x12, 0x00,
    0x12, 0xc0, 0x13, 0x37,
    0x12, 0x44, 0x13, 0x37
  };
  GumMemoryRange range;
  GumMatchPattern * pattern;
  TestForEachContext ctx;

  range.base_address = GUM_ADDRESS (buf);
  range.size = sizeof (buf);

  pattern = gum_match_pattern_new_from_string ("12 ?? 13 37");
  g_assert (pattern != NULL);

  ctx.number_of_calls = 0;
  ctx.value_to_return = TRUE;

  ctx.expected_address[0] = buf + 0;
  ctx.expected_address[1] = buf + 4 + 2;
  ctx.expected_address[2] = buf + 4 + 2 + 4;
  ctx.expected_size = 4;

  gum_memory_scan (&range, pattern, match_found_cb, &ctx);

  g_assert_cmpuint (ctx.number_of_calls, ==, 3);

  gum_match_pattern_free (pattern);
}

MEMORY_TESTCASE (is_memory_readable_handles_mixed_page_protections)
{
  guint8 * pages;
  guint page_size;
  GumAddress left_guard, first_page, second_page, right_guard;

  pages = gum_alloc_n_pages (4, GUM_PAGE_RW);

  page_size = gum_query_page_size ();

  left_guard = GUM_ADDRESS (pages);
  first_page = left_guard + page_size;
  second_page = first_page + page_size;
  right_guard = second_page + page_size;

  gum_mprotect (GSIZE_TO_POINTER (left_guard), page_size, GUM_PAGE_NO_ACCESS);
  gum_mprotect (GSIZE_TO_POINTER (second_page), page_size, GUM_PAGE_RW);
  gum_mprotect (GSIZE_TO_POINTER (right_guard), page_size, GUM_PAGE_NO_ACCESS);

  g_assert (gum_memory_is_readable (first_page, 1));
  g_assert (gum_memory_is_readable (first_page + page_size - 1, 1));
  g_assert (gum_memory_is_readable (first_page, page_size));

  g_assert (gum_memory_is_readable (second_page, 1));
  g_assert (gum_memory_is_readable (second_page + page_size - 1, 1));
  g_assert (gum_memory_is_readable (second_page, page_size));

  g_assert (gum_memory_is_readable (first_page + page_size - 1, 2));
  g_assert (gum_memory_is_readable (first_page, 2 * page_size));

  g_assert (!gum_memory_is_readable (second_page + page_size, 1));
  g_assert (!gum_memory_is_readable (second_page + page_size - 1, 2));

  gum_free_pages (pages);
}

MEMORY_TESTCASE (alloc_n_pages_returns_aligned_rw_address)
{
  gpointer page;
  guint page_size;

  page = gum_alloc_n_pages (1, GUM_PAGE_RW);

  page_size = gum_query_page_size ();

  g_assert (GPOINTER_TO_SIZE (page) % page_size == 0);

  g_assert (gum_memory_is_readable (GUM_ADDRESS (page), page_size));

  g_assert_cmpuint (*((gsize *) page), ==, 0);
  *((gsize *) page) = 42;
  g_assert_cmpuint (*((gsize *) page), ==, 42);

  gum_free_pages (page);
}

MEMORY_TESTCASE (alloc_n_pages_near_returns_aligned_rw_address_within_range)
{
  GumAddressSpec as;
  guint variable_on_stack;
  gpointer page;
  guint page_size;
  gsize actual_distance;

  as.near_address = &variable_on_stack;
  as.max_distance = G_MAXINT32;

  page = gum_alloc_n_pages_near (1, GUM_PAGE_RW, &as);
  g_assert (page != NULL);

  page_size = gum_query_page_size ();

  g_assert (GPOINTER_TO_SIZE (page) % page_size == 0);

  g_assert (gum_memory_is_readable (GUM_ADDRESS (page), page_size));

  g_assert_cmpuint (*((gsize *) page), ==, 0);
  *((gsize *) page) = 42;
  g_assert_cmpuint (*((gsize *) page), ==, 42);

  actual_distance = ABS ((guint8 *) page - (guint8 *) as.near_address);
  g_assert_cmpuint (actual_distance, <=, as.max_distance);

  gum_free_pages (page);
}

MEMORY_TESTCASE (mprotect_handles_page_boundaries)
{
  guint8 * pages;
  guint page_size;

  pages = gum_alloc_n_pages (2, GUM_PAGE_NO_ACCESS);
  page_size = gum_query_page_size ();

  gum_mprotect (pages + page_size - 1, 2, GUM_PAGE_RW);
  pages[page_size - 1] = 0x13;
  pages[page_size] = 0x37;

  gum_free_pages (pages);
}

static gboolean
match_found_cb (GumAddress address,
                gsize size,
                gpointer user_data)
{
  TestForEachContext * ctx = (TestForEachContext *) user_data;

  g_assert_cmpuint (ctx->number_of_calls, <, 3);

  g_assert (address ==
      GUM_ADDRESS (ctx->expected_address[ctx->number_of_calls]));
  g_assert_cmpuint (size, ==, ctx->expected_size);

  ctx->number_of_calls++;

  return ctx->value_to_return;
}
