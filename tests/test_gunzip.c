/* gzip 解压自检：test_gunzip <file.gz>
 * 样本是和风天气 /v7/weather/24h 的真实响应（服务端强制 gzip） */

#include "../src/app.h"
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

static unsigned char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *n = rd;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_gunzip <file.gz>\n");
        return 2;
    }

    size_t n = 0;
    unsigned char *gz = read_file(argv[1], &n);
    if (!gz) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

    check(n > 10 && gz[0] == 0x1f && gz[1] == 0x8b, "input is gzip");

    size_t dn = 0;
    unsigned char *plain = gz_decompress(gz, n, &dn);
    check(plain != NULL, "decompress ok");
    if (!plain) { free(gz); return 1; }

    printf("       %zu bytes -> %zu bytes\n", n, dn);
    check(dn > 1000, "output size plausible");

    JNode *root = json_parse((const char *)plain, dn);
    check(root != NULL, "output parses as JSON");
    if (root) {
        const wchar_t *code = json_str(json_obj(root, L"code"), L"");
        check(!wcscmp(code, L"200"), "QWeather code == 200");

        JNode *hourly = json_obj(root, L"hourly");
        check(json_count(hourly) == 24, "hourly has 24 entries");

        JNode *first = json_at(hourly, 0);
        check(first != NULL, "hourly[0] exists");
        check(first && json_obj(first, L"temp") != NULL, "hourly[0].temp present");
        check(first && json_obj(first, L"fxTime") != NULL, "hourly[0].fxTime present");

        json_free(root);
    }

    /* 负例：未压缩数据不处理、截断数据失败 */
    {
        const char *txt = "{\"a\":1}";
        size_t on = 0;
        check(gz_decompress((const unsigned char *)txt, strlen(txt), &on) == NULL,
              "plain text rejected");
    }
    {
        size_t on = 0;
        unsigned char *d = gz_decompress(gz, n / 3, &on);
        check(d == NULL, "truncated gzip rejected");
        if (d) free(d);
    }

    free(plain);
    free(gz);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
