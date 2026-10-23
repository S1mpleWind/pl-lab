# 表层语法 EBNF

与 `src/frontend/parser.y` 对照阅读。引号内是终结符，`{ X }` 表示重复零次以上，`[ X ]` 表示可选，`|` 表示候选；这些元符号不属于源语言。identifier、integer、float_literal 和 string_literal 由扫描器产生。

```text
program       = { declaration } ;
declaration   = function | structure ;
function      = "def" identifier "(" [ parameters ] ")" "->" type ( block | ";" ) ;
parameters    = parameter { "," parameter } ;
parameter     = identifier ":" type ;
structure     = "struct" identifier "{" { field } "}" [ ";" ] ;
field         = identifier ":" type ";" ;
type          = base_type [ "[" "]" ] ;
base_type     = cast_type | "void" | identifier ;
cast_type     = "int" | "float" | "bool" | "string" ;

block         = "{" { statement } "}" ;
variable      = "var" identifier ":" type [ "=" expression ] ;
simple        = expression [ "=" expression ] ;
statement     = block | ";" | variable ";" | simple ";"
              | "return" [ expression ] ";"
              | "if" "(" expression ")" block
                { "elif" "(" expression ")" block } [ "else" block ]
              | "while" "(" expression ")" block
              | "for" "(" [ variable | simple ] ";" [ expression ] ";" [ simple ] ")" block
              | "for" "(" "var" identifier ":" type "in" expression ")" block ;

expression    = disjunction ;
disjunction   = conjunction { "||" conjunction } ;
conjunction   = equality { "&&" equality } ;
equality      = comparison { ( "==" | "!=" ) comparison } ;
comparison    = sum { ( "<" | "<=" | ">" | ">=" ) sum } ;
sum           = product { ( "+" | "-" ) product } ;
product       = unary { ( "*" | "/" | "%" ) unary } ;
unary         = ( "+" | "-" | "!" ) unary | postfix ;
postfix       = primary { "(" [ arguments ] ")" | "[" expression "]" | "." identifier } ;
arguments     = expression { "," expression } ;
primary       = integer | float_literal | string_literal | "true" | "false" | identifier
              | "(" expression ")" | "[" [ arguments ] "]"
              | cast_type "(" expression ")"
              | "new" base_type "[" expression "]" | parallel_map ;
parallel_map  = "parallel" "map" "(" identifier "," identifier "in" expression ")"
                "{" { statement } "yield" expression ";" "}" ;
```

二元运算的重复项按左结合构造 AST；一元运算递归产生式体现右结合。`a = b = 1;` 不合法，因为赋值不属于 expression。

## 文法和语义的边界

语法允许空文件、void[]、任意表达式形式的赋值目标和嵌套 map；这不代表完整编译接受它们。sema 负责入口、合法类型位置、可写目标、原型与定义匹配、嵌套并行限制及其他静态规则。yield 只允许出现在 map 的末尾，不是一般语句。

`f(...)` 与结构体构造在语法阶段都是 Call；`range`、`len` 和其他内置接口都是普通标识符。lexer 不需要知道函数或结构体的符号表。文法发生改动时应同步本页，并保留 Bison 的零冲突构建约束。
