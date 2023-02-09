#ifndef OMNI_GIT_ALLOC_H
#define OMNI_GIT_ALLOC_H

#include <git2/sys/alloc.h>

int omni_git_setup_allocator(git_allocator *allocator);
#endif // OMNI_GIT_ALLOC_H