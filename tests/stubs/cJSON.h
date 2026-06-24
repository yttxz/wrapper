#ifndef CJSON_H
#define CJSON_H

typedef struct cJSON cJSON;

cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
char *cJSON_GetStringValue(const cJSON *item);

#endif
