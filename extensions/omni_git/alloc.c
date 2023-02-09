#include <string.h>

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include "alloc.h"

static void *gmalloc(size_t n, const char *file, int line) { return palloc(n); }
static void *gcalloc(size_t nelem, size_t elsize, const char *file, int line) {
  return palloc0(nelem * elsize);
}
static char *gstrdup(const char *str, const char *file, int line) { return pstrdup(str); }

static char *gstrndup(const char *str, size_t n, const char *file, int line) {
  char *nstr;
  Size len = strnlen(str, n);

  nstr = (char *)MemoryContextAlloc(CurrentMemoryContext, len + 1);

  memcpy(nstr, str, len);
  nstr[len] = 0;

  return nstr;
}

static char *gsubstrdup(const char *str, size_t n, const char *file, int line) {
  char *nstr;
  Size len = n;

  nstr = (char *)MemoryContextAlloc(CurrentMemoryContext, len + 1);

  memcpy(nstr, str, len);
  nstr[len] = 0;

  return nstr;
}

static void *grealloc(void *ptr, size_t size, const char *file, int line) {
  if (ptr == NULL) {
    return palloc(size);
  } else {
    return repalloc(ptr, size);
  }
}

static void *greallocarray(void *ptr, size_t nelem, size_t elsize, const char *file, int line) {
  if (ptr == NULL) {
    return palloc(nelem * elsize);
  } else {
    return repalloc(ptr, nelem * elsize);
  }
}

static void *gmallocarray(size_t nelem, size_t elsize, const char *file, int line) {
  return palloc(nelem * elsize);
}

static void gfree(void *ptr) {
  if (ptr != NULL) {
    pfree(ptr);
  }
}

int omni_git_setup_allocator(git_allocator *allocator) {
  *allocator = (git_allocator){.gmalloc = gmalloc,
                               .gcalloc = gcalloc,
                               .gstrdup = gstrdup,
                               .gstrndup = gstrndup,
                               .gsubstrdup = gsubstrdup,
                               .grealloc = grealloc,
                               .greallocarray = greallocarray,
                               .gmallocarray = gmallocarray,
                               .gfree = gfree};
  return 0;
}
