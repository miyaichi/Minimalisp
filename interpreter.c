// interpreter.c - Minimal Lisp interpreter core
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gc.h"

// Simple token types
enum {TOK_LPAREN, TOK_RPAREN, TOK_NUMBER, TOK_SYMBOL, TOK_EOF};

typedef struct {
    int type;
    char *text;
} Token;

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
        fprintf(stderr, "Unexpected token: %s\n", cur_token.text ? cur_token.text : "EOF");
        exit(1);
    }
    cur_token = next_token();
}

// Forward declaration
static double eval_expr();

static double eval_list() {
    // Assume '(' already consumed
    double result = 0;
    if (cur_token.type == TOK_SYMBOL) {
        char *op = cur_token.text;
        consume(TOK_SYMBOL);
        // Evaluate arguments until ')'
        double args[16];
        int argc = 0;
        while (cur_token.type != TOK_RPAREN && cur_token.type != TOK_EOF) {
            if (argc >= (int)(sizeof(args) / sizeof(args[0]))) {
                fprintf(stderr, "Too many arguments to operator %s\n", op);
                exit(1);
            }
            args[argc++] = eval_expr();
        }
        // Simple arithmetic ops
        if (strcmp(op, "+") == 0) {
            result = 0; for (int i=0;i<argc;i++) result += args[i];
        } else if (strcmp(op, "-") == 0) {
            result = args[0]; for (int i=1;i<argc;i++) result -= args[i];
        } else if (strcmp(op, "*") == 0) {
            result = 1; for (int i=0;i<argc;i++) result *= args[i];
        } else if (strcmp(op, "/") == 0) {
            result = args[0]; for (int i=1;i<argc;i++) result /= args[i];
        } else if (strcmp(op, "print") == 0) {
            for (int i=0;i<argc;i++) printf("%g ", args[i]);
            printf("\n");
            result = 0;
        } else {
            fprintf(stderr, "Unknown operator: %s\n", op);
            exit(1);
        }
    }
    consume(TOK_RPAREN);
    return result;
}

static double eval_expr() {
    if (cur_token.type == TOK_NUMBER) {
        double val = atof(cur_token.text);
        consume(TOK_NUMBER);
        return val;
    } else if (cur_token.type == TOK_LPAREN) {
        consume(TOK_LPAREN);
        return eval_list();
    } else {
        fprintf(stderr, "Unexpected token in expression\n");
        exit(1);
    }
    return 0;
}

// Exported entry point for WASM/CLI
// Returns result of evaluating the given Lisp source string.
// Caller must provide a null‑terminated C string.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE
#endif
double eval(const char *src) {
    gc_init();
    input_ptr = src;
    cur_token = next_token();
    double result = 0;
    while (cur_token.type != TOK_EOF) {
        result = eval_expr();
    }
    gc_collect();
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
        double result = eval(line);
        printf("%g\n", result);
        free(line);
    }
}

#ifndef __EMSCRIPTEN__
int main(int argc, char **argv) {
    if (argc < 2) {
        repl();
        return 0;
    }
    double r = eval(argv[1]);
    printf("Result: %g\n", r);
    return 0;
}
#endif
