; Helix highlights for tree-sitter-ysh.
; Written for Helix, not nvim-treesitter: uses Helix capture names and no
; `#set! priority` (Helix rejects it and drops the whole query).
; Helix gives precedence to later patterns and to inner nodes, so generic
; captures come first and specific ones after.

(variable_name) @variable

(string) @string
[
  (heredoc_start)
  (heredoc_body)
] @string

[
  (escape_sequence)
  (escaped_bytes)
  (escape_special_characters)
  (escaped_newline)
  (escaped_double_quote)
  (escaped_single_quote)
] @constant.character.escape
(escaped_newline_value) @constant.character

(comment) @comment
(number) @constant.numeric
(boolean) @constant.builtin.boolean
(null) @constant.builtin
(type) @type

; Commands

(command_name) @function
(command_name (expansion) @function)
((command_name) @function.builtin
  (#any-of? @function.builtin
    "echo" "write" "type" "shopt" "json" "json8" "assert" "fork" "forkwait"
    "pp" "source" "cd" "pushd" "popd" "test" "read" "printf" "exit" "break"
    "continue" "error" "failed" "try" "redir" "boolstatus" "runproc" "invoke"
    "eval" "trap" "builtin" "command" "hay" "haynode" "ctx" "append"))
((command_name) @keyword.control.import
  (#any-of? @keyword.control.import "use" "source"))
"shvar" @function.builtin

redirection_value: (word) @string.special.path

; Definitions and calls

(proc_definition (proc_name) @function)
(function_definition (function_name (variable_name) @function))
(function_call (function_name (variable_name) @function))
(method_call method: (function_name (variable_name) @function.method))

parameter: (variable_name) @variable.parameter
(rest_of_arguments (variable_name) @variable.parameter)
(spread "..." @operator)
(parameter_list (named_parameter (variable_name) @variable.parameter))
(proc_parameter_list (named_parameter (variable_name) @variable.parameter))
(named_parameter (variable_name) @variable.parameter)

; Variables

constant: (variable_name) @constant
member: (variable_name) @variable.other.member
key: (variable_name) @variable.other.member
(environment (variable_name) @variable)
(expansion (variable_name) @variable)
((variable_name) @variable.builtin
  (#any-of? @variable.builtin "_error" "_status" "_this_dir" "ENV" "ARGV"))

; Keywords

[
  "var"
  "setvar"
  "const"
  "setglobal"
  "call"
] @keyword.storage

[
  "func"
  "proc"
  "typed"
] @keyword.function

"return" @keyword.control.return

[
  "for"
  "in"
  "while"
  "until"
  "do"
  "done"
] @keyword.control.repeat

[
  "if"
  "elif"
  "else"
  "case"
  "then"
  "fi"
  "esac"
] @keyword.control.conditional

[
  "or"
  "and"
  "not"
  "is"
] @keyword.operator

(expression_mode "=" @keyword.control)

; Operators and punctuation

[
  "!"
  "+"
  "-"
  "*"
  "/"
  "//"
  "**"
  "%"
  "~"
  "<"
  "<="
  ">"
  ">="
  "==="
  "!=="
  "~=="
  "!~"
  "~~"
  "!~~"
  "++"
  "<<"
  ">>"
  "&"
  "|"
  "^"
  "="
  "+="
  "-="
  "*="
  "/="
  "//="
  "%="
  "**="
  "<<="
  ">>="
  "&="
  "|="
  "^="
  "=>"
  "->"
  "."
  ":"
  "..."
  "&&"
  "||"
  ";;"
  ";&"
  ";;&"
  ":-"
  "<>"
  ">&"
  ">|"
  ">>&"
  "&>>"
  "<<<"
  "<("
  (range_operator)
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

(literal_list [ ":|" "%(" "|" ")" ] @punctuation.bracket)

"," @punctuation.delimiter

[
  "$"
  ";"
  "@"
] @punctuation.special
