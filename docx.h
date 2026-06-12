#ifndef DOCX_H
#define DOCX_H

/* docxからHTMLを生成してファイルに書き出す
   戻り値: 0=成功, 非0=失敗 */
int docx_to_html(const char *docx_path, const char *html_path,
                 char *errmsg, int errmsg_size);

#endif