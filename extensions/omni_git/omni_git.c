/**
 * @file omni_git.c
 *
 */

#include <string.h>

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include <executor/spi.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>

#include <git2.h>
#include <git2/sys/alloc.h>
#include <git2/sys/config.h>
#include <git2/sys/repository.h>

#include "alloc.h"
#include "config.h"

PG_MODULE_MAGIC;

static bool libgit2_initialized = false;
static MemoryContext PersistentMemoryContext;

static void ensure_libgit2_initialized() {
  if (!libgit2_initialized) {
    PersistentMemoryContext =
        AllocSetContextCreate(TopMemoryContext, "omni_git", ALLOCSET_DEFAULT_SIZES);

    git_allocator *allocator = MemoryContextAlloc(PersistentMemoryContext, sizeof(git_allocator));
    omni_git_setup_allocator(allocator);

    git_libgit2_opts(GIT_OPT_SET_ALLOCATOR, allocator);
    git_libgit2_init();
    libgit2_initialized = true;
  }
}

PG_FUNCTION_INFO_V1(clone);

Datum clone(PG_FUNCTION_ARGS) {
  ensure_libgit2_initialized();
  text *url = PG_GETARG_TEXT_PP(0);
  SPI_connect();

  git_repository *repo;
  git_repository_new(&repo);

  git_config *config;
  git_config_new(&config);
  git_config_backend *config_backend;
  omni_git_new_config_backend(&config_backend);
  git_config_add_backend(config, config_backend, GIT_CONFIG_HIGHEST_LEVEL, repo, 0);

  git_repository_set_config(repo, config);

  git_odb *odb;
  git_odb_new(&odb);
  // git_odb_add_backend(odb, odb_backend, 0);
  git_repository_set_odb(repo, odb);

  git_remote *remote;

  git_remote_create(&remote, repo, "origin", text_to_cstring(url));

  git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
  git_remote_fetch(remote, NULL, &fetch_opts, NULL);

  git_remote_free(remote);
  git_repository_free(repo);
  git_odb_free(odb);

  SPI_finish();
  PG_RETURN_NULL();
}
