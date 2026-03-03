#include <stdlib.h>
#include <string.h>

#include "gecnd.h"

typedef struct {
    const char* name;
    const gecnd_key_t id;
} keymap_btn_id_t;

typedef struct {
    const uint32_t nec;
    const gecnd_key_t id;
} keymap_nec_id_t;

typedef struct {
    const char *name;
    const keymap_nec_id_t *list;
    const size_t count;
} keymap_nec_list_t;

static const keymap_nec_id_t keymap_century[] = {
    {0x60f7738c, GECND_KEY_A},
    {0x60f753ac, GECND_KEY_UP},
    {0x60f79966, GECND_KEY_LEFT},
    {0x60f74bb4, GECND_KEY_DOWN},
    {0x60f7837c, GECND_KEY_RIGHT},
    {0x60f7a35c, GECND_KEY_MENU},
    /* home */
    {0x60f7a956, GECND_KEY_E},
    /* info*/
    {0x60f7c33c, GECND_KEY_F}
};

static const keymap_nec_id_t keymap_montage[] = {
    {0x33b820df, GECND_KEY_A},
    {0x33b824db, GECND_KEY_A},
    {0x33b8a45b, GECND_KEY_B},
    {0x33b8649b, GECND_KEY_C},
    {0x33b8e41b, GECND_KEY_D},
    {0x33b8a05f, GECND_KEY_UP},
    {0x33b8e01f, GECND_KEY_LEFT},
    {0x33b8609f, GECND_KEY_DOWN},
    {0x33b810ef, GECND_KEY_RIGHT},
    {0x33b840bf, GECND_KEY_MENU}
};

static const keymap_nec_id_t keymap_vivensis_dtv_3_0[] = {
    {0x4f5828d7, GECND_KEY_A},
    {0x4f580af5, GECND_KEY_A},
    {0x4f584ab5, GECND_KEY_B},
    {0x4f588a75, GECND_KEY_C},
    {0x4f58aa55, GECND_KEY_D},
    {0x4f5808f7, GECND_KEY_UP},
    {0x4f5848b7, GECND_KEY_LEFT},
    {0x4f588877, GECND_KEY_DOWN},
    {0x4f58c837, GECND_KEY_RIGHT},
    {0x4f581ce3, GECND_KEY_MENU},
    /* dtv 3.0 */
    {0x4f582ad5, GECND_KEY_E},
    /* assistent */
    {0x4f58827d, GECND_KEY_F}
};

static const keymap_nec_id_t keymap_vivensis_vx_smart[] = {
    {0xbf808877, GECND_KEY_A},
    {0xbf802cd3, GECND_KEY_UP},
    {0xbf806c93, GECND_KEY_LEFT},
    {0xbf80ac53, GECND_KEY_DOWN},
    {0xbf80ec13, GECND_KEY_RIGHT},
    {0xbf8008f7, GECND_KEY_MENU},
    /* home */
    {0xbf809867, GECND_KEY_E},
    /* info*/
    {0xbf8048b7, GECND_KEY_F}
};

/**
 * @pre must be alfabetic ordered
 */
static const keymap_btn_id_t keymap_btn[] = {
   {"a", GECND_KEY_A},
   {"b", GECND_KEY_B},
   {"c", GECND_KEY_C},
   {"d", GECND_KEY_D},
   {"down", GECND_KEY_DOWN},
   {"e", GECND_KEY_E},
   {"f", GECND_KEY_F},
   {"left", GECND_KEY_LEFT},
   {"menu", GECND_KEY_MENU},
   {"right", GECND_KEY_RIGHT},
   {"up", GECND_KEY_UP}
};

static const size_t keymap_btn_count = sizeof(keymap_btn) / sizeof(keymap_btn[0]);

/**
 * @pre must be alfabetic ordered
 */
static const keymap_nec_list_t keymap_nec[] = {
    {"century", keymap_century, sizeof(keymap_century)/sizeof(keymap_nec_id_t)},
    {"montage", keymap_montage, sizeof(keymap_montage)/sizeof(keymap_nec_id_t)},
    {"vivensis dtv 3.0", keymap_vivensis_dtv_3_0, sizeof(keymap_vivensis_dtv_3_0)/sizeof(keymap_nec_id_t)},
    {"vivensis vx smart", keymap_vivensis_vx_smart, sizeof(keymap_vivensis_vx_smart)/sizeof(keymap_nec_id_t)}
};

static const size_t keymap_nec_bytes = sizeof(keymap_nec[0]);
static const size_t keymap_nec_count = sizeof(keymap_nec) / sizeof(keymap_nec[0]);
static size_t keymap_nec_id = keymap_nec_count;

static int keymap_nec_cmp(const void *key, const void *element) {
    const char *name = (const char *)key;
    const keymap_nec_list_t *entry = (const keymap_nec_list_t *)element;
    return strcmp(name, entry->name);
}

static int keymap_btn_cmp(const void *key, const void *element) {
    const char *name = (const char *)key;
    const keymap_btn_id_t *entry = (const keymap_btn_id_t *)element;
    return strcmp(name, entry->name);
}

gecnd_key_t gecnd_key_from_name(const char* name) {
    if (!name) return GECND_KEY_NULL;
    keymap_btn_id_t *found = bsearch(name, keymap_btn, keymap_btn_count, sizeof(keymap_btn[0]), keymap_btn_cmp);
    return found ? found->id : GECND_KEY_NULL;
}

const char *gecnd_nec_get_class(uint32_t i) {
    if (i >= keymap_nec_count) {
        return NULL;
    }

    return keymap_nec[i].name;
}

bool gecnd_nec_set_class(const char *name) {
    if (!name) {
        return false;
    }

    keymap_nec_list_t *found = bsearch(name, keymap_nec, keymap_nec_count, keymap_nec_bytes, keymap_nec_cmp);

    if (!found) {
        return false;
    }

    keymap_nec_id = (size_t)(found - keymap_nec);
    return true;
}

uint32_t gecnd_nec_get_btn(uint32_t nec) {
    if (keymap_nec_id >= keymap_nec_count) {
        return 0;
    }

    const keymap_nec_list_t *keymap = &keymap_nec[keymap_nec_id];
    for (uint32_t i = 0; i < keymap->count; i++) {
        if (keymap->list[i].nec == nec) return keymap->list[i].id;
    }

    return 0;
}

const char* gecnd_nec_get_key(uint32_t nec) {
    uint32_t btn_id = gecnd_nec_get_btn(nec);

    if (btn_id == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < keymap_btn_count; i++) {
        if (keymap_btn[i].id == btn_id) return keymap_btn[i].name;
    }

    return NULL;
}
