%require "3.8"
%skeleton "lalr1.cc"
%define api.namespace {lab::frontend}
%define api.parser.class {Parser}
%define api.value.type variant
%define api.token.constructor
%define api.token.raw
%define api.location.type {lab::SourceSpan}
%define parse.error detailed
%define parse.lac full
%locations
%parse-param {lab::frontend::ParseContext& driver}
%lex-param {lab::frontend::ParseContext& driver}

%code requires {
#include "ast/ast.hpp"
namespace lab::frontend { class ParseContext; }
}
%code {
#include "frontend/parse.hpp"
namespace {
lab::frontend::Parser::symbol_type yylex(lab::frontend::ParseContext& context) {
    return context.next();
}
lab::ast::ExprPtr binary(lab::SourceSpan span, std::string op,
                        lab::ast::ExprPtr left, lab::ast::ExprPtr right) {
    auto node = std::make_unique<lab::ast::BinaryExpr>(std::move(span));
    node->op = std::move(op);
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}
lab::ast::ExprPtr unary(lab::SourceSpan span, std::string op, lab::ast::ExprPtr value) {
    auto node = std::make_unique<lab::ast::UnaryExpr>(std::move(span));
    node->op = std::move(op);
    node->operand = std::move(value);
    return node;
}
}
}

%token END 0 "end of file"
%token <std::string> IDENTIFIER "identifier" INTEGER "integer literal" STRING_LITERAL "string literal"
%token DEF "def" VAR "var" STRUCT "struct"
%token IF "if" ELIF "elif" ELSE "else" WHILE "while" FOR "for" RETURN "return"
%token PARALLEL "parallel" MAP "map" IN "in" YIELD "yield" NEW "new"
%token INT "int" BOOL "bool" STRING "string" VOID "void" TRUE "true" FALSE "false"
%token LPAREN "(" RPAREN ")" LBRACE "{" RBRACE "}" LBRACKET "[" RBRACKET "]"
%token COLON ":" SEMICOLON ";" COMMA "," DOT "." ARROW "->"
%token ASSIGN "=" PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%"
%token EQ "==" NE "!=" LT "<" LE "<=" GT ">" GE ">=" AND "&&" OR "||" NOT "!"

%type <lab::ast::Declarations> declarations
%type <lab::ast::DeclPtr> declaration function_declaration struct_declaration
%type <lab::ast::TypeRef> type base_type cast_type
%type <std::vector<lab::ast::Parameter>> parameters parameter_list
%type <lab::ast::Parameter> parameter
%type <std::vector<lab::ast::Field>> fields
%type <lab::ast::Field> field
%type <std::unique_ptr<lab::ast::BlockStmt>> block else_body
%type <lab::ast::Statements> statements
%type <lab::ast::StmtPtr> statement variable simple initializer_optional simple_optional
%type <std::vector<lab::ast::IfBranch>> elif_branches
%type <lab::ast::ExprPtr> expression expression_optional initializer
%type <lab::ast::ExprPtr> disjunction conjunction equality comparison sum product unary postfix primary parallel_map
%type <lab::ast::Expressions> arguments argument_list
%start translation_unit

%%
translation_unit:
    declarations { driver.declarations = std::move($1); }
;
declarations:
    %empty { $$ = lab::ast::Declarations(); }
  | declarations declaration { $1.push_back(std::move($2)); $$ = std::move($1); }
;
declaration:
    function_declaration { $$ = std::move($1); }
  | struct_declaration { $$ = std::move($1); }
;
function_declaration:
    "def" IDENTIFIER "(" parameters ")" "->" type block {
        auto node = std::make_unique<lab::ast::FunctionDecl>(@$);
        node->name = std::move($2); node->name_span = @2;
        node->parameters = std::move($4); node->return_type = std::move($7);
        node->body = std::move($8); $$ = std::move(node);
    }
;
parameters:
    %empty { $$ = std::vector<lab::ast::Parameter>(); }
  | parameter_list { $$ = std::move($1); }
;
parameter_list:
    parameter { $$.push_back(std::move($1)); }
  | parameter_list "," parameter { $1.push_back(std::move($3)); $$ = std::move($1); }
;
parameter:
    IDENTIFIER ":" type { $$ = {std::move($1), @1, std::move($3), @$}; }
;
struct_declaration:
    "struct" IDENTIFIER "{" fields "}" optional_semicolon {
        auto node = std::make_unique<lab::ast::StructDecl>(@$);
        node->name = std::move($2); node->name_span = @2;
        node->fields = std::move($4); $$ = std::move(node);
    }
;
optional_semicolon: %empty | ";" ;
fields:
    %empty { $$ = std::vector<lab::ast::Field>(); }
  | fields field { $1.push_back(std::move($2)); $$ = std::move($1); }
;
field:
    IDENTIFIER ":" type ";" { $$ = {std::move($1), @1, std::move($3), @$}; }
;
type:
    base_type { $$ = std::move($1); }
  | base_type "[" "]" { $$ = std::move($1); $$.array = true; $$.span = @$; }
;
base_type:
    cast_type { $$ = std::move($1); }
  | "void" { $$ = {"void", false, @$}; }
  | IDENTIFIER { $$ = {std::move($1), false, @$}; }
;
cast_type:
    "int" { $$ = {"int", false, @$}; }
  | "bool" { $$ = {"bool", false, @$}; }
  | "string" { $$ = {"string", false, @$}; }
;
block:
    "{" statements "}" {
        $$ = std::make_unique<lab::ast::BlockStmt>(@$);
        $$->statements = std::move($2);
    }
;
statements:
    %empty { $$ = lab::ast::Statements(); }
  | statements statement { $1.push_back(std::move($2)); $$ = std::move($1); }
;
statement:
    variable ";" { $1->span = @$; $$ = std::move($1); }
  | simple ";" { $1->span = @$; $$ = std::move($1); }
  | block { $$ = std::move($1); }
  | ";" { $$ = std::make_unique<lab::ast::EmptyStmt>(@$); }
  | "return" expression_optional ";" {
        auto node = std::make_unique<lab::ast::ReturnStmt>(@$);
        node->value = std::move($2); $$ = std::move(node);
    }
  | "if" "(" expression ")" block elif_branches else_body {
        auto node = std::make_unique<lab::ast::IfStmt>(@$);
        node->branches.push_back({std::move($3), std::move($5)});
        for (auto& branch : $6) node->branches.push_back(std::move(branch));
        node->else_body = std::move($7); $$ = std::move(node);
    }
  | "while" "(" expression ")" block {
        auto node = std::make_unique<lab::ast::WhileStmt>(@$);
        node->condition = std::move($3); node->body = std::move($5); $$ = std::move(node);
    }
  | "for" "(" initializer_optional ";" expression_optional ";" simple_optional ")" block {
        auto node = std::make_unique<lab::ast::ForStmt>(@$);
        node->initializer = std::move($3); node->condition = std::move($5);
        node->step = std::move($7); node->body = std::move($9); $$ = std::move(node);
    }
  | "for" "(" "var" IDENTIFIER ":" type "in" expression ")" block {
        auto node = std::make_unique<lab::ast::RangeForStmt>(@$);
        node->name = std::move($4); node->name_span = @4;
        node->type = std::move($6); node->iterable = std::move($8);
        node->body = std::move($10); $$ = std::move(node);
    }
;
elif_branches:
    %empty { $$ = std::vector<lab::ast::IfBranch>(); }
  | elif_branches "elif" "(" expression ")" block {
        $1.push_back({std::move($4), std::move($6)}); $$ = std::move($1);
    }
;
else_body:
    %empty { $$ = nullptr; }
  | "else" block { $$ = std::move($2); }
;
variable:
    "var" IDENTIFIER ":" type initializer {
        auto node = std::make_unique<lab::ast::VariableStmt>(@$);
        node->name = std::move($2); node->name_span = @2;
        node->type = std::move($4); node->initializer = std::move($5); $$ = std::move(node);
    }
;
initializer:
    %empty { $$ = nullptr; }
  | "=" expression { $$ = std::move($2); }
;
simple:
    expression {
        auto node = std::make_unique<lab::ast::ExpressionStmt>(@$);
        node->expression = std::move($1); $$ = std::move(node);
    }
  | expression "=" expression {
        auto node = std::make_unique<lab::ast::AssignmentStmt>(@$);
        node->target = std::move($1); node->value = std::move($3); $$ = std::move(node);
    }
;
initializer_optional:
    %empty { $$ = nullptr; }
  | variable { $$ = std::move($1); }
  | simple { $$ = std::move($1); }
;
simple_optional:
    %empty { $$ = nullptr; }
  | simple { $$ = std::move($1); }
;
expression_optional:
    %empty { $$ = nullptr; }
  | expression { $$ = std::move($1); }
;
expression: disjunction { $$ = std::move($1); } ;
disjunction:
    conjunction { $$ = std::move($1); }
  | disjunction "||" conjunction { $$ = binary(@$, "||", std::move($1), std::move($3)); }
;
conjunction:
    equality { $$ = std::move($1); }
  | conjunction "&&" equality { $$ = binary(@$, "&&", std::move($1), std::move($3)); }
;
equality:
    comparison { $$ = std::move($1); }
  | equality "==" comparison { $$ = binary(@$, "==", std::move($1), std::move($3)); }
  | equality "!=" comparison { $$ = binary(@$, "!=", std::move($1), std::move($3)); }
;
comparison:
    sum { $$ = std::move($1); }
  | comparison "<" sum { $$ = binary(@$, "<", std::move($1), std::move($3)); }
  | comparison "<=" sum { $$ = binary(@$, "<=", std::move($1), std::move($3)); }
  | comparison ">" sum { $$ = binary(@$, ">", std::move($1), std::move($3)); }
  | comparison ">=" sum { $$ = binary(@$, ">=", std::move($1), std::move($3)); }
;
sum:
    product { $$ = std::move($1); }
  | sum "+" product { $$ = binary(@$, "+", std::move($1), std::move($3)); }
  | sum "-" product { $$ = binary(@$, "-", std::move($1), std::move($3)); }
;
product:
    unary { $$ = std::move($1); }
  | product "*" unary { $$ = binary(@$, "*", std::move($1), std::move($3)); }
  | product "/" unary { $$ = binary(@$, "/", std::move($1), std::move($3)); }
  | product "%" unary { $$ = binary(@$, "%", std::move($1), std::move($3)); }
;
unary:
    postfix { $$ = std::move($1); }
  | "-" unary { $$ = unary(@$, "-", std::move($2)); }
  | "+" unary { $$ = unary(@$, "+", std::move($2)); }
  | "!" unary { $$ = unary(@$, "!", std::move($2)); }
;
postfix:
    primary { $$ = std::move($1); }
  | postfix "(" arguments ")" {
        auto node = std::make_unique<lab::ast::CallExpr>(@$);
        node->callee = std::move($1); node->arguments = std::move($3); $$ = std::move(node);
    }
  | postfix "[" expression "]" {
        auto node = std::make_unique<lab::ast::IndexExpr>(@$);
        node->base = std::move($1); node->index = std::move($3); $$ = std::move(node);
    }
  | postfix "." IDENTIFIER {
        auto node = std::make_unique<lab::ast::MemberExpr>(@$);
        node->base = std::move($1); node->member = std::move($3); $$ = std::move(node);
    }
;
primary:
    INTEGER {
        auto node = std::make_unique<lab::ast::IntegerExpr>(@$);
        node->magnitude = std::move($1); $$ = std::move(node);
    }
  | STRING_LITERAL {
        auto node = std::make_unique<lab::ast::StringExpr>(@$);
        node->value = std::move($1); $$ = std::move(node);
    }
  | "true" {
        auto node = std::make_unique<lab::ast::BooleanExpr>(@$);
        node->value = true; $$ = std::move(node);
    }
  | "false" { $$ = std::make_unique<lab::ast::BooleanExpr>(@$); }
  | IDENTIFIER {
        auto node = std::make_unique<lab::ast::NameExpr>(@$);
        node->name = std::move($1); $$ = std::move(node);
    }
  | "(" expression ")" { $$ = std::move($2); $$->span = @$; }
  | "[" arguments "]" {
        auto node = std::make_unique<lab::ast::ArrayExpr>(@$);
        node->elements = std::move($2); $$ = std::move(node);
    }
  | cast_type "(" expression ")" {
        auto node = std::make_unique<lab::ast::CastExpr>(@$);
        node->type = std::move($1); node->value = std::move($3); $$ = std::move(node);
    }
  | "new" base_type "[" expression "]" {
        auto node = std::make_unique<lab::ast::NewArrayExpr>(@$);
        node->element_type = std::move($2); node->length = std::move($4); $$ = std::move(node);
    }
  | parallel_map { $$ = std::move($1); }
;
arguments:
    %empty { $$ = lab::ast::Expressions(); }
  | argument_list { $$ = std::move($1); }
;
argument_list:
    expression { $$.push_back(std::move($1)); }
  | argument_list "," expression { $1.push_back(std::move($3)); $$ = std::move($1); }
;
parallel_map:
    "parallel" "map" "(" IDENTIFIER "," IDENTIFIER "in" expression ")"
    "{" statements "yield" expression ";" "}" {
        auto node = std::make_unique<lab::ast::ParallelMapExpr>(@$);
        node->index_name = std::move($4); node->index_span = @4;
        node->element_name = std::move($6); node->element_span = @6;
        node->input = std::move($8);
        node->body = std::make_unique<lab::ast::BlockStmt>(lab::SourceSpan{@10.begin, @15.end});
        node->body->statements = std::move($11);
        node->result = std::move($13);
        node->yield_span = {@12.begin, @14.end}; $$ = std::move(node);
    }
;
%%
void lab::frontend::Parser::error(const location_type& location, const std::string& message) {
    const std::string prefix = "syntax error, ";
    throw lab::DiagnosticError(location, "syntax error",
        message.compare(0, prefix.size(), prefix) == 0 ? message.substr(prefix.size()) : message);
}

