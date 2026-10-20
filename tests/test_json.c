/* JSON 解析器自检：test_json <file.json>，失败返回非 0 */

#include "../src/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void check(int cond, const char *what)
{
    printf("%s  %s\n", cond ? "[ok]  " : "[FAIL]", what);
    if (!cond) g_fail++;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_json <file.json>\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 2; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 2; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;

    JNode *root = json_parse(buf, rd);
    check(root != NULL, "parse sample payload");
    if (!root) { free(buf); return 1; }

    check(root->type == JS_OBJ, "root is object");

    JNode *cur = json_obj(root, L"current");
    check(cur != NULL, "root.current exists");
    check(cur && cur->type == JS_OBJ, "root.current is object");

    const wchar_t *t = json_str(json_obj(cur, L"time"), L"");
    check(t[0] != 0, "current.time is string");

    double temp = json_num(json_obj(cur, L"temperature_2m"), -999);
    check(temp > -80 && temp < 80, "current.temperature_2m in range");
    printf("       temperature_2m = %.1f\n", temp);

    double hum = json_num(json_obj(cur, L"relative_humidity_2m"), -1);
    check(hum >= 0 && hum <= 100, "current.relative_humidity_2m in range");

    JNode *hourly = json_obj(root, L"hourly");
    check(json_count(json_obj(hourly, L"time")) >= 24, "hourly has >= 24 slots");

    JNode *daily = json_obj(root, L"daily");
    check(json_count(json_obj(daily, L"time")) == 7, "daily has 7 days");
    check(json_count(json_obj(daily, L"temperature_2m_max")) == 7, "daily max array aligned");
    check(json_count(json_obj(daily, L"temperature_2m_min")) == 7, "daily min array aligned");

    double dmax = json_num(json_at(json_obj(daily, L"temperature_2m_max"), 0), -999);
    double dmin = json_num(json_at(json_obj(daily, L"temperature_2m_min"), 0), -999);
    check(dmax > dmin, "day0 max > min");
    printf("       day0 range = %.1f / %.1f\n", dmin, dmax);

    check(json_obj(root, L"no_such_key") == NULL, "missing key returns NULL");
    check(json_at(json_obj(daily, L"time"), 99) == NULL, "out-of-range index returns NULL");

    json_free(root);
    free(buf);

    /* 负例：残缺 / 非法输入必须被拒绝 */
    {
        const char *bad[] = { "{", "[1,]", "{\"a\":}", "{\"a\" 1}", "", "{\"a\":\"unterminated" };
        int all = 1;
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            JNode *v = json_parse(bad[i], strlen(bad[i]));
            if (v) { all = 0; json_free(v); }
        }
        check(all, "malformed inputs rejected");
    }

    /* 转义与代理对 */
    {
        const char *s = "{\"a\":\"x\\ty\",\"b\":\"\\u4e2d\\u6587\",\"c\":\"\\ud83d\\ude00\"}";
        JNode *v = json_parse(s, strlen(s));
        check(v != NULL, "escape payload parses");
        if (v) {
            const wchar_t *a = json_str(json_obj(v, L"a"), L"");
            check(a[0] == L'x' && a[1] == L'\t' && a[2] == L'y', "\\t decoded");
            const wchar_t *b = json_str(json_obj(v, L"b"), L"");
            check(b[0] == 0x4E2D && b[1] == 0x6587, "\\uXXXX decoded (CJK)");
            const wchar_t *c = json_str(json_obj(v, L"c"), L"");
            check(c[0] == 0xD83D && c[1] == 0xDE00, "surrogate pair decoded");
            json_free(v);
        }
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
