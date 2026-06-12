#include "docx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- zlib/minizip ---- */
#include <zlib.h>
#include "contrib/minizip/unzip.h"   /* w64devkitのzlibに含まれていない場合は後述 */

/* ================================================================
   minizipが無い場合は zlibのgzファイル関数は使えないので
   シンプルな独自ZIPリーダーか、別途minizip.c を追加する。
   ここでは minizip を使う前提で記述。
   ================================================================ */

/* ---- 動的文字列 ---- */
typedef struct { char *buf; size_t len; size_t cap; } Str;

static void str_init(Str *s) { s->buf = malloc(1); s->buf[0]=0; s->len=0; s->cap=1; }
static void str_free(Str *s) { free(s->buf); }
static void str_append(Str *s, const char *p, size_t n) {
    if (s->len + n + 1 > s->cap) {
        while (s->len + n + 1 > s->cap) s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = 0;
}
static void str_appends(Str *s, const char *p) { str_append(s, p, strlen(p)); }

/* ---- ZIPからファイルを読み込む ---- */
static char *zip_read_file(const char *zip_path, const char *entry,
                           size_t *out_len, char *errmsg, int esz) {
    unzFile zf = unzOpen(zip_path);
    if (!zf) { snprintf(errmsg, esz, "ZIPを開けません: %s", zip_path); return NULL; }

    if (unzLocateFile(zf, entry, 1) != UNZ_OK) {
        snprintf(errmsg, esz, "エントリが見つかりません: %s", entry);
        unzClose(zf); return NULL;
    }
    unz_file_info fi;
    unzGetCurrentFileInfo(zf, &fi, NULL, 0, NULL, 0, NULL, 0);
    unzOpenCurrentFile(zf);

    char *buf = malloc(fi.uncompressed_size + 1);
    int n = unzReadCurrentFile(zf, buf, (unsigned)fi.uncompressed_size);
    buf[fi.uncompressed_size] = 0;
    *out_len = fi.uncompressed_size;

    unzCloseCurrentFile(zf);
    unzClose(zf);
    return buf;
}

/* ================================================================
   超軽量XMLパーサー
   目的: <w:ruby>, <w:rt>, <w:rubyBase>, <w:t> だけ抽出できればよい
   ================================================================ */

/* タグ名を取得（"w:ruby"など）。pはタグ先頭('<'の次) */
static void tag_name(const char *p, char *out, int outsz) {
    int i = 0;
    while (*p && *p != '>' && *p != ' ' && *p != '/' && i < outsz-1)
        out[i++] = *p++;
    out[i] = 0;
}

/* <w:t>...</w:t> の中身をすべて連結して返す（src は XML断片） */
static void collect_wt(const char *src, Str *out) {
    const char *p = src;
    while ((p = strstr(p, "<w:t"))) {
        /* xml:space="preserve" などの属性をスキップ */
        while (*p && *p != '>') p++;
        if (!*p) break;
        p++; /* '>' の次 */
        const char *end = strstr(p, "</w:t>");
        if (!end) break;
        str_append(out, p, end - p);
        p = end + 6;
    }
}

/* HTMLエスケープして追記 */
static void append_escaped(Str *out, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '<': str_appends(out, "&lt;");  break;
            case '>': str_appends(out, "&gt;");  break;
            case '&': str_appends(out, "&amp;"); break;
            case '"': str_appends(out, "&quot;"); break;
            default:  str_append(out, s, 1);     break;
        }
    }
}

/* ================================================================
   Word XML → HTML 変換
   - <w:ruby> → <ruby>BASE<rt>RT</rt></ruby>
   - <w:ruby> 以外の <w:t> → プレーンテキスト
   - <w:p> → <p>
   ================================================================ */
static void convert_xml(const char *xml, Str *html) {
    str_appends(html,
        "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\">"
        "<title>変換結果</title>"
        "<style>ruby { ruby-align: center; } rt { font-size: 0.5em; }</style>"
        "</head><body>\n");

    const char *p = xml;

    while (*p) {
        /* タグを探す */
        const char *lt = strchr(p, '<');
        if (!lt) {
            /* タグなし: テキストをエスケープして出力 */
            append_escaped(html, p);
            break;
        }

        /* タグ前のテキスト */
        if (lt > p) append_escaped(html, p);  /* ※ここは生テキストなので最小限 */
        p = lt + 1; /* '<' の次 */

        /* 閉じタグや宣言はスキップ */
        if (*p == '/' || *p == '?' || *p == '!') {
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }

        char tname[64];
        tag_name(p, tname, sizeof(tname));

        /* ---- <w:p> → <p> ---- */
        if (strcmp(tname, "w:p") == 0) {
            str_appends(html, "<p>");
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }
        /* ---- </w:p> → </p> ---- */
        /* （既に閉じタグはスキップしているのでここには来ない）*/

        /* ---- <w:ruby> ---- */
        if (strcmp(tname, "w:ruby") == 0) {
            /* w:ruby の終端 </w:ruby> を探す */
            const char *ruby_end = strstr(p, "</w:ruby>");
            if (!ruby_end) { while (*p && *p != '>') p++; if (*p) p++; continue; }
            /* ruby ブロック全体を切り出す */
            size_t block_len = ruby_end - (lt + 1);
            char *block = malloc(block_len + 1);
            memcpy(block, lt + 1, block_len);
            block[block_len] = 0;

            /* <w:rt> の中身 */
            Str rt_text; str_init(&rt_text);
            const char *rt_s = strstr(block, "<w:rt>");
            const char *rb_s = strstr(block, "<w:rubyBase>");
            if (rt_s && rb_s) {
                const char *rt_e  = strstr(rt_s,  "</w:rt>");
                const char *rb_e  = strstr(rb_s,  "</w:rubyBase>");
                /* rt */
                if (rt_e) {
                    char *rt_block = malloc(rt_e - rt_s + 1);
                    memcpy(rt_block, rt_s, rt_e - rt_s);
                    rt_block[rt_e - rt_s] = 0;
                    collect_wt(rt_block, &rt_text);
                    free(rt_block);
                }
                /* base */
                Str base_text; str_init(&base_text);
                if (rb_e) {
                    char *rb_block = malloc(rb_e - rb_s + 1);
                    memcpy(rb_block, rb_s, rb_e - rb_s);
                    rb_block[rb_e - rb_s] = 0;
                    collect_wt(rb_block, &base_text);
                    free(rb_block);
                }
                str_appends(html, "<ruby>");
                append_escaped(html, base_text.buf);
                str_appends(html, "<rt>");
                append_escaped(html, rt_text.buf);
                str_appends(html, "</rt></ruby>");
                str_free(&base_text);
            }
            str_free(&rt_text);
            free(block);
            p = ruby_end + 9; /* </w:ruby> の長さ */
            continue;
        }

        /* ---- <w:t> (rubyの外) ---- */
        if (strcmp(tname, "w:t") == 0) {
            while (*p && *p != '>') p++;
            if (*p) p++;
            const char *end = strstr(p, "</w:t>");
            if (!end) continue;
            char *txt = malloc(end - p + 1);
            memcpy(txt, p, end - p);
            txt[end - p] = 0;
            append_escaped(html, txt);
            free(txt);
            p = end + 6;
            continue;
        }

        /* それ以外のタグはスキップ */
        while (*p && *p != '>') p++;
        if (*p) p++;
    }

    str_appends(html, "\n</body></html>\n");
}

/* ================================================================
   公開API
   ================================================================ */
int docx_to_html(const char *docx_path, const char *html_path,
                 char *errmsg, int errmsg_size) {
    size_t xml_len;
    char *xml = zip_read_file(docx_path, "word/document.xml",
                               &xml_len, errmsg, errmsg_size);
    if (!xml) return -1;

    Str html; str_init(&html);
    convert_xml(xml, &html);
    free(xml);

    FILE *f = fopen(html_path, "wb");
    if (!f) {
        snprintf(errmsg, errmsg_size, "書き込み失敗: %s", html_path);
        str_free(&html); return -2;
    }
    fwrite(html.buf, 1, html.len, f);
    fclose(f);
    str_free(&html);
    return 0;
}

/* Wasm用エントリポイント
   JSからdocxのバイト列を受け取りHTML文字列を返す */
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
char *docx_to_html_wasm(const unsigned char *data, int len) {
    /* EmscriptenのメモリFS（仮想ファイル）に書き込む */
    FILE *f = fopen("/tmp/input.docx", "wb");
    fwrite(data, 1, len, f);
    fclose(f);

    char errmsg[256];
    /* 既存のロジックをそのまま呼ぶ */
    /* ※html_path は仮想FSに出力 */
    int ret = docx_to_html("/tmp/input.docx", "/tmp/output.html",
                           errmsg, sizeof(errmsg));
    if (ret != 0) return NULL;

    /* 結果を読み返してJSに返す */
    f = fopen("/tmp/output.html", "rb");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;  /* JS側でfreeする */
}