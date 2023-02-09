#ifndef OMNI_GIT_CONFIG_H
#define OMNI_GIT_CONFIG_H

#include <git2.h>
#include <git2/sys/config.h>

typedef struct config_omni_backend config_omni_backend;

int omni_git_new_config_backend(git_config_backend **out);

extern int git_config_backend_from_string(git_config_backend **out, const char *cfg, size_t len);

#endif // OMNI_GIT_CONFIG_H