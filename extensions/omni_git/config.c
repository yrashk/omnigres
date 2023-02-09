// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on

#include "config.h"

struct config_omni_backend {
  git_config_backend parent;
};

static int cfg_open(struct git_config_backend *, git_config_level_t level,
                    const git_repository *repo) {
  return 0;
}
static int cfg_get(struct git_config_backend *, const char *key, git_config_entry **entry) {
  ereport(NOTICE, errmsg("[%s]", key));
  return 0;
}
static int cfg_set(struct git_config_backend *, const char *key, const char *value) {
  ereport(NOTICE, errmsg("[%s] = %s", key, value));
  return 0;
}

/*
int GIT_CALLBACK(set_multivar)(git_config_backend *cfg, const char *name, const char *regexp,
                               const char *value);
int GIT_CALLBACK(del)(struct git_config_backend *, const char *key);
int GIT_CALLBACK(del_multivar)(struct git_config_backend *, const char *key, const char *regexp);
int GIT_CALLBACK(iterator)(git_config_iterator **, struct git_config_backend *);
int GIT_CALLBACK(snapshot)(struct git_config_backend **, struct git_config_backend *);
int GIT_CALLBACK(lock)(struct git_config_backend *);
int GIT_CALLBACK(unlock)(struct git_config_backend *, int success);
void GIT_CALLBACK(free)(struct git_config_backend *);
*/

int omni_git_new_config_backend(git_config_backend **out) {
  config_omni_backend *backend = palloc(sizeof(config_omni_backend));
  backend->parent = (git_config_backend){.open = cfg_open, .get = cfg_get, .set = cfg_set};

  return 0;
}