// interpreter.c - Minimal Lisp interpreter core
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include "gc.h"

typedef enum {
    VAL_NUMBER,
    VAL_NIL,
    VAL_PAIR,
    VAL_SYMBOL
} ValueType;

typedef struct Value {
    ValueType type;
    double number;
    char *symbol;
    struct Value *car;
    struct Value *cdr;
} Value;

static Value NIL_VALUE = {VAL_NIL, 0.0, NULL, NULL, NULL};
static Value *const NIL = &NIL_VALUE;

// Simple token types
enum {TOK_LPAREN, TOK_RPAREN, TOK_NUMBER, TOK_SYMBOL, TOK_QUOTE, TOK_EOF};

typedef struct {
    int type;
    char *text;
} Token;

// Forward declaration
static void runtime_error(const char *fmt, ...);

// Lexer state
static const char *input_ptr;
static Token cur_token;

static void skip_whitespace() {
    while (*input_ptr && (*input_ptr == ' ' || *input_ptr == '\t' || *input_ptr == '\n' || *input_ptr == '\r')) {
        input_ptr++;
    }
}

static Token next_token() {
    skip_whitespace();
    Token t;
    if (!*input_ptr) { t.type = TOK_EOF; t.text = NULL; return t; }
    char c = *input_ptr;
    if (c == '(') { t.type = TOK_LPAREN; t.text = "("; input_ptr++; }
    else if (c == ')') { t.type = TOK_RPAREN; t.text = ")"; input_ptr++; }
    else if (c == '\'') { t.type = TOK_QUOTE; t.text = "'"; input_ptr++; }
    else if ((c >= '0' && c <= '9') || c == '-') {
        const char *start = input_ptr;
        while ((*input_ptr >= '0' && *input_ptr <= '9') || *input_ptr == '.') input_ptr++;
        size_t len = input_ptr - start;
        char *num = (char*)gc_allocate(len + 1);
        memcpy(num, start, len);
        num[len] = '\0';
        t.type = TOK_NUMBER; t.text = num;
    } else {
        // Symbol (e.g., +, *, print)
        const char *start = input_ptr;
        while (*input_ptr && *input_ptr != ' ' && *input_ptr != '\t' && *input_ptr != '\n' && *input_ptr != '(' && *input_ptr != ')') input_ptr++;
        size_t len = input_ptr - start;
        char *sym = (char*)gc_allocate(len + 1);
        memcpy(sym, start, len);
        sym[len] = '\0';
        t.type = TOK_SYMBOL; t.text = sym;
    }
    return t;
}

static void consume(int expected) {
    if (cur_token.type != expected) {
        runtime_error("Unexpected token: %s", cur_token.text ? cur_token.text : "EOF");
        return;
    }
    cur_token = next_token();
}

// Forward declarations
static Value *eval_expr();
static Value *read_data_expr();
static void print_value(Value *value);

static jmp_buf eval_jmp_buf;
static int eval_jmp_active = 0;

static void runtime_error(const char *fmt, ...) {
    va_list args;
    fprintf(stderr, "Error: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    if (eval_jmp_active) {
        longjmp(eval_jmp_buf, 1);
    }
}

static Value *make_number(double num) {
    Value *v = (Value*)gc_allocate(sizeof(Value));
    v->type = VAL_NUMBER;
    v->number = num;
    v->symbol = NULL;
    v->car = NULL;
    v->cdr = NULL;
    return v;
}

static Value *make_symbol_copy(const char *text) {
    size_t len = strlen(text);
    char *copy = (char*)gc_allocate(len + 1);
    memcpy(copy, text, len + 1);
    Value *v = (Value*)gc_allocate(sizeof(Value));
    v->type = VAL_SYMBOL;
    v->number = 0.0;
    v->symbol = copy;
    v->car = NULL;
    v->cdr = NULL;
    return v;
}

static Value *make_pair(Value *car, Value *cdr) {
    Value *v = (Value*)gc_allocate(sizeof(Value));
    v->type = VAL_PAIR;
    v->car = car;
    v->cdr = cdr;
    v->symbol = NULL;
    v->number = 0.0;
    return v;
}

static int is_nil(Value *value) {
    return value == NULL || value->type == VAL_NIL;
}

static double as_number(Value *value, const char *context) {
    if (!value || value->type != VAL_NUMBER) {
        runtime_error("%s expects number arguments", context);
    }
    return value->number;
}

static Value *require_pair(Value *value, const char *context) {
    if (!value || value->type != VAL_PAIR) {
        runtime_error("%s expects list arguments", context);
    }
    return value;
}

static Value *build_list(Value **items, int count) {
    Value *head = NIL;
    Value **tail = &head;
    for (int i = 0; i < count; ++i) {
        Value *node = make_pair(items[i], NIL);
        *tail = node;
        tail = &node->cdr;
    }
    return head;
}

static Value *read_data_list(void);
static Value *read_data_expr(void) {
    if (cur_token.type == TOK_NUMBER) {
        double val = atof(cur_token.text);
        consume(TOK_NUMBER);
        return make_number(val);
    } else if (cur_token.type == TOK_SYMBOL) {
        char *sym = cur_token.text;
        consume(TOK_SYMBOL);
        if (strcmp(sym, "nil") == 0) return NIL;
        return make_symbol_copy(sym);
    } else if (cur_token.type == TOK_LPAREN) {
        consume(TOK_LPAREN);
        return read_data_list();
    } else if (cur_token.type == TOK_QUOTE) {
        consume(TOK_QUOTE);
        Value *inner = read_data_expr();
        Value *quote_sym = make_symbol_copy("quote");
        return make_pair(quote_sym, make_pair(inner, NIL));
    } else {
        runtime_error("Unexpected token while reading literal");
        return NIL;
    }
}

static Value *read_data_list(void) {
    Value *head = NIL;
    Value **tail = &head;
    while (cur_token.type != TOK_RPAREN && cur_token.type != TOK_EOF) {
        Value *element = read_data_expr();
        Value *node = make_pair(element, NIL);
        *tail = node;
        tail = &node->cdr;
    }
    consume(TOK_RPAREN);
    return head;
}

static Value *eval_list() {
    // Assume '(' already consumed
    Value *result = NIL;
    if (cur_token.type != TOK_SYMBOL) {
        runtime_error("List expression must start with a symbol");
        return NIL;
    }

    char *op = cur_token.text;
    consume(TOK_SYMBOL);

    if (strcmp(op, "quote") == 0) {
        Value *literal = read_data_expr();
        consume(TOK_RPAREN);
        return literal;
    }

    // Evaluate arguments until ')'
    Value *args[16];
    int argc = 0;
    while (cur_token.type != TOK_RPAREN && cur_token.type != TOK_EOF) {
        if (argc >= (int)(sizeof(args) / sizeof(args[0]))) {
            runtime_error("Too many arguments to operator %s", op);
        }
        args[argc++] = eval_expr();
    }

    // Simple arithmetic ops
    if (strcmp(op, "+") == 0) {
        double sum = 0;
        for (int i = 0; i < argc; i++) sum += as_number(args[i], op);
        result = make_number(sum);
    } else if (strcmp(op, "-") == 0) {
        if (argc == 0) {
            runtime_error("- expects at least one argument");
        }
        double subtotal = as_number(args[0], op);
        if (argc == 1) {
            subtotal = -subtotal;
        } else {
            for (int i = 1; i < argc; i++) subtotal -= as_number(args[i], op);
        }
        result = make_number(subtotal);
    } else if (strcmp(op, "*") == 0) {
        double product = 1;
        for (int i = 0; i < argc; i++) product *= as_number(args[i], op);
        result = make_number(product);
    } else if (strcmp(op, "/") == 0) {
        if (argc == 0) {
            runtime_error("/ expects at least one argument");
        }
        double quotient = as_number(args[0], op);
        for (int i = 1; i < argc; i++) quotient /= as_number(args[i], op);
        result = make_number(quotient);
    } else if (strcmp(op, "print") == 0) {
        for (int i = 0; i < argc; i++) {
            print_value(args[i]);
            if (i + 1 < argc) printf(" ");
        }
        printf("\n");
        result = NIL;
    } else if (strcmp(op, "cons") == 0) {
        if (argc != 2) {
            runtime_error("cons expects exactly two arguments");
        }
        result = make_pair(args[0], args[1]);
    } else if (strcmp(op, "car") == 0) {
        if (argc != 1) {
            runtime_error("car expects one argument");
        }
        Value *pair = require_pair(args[0], "car");
        result = pair->car;
    } else if (strcmp(op, "cdr") == 0) {
        if (argc != 1) {
            runtime_error("cdr expects one argument");
        }
        Value *pair = require_pair(args[0], "cdr");
        result = pair->cdr;
    } else if (strcmp(op, "list") == 0) {
        result = build_list(args, argc);
    } else {
        runtime_error("Unknown operator: %s", op);
    }

    consume(TOK_RPAREN);
    return result;
}

static Value *eval_expr() {
    if (cur_token.type == TOK_NUMBER) {
        double val = atof(cur_token.text);
        consume(TOK_NUMBER);
        return make_number(val);
    } else if (cur_token.type == TOK_LPAREN) {
        consume(TOK_LPAREN);
        return eval_list();
    } else if (cur_token.type == TOK_QUOTE) {
        consume(TOK_QUOTE);
        return read_data_expr();
    } else if (cur_token.type == TOK_SYMBOL) {
        if (strcmp(cur_token.text, "nil") == 0) {
            consume(TOK_SYMBOL);
            return NIL;
        }
        runtime_error("Unexpected symbol: %s", cur_token.text);
    } else {
        runtime_error("Unexpected token in expression");
    }
    return NIL;
}

static void print_pair(Value *value) {
    printf("(");
    while (1) {
        print_value(value->car);
        if (value->cdr && value->cdr->type == VAL_PAIR) {
            printf(" ");
            value = value->cdr;
            continue;
        } else if (is_nil(value->cdr)) {
            break;
        } else {
            printf(" . ");
            print_value(value->cdr);
            break;
        }
    }
    printf(")");
}

static void print_value(Value *value) {
    if (is_nil(value)) {
        printf("()");
    } else if (value->type == VAL_NUMBER) {
        printf("%g", value->number);
    } else if (value->type == VAL_SYMBOL) {
        printf("%s", value->symbol);
    } else if (value->type == VAL_PAIR) {
        print_pair(value);
    } else {
        printf("<unknown>");
    }
}

static Value *eval_source(const char *src, int *out_error) {
    Value *result = NIL;
    eval_jmp_active = 1;
    gc_init();
    if (setjmp(eval_jmp_buf) == 0) {
        input_ptr = src;
        cur_token = next_token();
        while (cur_token.type != TOK_EOF) {
            result = eval_expr();
        }
        if (out_error) *out_error = 0;
    } else {
        if (out_error) *out_error = 1;
    }
    eval_jmp_active = 0;
    gc_collect();
    return result;
}

// Exported entry point for WASM/CLI
// Returns result of evaluating the given Lisp source string.
// Caller must provide a null‑terminated C string.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE
#endif
double eval(const char *src) {
    int had_error = 0;
    Value *value = eval_source(src, &had_error);
    if (had_error || !value || value->type != VAL_NUMBER) {
        runtime_error("Expression did not evaluate to a number");
        return 0.0;
    }
    double result = value->number;
    return result;
}

static char *read_line(void) {
    size_t capacity = 128;
    size_t len = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return NULL;
    int ch;
    while ((ch = getchar()) != EOF) {
        if (ch == '\n') break;
        if (len + 1 >= capacity) {
            capacity *= 2;
            char *tmp = realloc(buffer, capacity);
            if (!tmp) {
                free(buffer);
                return NULL;
            }
            buffer = tmp;
        }
        buffer[len++] = (char)ch;
    }
    if (ch == EOF && len == 0) {
        free(buffer);
        return NULL;
    }
    buffer[len] = '\0';
    return buffer;
}

static void repl(void) {
    printf("Minimalisp REPL. Press Ctrl-D to exit.\n");
    while (1) {
        printf("ml> ");
        fflush(stdout);
        char *line = read_line();
        if (!line) {
            printf("\n");
            break;
        }
        if (line[0] == '\0') {
            free(line);
            continue;
        }
        int had_error = 0;
        Value *value = eval_source(line, &had_error);
        if (!had_error) {
            print_value(value);
            printf("\n");
        }
        free(line);
    }
}

#ifndef __EMSCRIPTEN__
int main(int argc, char **argv) {
    if (argc < 2) {
        repl();
        return 0;
    }
    int had_error = 0;
    Value *value = eval_source(argv[1], &had_error);
    if (had_error) {
        return 1;
    }
    printf("Result: ");
    print_value(value);
    printf("\n");
    return 0;
}
#endif
