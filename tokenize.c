#include "angcc.h"

// Input filename
static char *current_filename;

// Input string
static char *current_input;


// Reports an error and exit.
void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

// Reports an error message in the following fxormat and exit.
//
// foo.c:10: x = y + 1;
//               ^ <error message here>
static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
  // #1 find a line containing `loc`
  char *line = loc;
  while (current_input < line && line[-1] != '\n')
    // from line end to begin, scan
    line--;

  // line is at the beginning of the line

  char *end = loc;
  while (*end != '\n')
    end++;

  // #2 Get a line number
  /*
  int line_no = 1;
  for (char *p = current_input; p < line; p++) {
    if (*p == '\n') {
      line_no++;
    }
  }
  */

  // #3 Print out the line
  int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
  fprintf(stderr, "%.*s\n", (int)(end - line), line); // from end-lne to line

  // #4 Show the error message
  int pos = loc - line + indent;   // prefix
  fprintf(stderr, "%*s", pos, ""); // print pos spaces.
  fprintf(stderr, "^ ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

void error_at(char *loc, char *fmt, ...) {
  // error happends at specifc location, using error_at
  int line_no = 1;
  // the current_input indicates the start of current input file
  for (char *p = current_input; p < loc; p++) {
    if (*p == '\n')
      line_no++;
  }

  va_list ap;
  va_start(ap, fmt);
  verror_at(line_no, loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
  // error happends in specific token, using error_tok
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->line_no, tok->loc, fmt, ap);
}

// Consumes the current token if it matches `op`.
bool equal(Token *tok, char *op) {
  return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

// Ensure that the current token is `op`.
Token *skip(Token *tok, char *op) {
  if (!equal(tok, op))
    error_tok(tok, "expected '%s'", op);
  return tok->next;
}

bool consume(Token **rest, Token *tok, char *str) {
  if (equal(tok, str)) {
    *rest = tok->next;
    return true;
  }
  *rest = tok;
  return false;
}

// Create a new token. (in heap)
static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = calloc(1, sizeof(Token));
  tok->kind = kind;
  tok->loc = start;
  tok->len = end - start;
  return tok;
}

static bool startswith(char *p, char *q) {
  return strncmp(p, q, strlen(q)) == 0;
}

// Returns true if c is valid as the first character of an identifier.
static bool is_ident1(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

// Returns true if c is valid as a non-first character of an identifier.
static bool is_ident2(char c) {
  return is_ident1(c) || ('0' <= c && c <= '9');
}

static int from_hex(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}


// Read a punctuator token from p and returns its length.
// punctuator-> any character(s) which is not a space or an alphanumric charactor
static int read_punct(char *p) {
  static char *kw[] = {"==", "!=", "<=", ">=", "->"};

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++) {
    if (startswith(p, kw[i])) {
      return strlen(kw[i]);  // 2
    }
  }

  return ispunct(*p) ? 1 : 0; // single
}

static bool is_keyword(Token* tok) {
  static char *kw[] = {
    "return", "if", "else", "for", "while", "int", "sizeof", "char",
    "struct", "union", "long", "short",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++) {
    // "return", "if" ... all these keywords
    // were stored in the TEXT segment
    // and each of them was a 8-byte address
    if (equal(tok, kw[i]))
      return true;
  }

  return false;
}

static int read_escaped_char(char **new_pos, char *p) {
  // Escape sequences are defined using themseleves here E.g.
  // '\n' is implemented using '\n'. This tautological definition
  // works because the compiler that compiles our compiler knows
  // what '\n' actually is. In other words, we "inherit" the ASCII
  // code of '\n' from the compiler that compiles our compiler,
  // so we don't have to teach the actual code here. (Rui)
  //
  // This fact has huge implications not only for the correctness
  // of the compiler but also for the security of the generated code.
  //
  // for more info, read "Reflections on Trusting Trust" by Ken Thompson.
  // https://angold4.org/cs/uc/thompson1984.pdf
  // and this article: https://angold4.org/cs/docs/weicc/1Thompson1984.html

  if ('0' <= *p && *p <= '7') {
    // Read an octal number (3 digits?)
    int c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = (c << 3) + (*p++ - '0');
      if ('0' <= *p && *p <= '7') {
	c = (c << 3) + (*p++ - '0');
      }
    }
    *new_pos = p;
    return c;
  }

  if (*p == 'x') {
    // Read a hexadecimal number.
    p++;
    if (!isxdigit(*p)) {
      // checks for hexadecimal digits
      error_at(p, "invalid hex escape sequence");
    }

    int c = 0;
    for (; isxdigit(*p); p++) {
      c = (c << 4) + from_hex(*p);
    }
    *new_pos = p;
    return c;
  }

  *new_pos = p + 1; // (p + 2)

  switch(*p)  {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    case 'e': return 27;
    default: return *p;
  }
}


static char *string_literal_end(char *p) {
  char *start = p; // used for indicatiing the error
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0') {
      error_at(start, "unclosed string literal");
    }

    if (*p == '\\')
      p++;

  }
  return p;
}

static Token *read_string_literal(char *start) {
  char *end = string_literal_end(start+1);
  char *buf = calloc(1, end - start);
  int len = 0;

  for (char *p = start + 1; p < end;) {
    if (*p == '\\') {
      buf[len++] = read_escaped_char(&p, p+1);
    } else {
      buf[len++] = *p++;
    }
  }

  Token *tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty_char, len + 1);
  tok->str = buf;
  return tok;
}

static void convert_keywords(Token* tok) {
  // scan all tokens
  for (Token *t = tok; t->kind != TK_EOF; t = t->next) {
    if (is_keyword(t))
      t->kind = TK_KEYWORD;
  }
}

// Initialize line info for all tokens
static void add_line_numbers(Token *tok) {
  char *p = current_input;
  int n = 1;

  do {
    if (p == tok->loc) {
      tok->line_no = n;
      tok = tok->next;
    }
    if (*p == '\n') {
      n++;
    }
  } while (*p++);
}

// Input stream ->
// head -> tok1 -> tok2 -> ... -> tokEOF

// Tokenize `current_input` and returns new tokens.
static Token *tokenize(char *filename, char *p) {
  current_filename = filename;
  current_input = p;
  Token head = {};
  Token *cur = &head;

  while (*p) {
    // Skip the line comments
    if (startswith(p, "//")) {
      p += 2;
      while (*p != '\n') {
	p++;
      }
      continue;
    }

    // Skip block comments
    if (startswith(p, "/*")) {
      char *q = strstr(p + 2, "*/");
      // The strstr() function finds the first occurrence of the substring 
      // needle in the string haystack
      if (!q) {
	error_at(p, "unclosed block comment");
      }

      p = q + 2;
      continue;
   }

    if (isspace(*p)) {
      // checks for white-space characters.  
      // In the "C" and "POSIX" locales, these are: 
      // space, form-feed ('\f'), newline ('\n'), carriage return ('\r'),
      // horizontal tab ('\t'), and vertical tab ('\v').      
      p++;
      continue;
    }

    // Numeric literal
    if (isdigit(*p)) {
      // <---
      cur = cur->next = new_token(TK_NUM, p, p);
      char *q = p;
      // if endptr is not null (&p)
      // strtoul store the addr of the first invalid charactor in endptr
      // which will update the &p
      cur->val = strtoul(p, &p, 10); // str -> u long
      cur->len = p - q;
      continue;
    }

    // String literal
    if (*p == '"') {
      cur = cur->next = read_string_literal(p);
      p += cur->len;
      continue;
    }

    // Identifier or keyword
    if (is_ident1(*p)) {
      char *start = p;
      do {
	p++;
      } while (is_ident2(*p));
      cur = cur->next = new_token(TK_IDENT, start, p);
      continue;
    }

    // Punctuators
    int punct_len = read_punct(p);
    if (punct_len) {
      cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
      p += cur->len;
      continue;
    }

    error_at(p, "invalid token");
  }

  cur = cur->next = new_token(TK_EOF, p, p);
  add_line_numbers(head.next);
  convert_keywords(head.next);
  return head.next;
}

// Returns the contents of a given file.
static char *read_file(char *path) {
  FILE *fp;

  if (strcmp(path, "-") == 0) {
    // By convention, read from stdin if a given filename is "-"
    fp = stdin;
  } else {
    fp = fopen(path, "r");
    if (!fp) 
      error("cannot open %s: %s", path, strerror(errno));
  }

  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);
  
  // Read the entire file
  for (;;) {
    char buf2[4096];
    int n = fread(buf2, 1, sizeof(buf2), fp);
    if (n == 0) break; // read 0 items

    fwrite(buf2, 1, n, out);
  }

  if (fp != stdin) 
    fclose(fp);

  // Make sure that the last line is properly terminated with '\n'
  fflush(out);
  // fflush forces a write of all user-space buffered data for the given output

  if (buflen == 0 || buf[buflen-1] != '\n')
    fputc('\n', out);
  fputc('\0', out);
  fclose(out);

  return buf; // return the buffer pointer
}

Token *tokenize_file(char *path) {
  return tokenize(path, read_file(path));
}
