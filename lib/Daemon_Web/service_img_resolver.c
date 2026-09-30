#include "gecnd.h"
#include "gdweb.h"

void gamely_resolver_image_http(const char *url, void *schema_usr,
                            gamely_img_on_fetch_cb img_on_done,
                            void *img_on_done_usr) {
    (void)schema_usr;
    gamely_web_fetch(url, NULL, (gamely_fetch_done_cb)img_on_done, img_on_done_usr);
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_resolver:http$0",  gamely_resolver_image_http, NULL);
    gecnd_registry("set", "image_resolver:https$0", gamely_resolver_image_http, NULL);
}
