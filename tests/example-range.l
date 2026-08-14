%{
#include <stdio.h>
#include <locale.h>
%}

%option noyywrap

/* Regression range: the low endpoint is NOT 0x80-aligned, which used to
 * drop the [0x80..low-tail) slice of code points inside the range, e.g.
 * U+4E40 "乀" and U+4F60 "你".  ("世" U+4E16 is below U+4E2D and must
 * stay unmatched.) */
UNICODE [\u4e2d-\u9fff]

%%

{UNICODE}+   { printf("CJK: %s\n", yytext); }
[\x00-\x7F]+ { printf("ASCII: %s\n", yytext); }

%%

int main() {
    setlocale(LC_ALL, "en_US.UTF-8");
    yylex();
    return 0;
}
