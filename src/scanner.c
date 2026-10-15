#include "tree_sitter/parser.h"
#include <stdlib.h>
#include <string.h>

enum TokenType {
  DOLLAR_EXPANSION,
  HAT_EXPANSION,
  ENV_VAR_NAME,
  ENV_EQUAL,
  CONST_DECL_VAR,
  COMMA,
  SEMICOLON,
  CLOSE_PAREN,
  CLOSE_BRACE,
  CLOSE_BRACKET,
  CLOSING_LIST,
  NAMED_PARAM_EQ,
  BYTE_STRING_MARKER,
  NEWLINE,
  TERMINATOR_SENTINEL,
  STATEMENT_SENTINEL,
  MULTILINE_CMD_SENTINEL,
  COMMA_SENTINEL,
  ERROR_SENTINEL,
  HEREDOC_START,
  HEREDOC_BODY,
};

#define MAX_HEREDOCS 8
#define MAX_DELIMITER 64

// Here docs opened on the current line, waiting for their body on the next one.
typedef struct {
  uint8_t len;
  bool strip_tabs;
  char delimiter[MAX_DELIMITER];
} Heredoc;

typedef struct {
  uint8_t count;
  Heredoc pending[MAX_HEREDOCS];
} Scanner;

static inline bool is_alpha_(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool is_num_(int c) { return (c >= '0' && c <= '9'); }

static inline bool is_alnum_(int c) { return is_alpha_(c) || is_num_(c); }

void *tree_sitter_ysh_external_scanner_create() {
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_ysh_external_scanner_destroy(void *p) { free(p); }

void tree_sitter_ysh_external_scanner_reset(void *p) {
  ((Scanner *)p)->count = 0;
}

unsigned tree_sitter_ysh_external_scanner_serialize(void *p, char *buffer) {
  Scanner *scanner = p;
  unsigned size = 0;
  buffer[size++] = (char)scanner->count;
  for (uint8_t i = 0; i < scanner->count; i++) {
    Heredoc *h = &scanner->pending[i];
    buffer[size++] = (char)h->strip_tabs;
    buffer[size++] = (char)h->len;
    memcpy(&buffer[size], h->delimiter, h->len);
    size += h->len;
  }
  return size;
}

void tree_sitter_ysh_external_scanner_deserialize(void *p, const char *b,
                                                  unsigned n) {
  Scanner *scanner = p;
  scanner->count = 0;
  if (n == 0) {
    return;
  }
  unsigned size = 0;
  scanner->count = (uint8_t)b[size++];
  for (uint8_t i = 0; i < scanner->count; i++) {
    Heredoc *h = &scanner->pending[i];
    h->strip_tabs = b[size++];
    h->len = (uint8_t)b[size++];
    memcpy(h->delimiter, &b[size], h->len);
    size += h->len;
  }
}

static bool scan_heredoc_start(Scanner *scanner, TSLexer *lexer) {
  if (scanner->count >= MAX_HEREDOCS) {
    return false;
  }
  Heredoc *h = &scanner->pending[scanner->count];
  h->len = 0;
  h->strip_tabs = false;
  if (lexer->lookahead == '-') {
    h->strip_tabs = true;
    lexer->advance(lexer, false);
  }
  int32_t quote = 0;
  if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
    quote = lexer->lookahead;
    lexer->advance(lexer, false);
  } else if (lexer->lookahead == '\\') {
    lexer->advance(lexer, false);
  }
  while (lexer->lookahead != 0 && h->len < MAX_DELIMITER) {
    int32_t c = lexer->lookahead;
    if (quote ? c == quote || c == '\n'
              : !(is_alnum_(c) || c == '-' || c == '.' || c == '/')) {
      break;
    }
    h->delimiter[h->len++] = (char)c;
    lexer->advance(lexer, false);
  }
  if (quote) {
    if (lexer->lookahead != quote) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  if (h->len == 0) {
    return false;
  }
  scanner->count++;
  lexer->result_symbol = HEREDOC_START;
  return true;
}

// Consume the lines of the oldest pending here doc, up to and including the
// line holding its delimiter.
static bool scan_heredoc_body(Scanner *scanner, TSLexer *lexer) {
  Heredoc h = scanner->pending[0];
  scanner->count--;
  memmove(&scanner->pending[0], &scanner->pending[1],
          scanner->count * sizeof(Heredoc));
  for (;;) {
    if (h.strip_tabs) {
      while (lexer->lookahead == '\t') {
        lexer->advance(lexer, false);
      }
    }
    uint8_t matched = 0;
    while (matched < h.len && lexer->lookahead == h.delimiter[matched]) {
      lexer->advance(lexer, false);
      matched++;
    }
    if (matched == h.len &&
        (lexer->lookahead == '\n' || lexer->lookahead == 0)) {
      if (lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
      }
      break;
    }
    while (lexer->lookahead != '\n' && lexer->lookahead != 0) {
      lexer->advance(lexer, false);
    }
    if (lexer->lookahead == 0) {
      break;
    }
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);
  lexer->result_symbol = HEREDOC_BODY;
  return true;
}

bool tree_sitter_ysh_external_scanner_scan(void *payload, TSLexer *lexer,
                                           const bool *valid_symbols) {
  Scanner *scanner = payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }

  // A here doc body starts on the line after its << operator
  if (valid_symbols[HEREDOC_BODY] && scanner->count > 0 &&
      lexer->get_column(lexer) == 0) {
    return scan_heredoc_body(scanner, lexer);
  }

  // Skip whitespaces
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
    lexer->advance(lexer, true);
  }

  if ((valid_symbols[CLOSE_PAREN] || valid_symbols[CLOSE_BRACE] ||
       valid_symbols[CLOSE_BRACKET] || valid_symbols[COMMA] ||
       valid_symbols[SEMICOLON] || valid_symbols[CLOSING_LIST] ||
       valid_symbols[NAMED_PARAM_EQ] || valid_symbols[STATEMENT_SENTINEL] ||
       valid_symbols[MULTILINE_CMD_SENTINEL] ||
       valid_symbols[COMMA_SENTINEL]) &&
      !valid_symbols[TERMINATOR_SENTINEL] && lexer->lookahead == '\n') {
    lexer->advance(lexer, false);
    if (valid_symbols[MULTILINE_CMD_SENTINEL]) {
      while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
      }
      // Two consecutive newlines are not allowed in multiline commands
      if (lexer->lookahead == '\n') {
        return false;
      }
    }
    lexer->result_symbol = NEWLINE;
    return true;
  }

  if (valid_symbols[HEREDOC_START]) {
    return scan_heredoc_start(scanner, lexer);
  }

  if (valid_symbols[COMMA] && lexer->lookahead == ',') {
    lexer->advance(lexer, false);
    lexer->result_symbol = COMMA;
    return true;
  }

  if (valid_symbols[SEMICOLON] && lexer->lookahead == ';') {
    lexer->advance(lexer, false);
    lexer->result_symbol = SEMICOLON;
    return true;
  }

  if (valid_symbols[CLOSE_PAREN] && lexer->lookahead == ')') {
    lexer->advance(lexer, false);
    lexer->result_symbol = CLOSE_PAREN;
    return true;
  }

  if (valid_symbols[CLOSE_BRACE] && lexer->lookahead == '}') {
    lexer->advance(lexer, false);
    lexer->result_symbol = CLOSE_BRACE;
    return true;
  }

  if (valid_symbols[CLOSE_BRACKET] && lexer->lookahead == ']') {
    lexer->advance(lexer, false);
    lexer->result_symbol = CLOSE_BRACKET;
    return true;
  }

  if (valid_symbols[CLOSING_LIST] && lexer->lookahead == '|') {
    lexer->advance(lexer, false);
    lexer->result_symbol = CLOSING_LIST;
    return true;
  }

  if (valid_symbols[NAMED_PARAM_EQ] && lexer->lookahead == '=') {
    lexer->advance(lexer, false);
    // == and => are operators, not named parameters
    if (lexer->lookahead != '=' && lexer->lookahead != '>') {
      lexer->result_symbol = NAMED_PARAM_EQ;
      return true;
    }
    return false;
  }

  if (valid_symbols[DOLLAR_EXPANSION] && lexer->lookahead == '$') {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    // $var, $@
    if (is_alpha_(lexer->lookahead)) {
      lexer->result_symbol = DOLLAR_EXPANSION;
      return true;
    }
    // $1, $*, $?, $#
    if (is_num_(lexer->lookahead)) {
      lexer->result_symbol = DOLLAR_EXPANSION;
      return true;
    } else if (lexer->lookahead == '*' || lexer->lookahead == '?' ||
               lexer->lookahead == '#' || lexer->lookahead == '@' ||
               lexer->lookahead == '$' || lexer->lookahead == '!') {
      lexer->advance(lexer, false);
      if (!is_alnum_(lexer->lookahead)) {
        lexer->result_symbol = DOLLAR_EXPANSION;
        return true;
      }
    }
    // ${...} $[...], $(...)
    if (lexer->lookahead == '{' || lexer->lookahead == '[' ||
        lexer->lookahead == '(') {
      lexer->result_symbol = DOLLAR_EXPANSION;
      return true;
    }
    return false;
  }

  if (valid_symbols[HAT_EXPANSION] && lexer->lookahead == '@') {
    lexer->advance(lexer, false);
    if (is_alnum_(lexer->lookahead) || lexer->lookahead == '[' ||
        lexer->lookahead == '(') {
      lexer->result_symbol = HAT_EXPANSION;
      return true;
    }
    return false;
  }

  if (valid_symbols[BYTE_STRING_MARKER] && lexer->lookahead == 'b') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '\'') {
      lexer->result_symbol = BYTE_STRING_MARKER;
      return true;
    }
    return false;
  }

  if (valid_symbols[ENV_EQUAL] && lexer->lookahead == '=') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != ' ' && lexer->lookahead != '\t') {
      lexer->result_symbol = ENV_EQUAL;
      return true;
    }
    return false;
  }

  if (valid_symbols[ENV_VAR_NAME] || valid_symbols[CONST_DECL_VAR]) {
    unsigned len = 0;
    while (is_alpha_(lexer->lookahead) ||
           (len > 0 && is_num_(lexer->lookahead))) {
      lexer->advance(lexer, false);
      len++;
    }
    if (len > 0) {
      if (valid_symbols[ENV_VAR_NAME] && lexer->lookahead == '=') {
        lexer->result_symbol = ENV_VAR_NAME;
        return true;
      } else if (valid_symbols[CONST_DECL_VAR] &&
                 (lexer->lookahead == ' ' || lexer->lookahead == '\t')) {
        lexer->mark_end(lexer);
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
          lexer->advance(lexer, false);
        }
        if (lexer->lookahead == '=') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->result_symbol = CONST_DECL_VAR;
            return true;
          }
        }
      }
    }
  }

  return false;
}
