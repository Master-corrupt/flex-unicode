%{
#include <stdio.h>
%}

%option noyywrap

SINGLE_HAN   \u4e2d
EMOJI        \u{1F600}
MIXED        [a-z\u4e2d\u6587]
SPAN_2_3     [\u00e9-\u4e2d]
UPPER        [\u0041-\u005A]

%%

{SINGLE_HAN}     { printf("single-han: %s\n", yytext); }
{EMOJI}          { printf("emoji: %s\n", yytext); }
{MIXED}+         { printf("mixed: %s\n", yytext); }
{SPAN_2_3}+      { printf("span2to3: %s\n", yytext); }
{UPPER}+         { printf("upper: %s\n", yytext); }
[ ]+             { /* skip spaces */ }
\n               { }

%%

int main() { yylex(); return 0; }
