%option reentrant noyywrap nounput noinput nodefault never-interactive 8bit
%option prefix="lab"
%option extra-type="lab::frontend::LexerState*"
%x STR COMMENT

%{
#include "frontend/lexer.hpp"
#include <string>

#define YY_DECL lab::frontend::Parser::symbol_type lab_scan_token(yyscan_t yyscanner)
#define YY_USER_ACTION yyextra->advance(yytext, static_cast<std::size_t>(yyleng));
// Flex 按最长匹配取 token；长度相同才按规则先后选择，故关键字放在标识符前。
using Parser = lab::frontend::Parser;
using lab::DiagnosticError;
%}

%%
[ \t\v\f\r\n]+       { /* Whitespace still advances source positions. */ }
"//"[^\r\n]*        { }
"/*"                 { yyextra->opening = yyextra->token; BEGIN(COMMENT); }
<COMMENT>"*/"        { BEGIN(INITIAL); }
<COMMENT>[^*\r\n]+   { }
<COMMENT>"*"         { }
<COMMENT>[\r\n]      { }
<COMMENT><<EOF>>     { throw DiagnosticError(yyextra->opening, "lexical error", "unterminated block comment"); }

\"                   { yyextra->opening = yyextra->token; yyextra->literal.clear(); BEGIN(STR); }
<STR>\"              {
    const lab::SourceSpan span{yyextra->opening.begin, yyextra->token.end};
    BEGIN(INITIAL);
    return Parser::make_STRING_LITERAL(std::move(yyextra->literal), span);
}
<STR>\\\"            { yyextra->literal.push_back('"'); }
<STR>\\\\            { yyextra->literal.push_back('\\'); }
<STR>\\n             { yyextra->literal.push_back('\n'); }
<STR>\\r             { yyextra->literal.push_back('\r'); }
<STR>\\t             { yyextra->literal.push_back('\t'); }
<STR>\\0             { yyextra->literal.push_back('\0'); }
<STR>\\x[0-9a-fA-F]{2} {
    auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    const int value = digit(yytext[2]) * 16 + digit(yytext[3]);
    if (value > 127) throw DiagnosticError(yyextra->token, "lexical error", "string escape is outside ASCII range");
    yyextra->literal.push_back(static_cast<char>(value));
}
<STR>[^"\\\x00-\x1f\x80-\xff]+ { yyextra->literal.append(yytext, static_cast<std::size_t>(yyleng)); }
<STR>\\[^\r\n]       { throw DiagnosticError(yyextra->token, "lexical error", "invalid string escape"); }
<STR>\\              { throw DiagnosticError(yyextra->token, "lexical error", "incomplete string escape"); }
<STR>[\r\n]          { throw DiagnosticError(yyextra->opening, "lexical error", "unterminated string literal"); }
<STR>[\x00-\x1f\x80-\xff] {
    throw DiagnosticError(yyextra->token, "lexical error", "string requires ASCII bytes and escaped control characters");
}
<STR><<EOF>>         { throw DiagnosticError(yyextra->opening, "lexical error", "unterminated string literal"); }

"def"                { return Parser::make_DEF(yyextra->token); }
"var"                { return Parser::make_VAR(yyextra->token); }
"struct"             { return Parser::make_STRUCT(yyextra->token); }
"if"                 { return Parser::make_IF(yyextra->token); }
"elif"               { return Parser::make_ELIF(yyextra->token); }
"else"               { return Parser::make_ELSE(yyextra->token); }
"while"              { return Parser::make_WHILE(yyextra->token); }
"for"                { return Parser::make_FOR(yyextra->token); }
"return"             { return Parser::make_RETURN(yyextra->token); }
"parallel"           { return Parser::make_PARALLEL(yyextra->token); }
"map"                { return Parser::make_MAP(yyextra->token); }
"in"                 { return Parser::make_IN(yyextra->token); }
"yield"              { return Parser::make_YIELD(yyextra->token); }
"new"                { return Parser::make_NEW(yyextra->token); }
"float"              { return Parser::make_FLOAT(yyextra->token); }
"int"                { return Parser::make_INT(yyextra->token); }
"bool"               { return Parser::make_BOOL(yyextra->token); }
"string"             { return Parser::make_STRING(yyextra->token); }
"void"               { return Parser::make_VOID(yyextra->token); }
"true"               { return Parser::make_TRUE(yyextra->token); }
"false"              { return Parser::make_FALSE(yyextra->token); }

[A-Za-z_][A-Za-z0-9_]* { return Parser::make_IDENTIFIER(std::string(yytext, yyleng), yyextra->token); }
([0-9]+\.[0-9]+([eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+) {
    return Parser::make_FLOAT_LITERAL(std::string(yytext, yyleng), yyextra->token);
}
[0-9]+               { return Parser::make_INTEGER(std::string(yytext, yyleng), yyextra->token); }

"->"                 { return Parser::make_ARROW(yyextra->token); }
"=="                 { return Parser::make_EQ(yyextra->token); }
"!="                 { return Parser::make_NE(yyextra->token); }
"<="                 { return Parser::make_LE(yyextra->token); }
">="                 { return Parser::make_GE(yyextra->token); }
"&&"                 { return Parser::make_AND(yyextra->token); }
"||"                 { return Parser::make_OR(yyextra->token); }
"("                  { return Parser::make_LPAREN(yyextra->token); }
")"                  { return Parser::make_RPAREN(yyextra->token); }
"{"                  { return Parser::make_LBRACE(yyextra->token); }
"}"                  { return Parser::make_RBRACE(yyextra->token); }
"["                  { return Parser::make_LBRACKET(yyextra->token); }
"]"                  { return Parser::make_RBRACKET(yyextra->token); }
":"                  { return Parser::make_COLON(yyextra->token); }
";"                  { return Parser::make_SEMICOLON(yyextra->token); }
","                  { return Parser::make_COMMA(yyextra->token); }
"."                  { return Parser::make_DOT(yyextra->token); }
"="                  { return Parser::make_ASSIGN(yyextra->token); }
"+"                  { return Parser::make_PLUS(yyextra->token); }
"-"                  { return Parser::make_MINUS(yyextra->token); }
"*"                  { return Parser::make_STAR(yyextra->token); }
"/"                  { return Parser::make_SLASH(yyextra->token); }
"%"                  { return Parser::make_PERCENT(yyextra->token); }
"<"                  { return Parser::make_LT(yyextra->token); }
">"                  { return Parser::make_GT(yyextra->token); }
"!"                  { return Parser::make_NOT(yyextra->token); }

<<EOF>>              { return Parser::make_END(lab::SourceSpan{yyextra->position, yyextra->position}); }
.                    {
    static constexpr char hex[] = "0123456789ABCDEF";
    const unsigned char byte = static_cast<unsigned char>(yytext[0]);
    std::string message = "unexpected byte 0x";
    message += hex[byte >> 4]; message += hex[byte & 15];
    throw DiagnosticError(yyextra->token, "lexical error", message);
}
%%
