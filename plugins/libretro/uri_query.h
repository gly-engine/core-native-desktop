#pragma once

// Simple URI query string parser: "key=value&key2=value2"
// No URL-decoding — values are used as-is.

void        uri_query_parse(const char *query);
const char *uri_query_get(const char *key);
void        uri_query_clear(void);
