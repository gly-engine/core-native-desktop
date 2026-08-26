#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

/* Runtime backend selection.
 *
 * Every selectable backend publishes its init function under "backend:<name>"
 * from a library constructor, so the candidate list is just a registry prefix
 * walk. A backend that cannot start reports why by setting "error:backend";
 * the hook below captures that, which is what tells the selector the attempt
 * failed. That signal is a channel only backends write to during their own
 * init, unlike the application error buffer, which unrelated code also
 * appends to and so cannot be used to judge an attempt.
 *
 * Reasons accumulate across candidates and only reach the application error if
 * nothing came up at all: when a backend does start, the ones before it were
 * fallback noise, not failures worth reporting. */

static struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   failed;   /* an error:backend arrived during the current attempt */
} s_reasons;

static void reasons_append(const char *msg) {
    if (!msg || !*msg) return;

    size_t sep   = s_reasons.len ? 1 : 0;
    size_t need  = strlen(msg);
    size_t total = s_reasons.len + sep + need + 1;

    if (total > s_reasons.cap) {
        size_t cap = s_reasons.cap ? s_reasons.cap * 2 : 128;
        while (cap < total) cap *= 2;
        char *nb = realloc(s_reasons.buf, cap);
        if (!nb) return;
        s_reasons.buf = nb;
        s_reasons.cap = cap;
    }

    if (sep) s_reasons.buf[s_reasons.len++] = '\n';
    memcpy(s_reasons.buf + s_reasons.len, msg, need + 1);
    s_reasons.len += need;
}

static void reasons_clear(void) {
    free(s_reasons.buf);
    s_reasons.buf = NULL;
    s_reasons.len = 0;
    s_reasons.cap = 0;
    s_reasons.failed = false;
}

static void on_backend_error(const char *key, void *value, void *usr) {
    (void)key; (void)usr;
    s_reasons.failed = true;
    reasons_append((const char *)value);
}

typedef struct {
    gecnd_t    *gly;
    const char *only;     /* --backend: try this one and nothing else */
    const char *chosen;
    bool        done;
} backend_try_t;

static void try_backend(const char *key, void *value, void *usr) {
    backend_try_t *t = (backend_try_t *)usr;
    if (t->done) return;   /* one is already up: ignore the rest of the walk */
    if (!value) return;

    const char *name = key + (sizeof("backend:") - 1);
    if (strchr(name, ':')) return;                       /* sub-key, not a backend */
    if (t->only && strcmp(name, t->only) != 0) return;   /* forced to another one */

    s_reasons.failed = false;
    ((void (*)(gecnd_t *))value)(t->gly);
    if (s_reasons.failed) return;

    t->chosen = name;
    t->done   = true;
}

bool gecnd_boot_backend(gecnd_t *gly) {
    const char *forced = NULL;
    gecnd_registry("get", "core:backend", (void *)&forced, NULL);

    backend_try_t t = { .gly = gly, .only = forced };
    gecnd_registry("get", "backend:*", (void *)try_backend, &t);

    if (!t.done) {
        if (s_reasons.len) gecnd_add_error(gly, "%s", s_reasons.buf);
        if (forced) gecnd_add_error(gly, "backend '%s' is unavailable", forced);
        else        gecnd_add_error(gly, "no usable backend");
        reasons_clear();
        return false;
    }

    reasons_clear();
    gecnd_registry("set", "core:backend", (void *)t.chosen, "strdup=val");
    return true;
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("hook", "error:backend", (void *)on_backend_error, NULL);
}
