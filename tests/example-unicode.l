%{
#include <stdio.h>
#include <wchar.h>
#include <locale.h>
%}

%option noyywrap

UNICODE_CHINESE    [\u4e00-\u9fff]
UNICODE_GREEK      [\u0370-\u03FF]
UNICODE_ARROWS     [\u2190-\u21FF]
UNICODE_SYMBOLS    [\u2600-\u26FF]

%%

{UNICODE_CHINESE}+   { printf("Chinese text (%d bytes): %s\n", yyleng, yytext); }
{UNICODE_GREEK}+     { printf("Greek: %s\n", yytext); }
{UNICODE_ARROWS}     { printf("Arrow: %s\n", yytext); }
{UNICODE_SYMBOLS}    { printf("Symbol: %s\n", yytext); }
[\x00-\x7F]+         { printf("ASCII: %s\n", yytext); }

%%

int main() {
    setlocale(LC_ALL, "en_US.UTF-8");
    yylex();
    return 0;
}
