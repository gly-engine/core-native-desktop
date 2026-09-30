#include "gecnd.h"

extern gecnd_api_t *api;


void url_env_set(const char *url);
const char *url_env_get(const char *key);
const char *url_opt_get(const char *key);

gecnd_fsm_t state_wanted(void);