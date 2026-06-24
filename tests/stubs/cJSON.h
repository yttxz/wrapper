#ifndef CJSON_H
#define CJSON_H

typedef struct cJSON cJSON;

cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
char *cJSON_GetStringValue(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);
void cJSON_Delete(cJSON *item);
void cJSON_free(void *object);

#endif
