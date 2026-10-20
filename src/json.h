#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <wchar.h>

typedef enum { JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ } JType;

typedef struct JNode JNode;
struct JNode {
    JType     type;
    double    num;
    wchar_t  *str;
    JNode   **items;
    wchar_t **keys;
    int       count;
};

JNode         *json_parse(const char *s, size_t n);
void           json_free(JNode *v);
JNode         *json_obj(const JNode *o, const wchar_t *key);
JNode         *json_at(const JNode *a, int i);
int            json_count(const JNode *v);
double         json_num(const JNode *v, double def);
const wchar_t *json_str(const JNode *v, const wchar_t *def);

#endif
