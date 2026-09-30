#include "gecnd.h"
#include "gdweb.h"

void gamely_resolver_font_http(const char *url, void *schema_usr,
                           gamely_font_on_fetch_cb font_on_done,
                           void *font_on_done_usr) {
    (void)schema_usr;
    gamely_web_fetch(url, NULL, (gamely_fetch_done_cb)font_on_done, font_on_done_usr);
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "font_resolver:http$0",  gamely_resolver_font_http, NULL);
    gecnd_registry("set", "font_resolver:https$0", gamely_resolver_font_http, NULL);
}
