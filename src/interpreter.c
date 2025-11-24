// interpreter.c - Minimal Lisp interpreter with list primitives, quoting, and simple runtime
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <stdarg.h>
#include <setjmp.h>
#include <ctype.h>
#include "gc.h"

#define MAX_ARGS 64

typedef struct Value Value;
typedef struct Env Env;
typedef struct Binding Binding;
typedef Value *(*BuiltinFunc)(Value **args, int argc, Env *env);

typedef enum {
    VAL_NUMBER,
    VAL_NIL,
    VAL_PAIR,
    VAL_SYMBOL,
    VAL_BUILTIN,
    VAL_LAMBDA
} ValueType;

struct Value {
    ValueType type;
    double number;
    char *symbol;
    Value *car;
    Value *cdr;
    BuiltinFunc builtin;
    Env *env;
    Value *params;
    Value *body;
};

struct Binding {
    const char *name;
    Value *value;
    Binding *next;
};

struct Env {
    Env *parent;
    Binding *bindings;
};

static Value NIL_VALUE = {VAL_NIL, 0.0, NULL, NULL, NULL, NULL, NULL, NULL};
static Value TRUE_VALUE = {VAL_SYMBOL, 0.0, "t", NULL, NULL, NULL, NULL, NULL};
static Value *const NIL = &NIL_VALUE;
static Value *const TRUE = &TRUE_VALUE;

// Simple token types
enum {TOK_LPAREN, TOK_RPAREN, TOK_NUMBER, TOK_SYMBOL, TOK_QUOTE, TOK_EOF};

typedef struct {
    int type;
    char *text;
} Token;

static const char *input_ptr;
static Token cur_token;
static jmp_buf eval_jmp_buf;
static int eval_jmp_active = 0;

static Env *global_env = NULL;
static int runtime_initialized = 0;

#define MAX_TEMP_ROOTS 65536
static Value *temp_roots[MAX_TEMP_ROOTS];
static int temp_root_sp = 0;

static void push_root(Value *v) {
    if (temp_root_sp >= MAX_TEMP_ROOTS) {
        fprintf(stderr, "Error: Stack overflow (temp roots)\n");
        exit(1);
    }
    temp_roots[temp_root_sp++] = v;
}

static void pop_root(void) {
    if (temp_root_sp > 0) temp_root_sp--;
}

static void runtime_error(const char *fmt, ...);
static void print_value(Value *value);
static void print_pair(Value *value);
static void trace_value(void *obj);
static void trace_env(void *obj);
static void trace_binding(void *obj);
static char *gc_alloc_buffer(size_t size);
static char *gc_copy_cstring(const char *text);
static char *read_file(const char *path);
static char *read_file_internal(const char *path, int warn_on_error);
static void load_standard_library(void);

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void skip_whitespace(void) {
    while (*input_ptr) {
        if (*input_ptr == ' ' || *input_ptr == '\t' || *input_ptr == '\n' || *input_ptr == '\r') {
            input_ptr++;
            continue;
        }
        if (*input_ptr == ';') {
            while (*input_ptr && *input_ptr != '\n' && *input_ptr != '\r') {
                input_ptr++;
            }
            continue;
        }
        break;
    }
}

static Token next_token(void) {
    skip_whitespace();
    Token t;
    if (!*input_ptr) { t.type = TOK_EOF; t.text = NULL; return t; }
    char c = *input_ptr;
    if (c == '(') { t.type = TOK_LPAREN; t.text = "("; input_ptr++; return t; }
    if (c == ')') { t.type = TOK_RPAREN; t.text = ")"; input_ptr++; return t; }
    if (c == '\'') { t.type = TOK_QUOTE; t.text = "'"; input_ptr++; return t; }
    if (is_digit(c) || (c == '-' && is_digit(*(input_ptr + 1)))) {
        const char *start = input_ptr;
        input_ptr++;
        while (is_digit(*input_ptr) || *input_ptr == '.') input_ptr++;
        size_t len = input_ptr - start;
        char *num = gc_alloc_buffer(len + 1);
        memcpy(num, start, len);
        num[len] = '\0';
        t.type = TOK_NUMBER; t.text = num;
        return t;
    }
    const char *start = input_ptr;
    while (*input_ptr && *input_ptr != ' ' && *input_ptr != '\t' && *input_ptr != '\n' && *input_ptr != '(' && *input_ptr != ')' && *input_ptr != '\'') input_ptr++;
    size_t len = input_ptr - start;
    char *sym = gc_alloc_buffer(len + 1);
    memcpy(sym, start, len);
    sym[len] = '\0';
    t.type = TOK_SYMBOL; t.text = sym;
    return t;
}

static void consume(int expected) {
    if (cur_token.type != expected) {
        runtime_error("Unexpected token: %s", cur_token.text ? cur_token.text : "EOF");
        return;
    }
    cur_token = next_token();
}

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

static char *gc_alloc_buffer(size_t size) {
    char *buf = (char*)gc_allocate(size);
    gc_set_trace(buf, NULL);
    gc_set_tag(buf, GC_TAG_STRING);
    return buf;
}

static char *gc_copy_cstring(const char *text) {
    size_t len = strlen(text);
    char *copy = gc_alloc_buffer(len + 1);
    memcpy(copy, text, len + 1);
    return copy;
}

static Value *alloc_value(ValueType type) {
    Value *v = (Value*)gc_allocate(sizeof(Value));
    gc_set_trace(v, trace_value);
    v->type = type;
    v->number = 0.0;
    v->symbol = NULL;
    v->car = NULL;
    v->cdr = NULL;
    v->builtin = NULL;
    v->env = NULL;
    v->params = NULL;
    v->body = NULL;
    return v;
}

static Value *make_number(double num) {
    Value *v = alloc_value(VAL_NUMBER);
    gc_set_tag(v, GC_TAG_VALUE_NUMBER);
    v->number = num;
    return v;
}

static Value *make_symbol_copy(const char *text) {
    Value *v = alloc_value(VAL_SYMBOL);
    gc_set_tag(v, GC_TAG_VALUE_SYMBOL);
    v->symbol = gc_copy_cstring(text);
    return v;
}

static Value *make_pair(Value *car, Value *cdr) {
    Value *v = alloc_value(VAL_PAIR);
    gc_set_tag(v, GC_TAG_VALUE_PAIR);
    v->car = car;
    v->cdr = cdr;
    return v;
}

static Value *make_builtin(BuiltinFunc fn) {
    Value *v = alloc_value(VAL_BUILTIN);
    gc_set_tag(v, GC_TAG_VALUE_BUILTIN);
    v->builtin = fn;
    return v;
}

static Value *make_lambda(Value *params, Value *body, Env *env) {
    Value *v = alloc_value(VAL_LAMBDA);
    gc_set_tag(v, GC_TAG_VALUE_LAMBDA);
    v->params = params;
    v->body = body;
    v->env = env;
    return v;
}

static int is_nil(Value *value) {
    return value == NULL || value->type == VAL_NIL;
}

static int is_truthy(Value *value) {
    return !is_nil(value);
}

static Env *env_new(Env *parent) {
    Env *env = (Env*)gc_allocate(sizeof(Env));
    gc_set_trace(env, trace_env);
    gc_set_tag(env, GC_TAG_ENV);
    env->parent = parent;
    env->bindings = NULL;
    return env;
}

static void binding_set_value(Binding *binding, Value *value) {
    gc_write_barrier(binding, (void**)&binding->value, value);
    binding->value = value;
}

static void binding_set_next(Binding *binding, Binding *next) {
    gc_write_barrier(binding, (void**)&binding->next, next);
    binding->next = next;
}

static void env_set_bindings(Env *env, Binding *bindings) {
    gc_write_barrier(env, (void**)&env->bindings, bindings);
    env->bindings = bindings;
}

static void append_to_buffer(char *buffer, size_t size, const char *text) {
    size_t len = strlen(buffer);
    if (len >= size - 1) return;
    strncat(buffer, text, size - len - 1);
}

static void append_value_to_buffer(char *buffer, size_t size, Value *value);

static void append_pair_to_buffer(char *buffer, size_t size, Value *value) {
    append_to_buffer(buffer, size, "(");
    while (1) {
        append_value_to_buffer(buffer, size, value->car);
        if (value->cdr && value->cdr->type == VAL_PAIR) {
            append_to_buffer(buffer, size, " ");
            value = value->cdr;
            continue;
        }
        if (value->cdr && value->cdr->type != VAL_NIL) {
            append_to_buffer(buffer, size, " . ");
            append_value_to_buffer(buffer, size, value->cdr);
        }
        break;
    }
    append_to_buffer(buffer, size, ")");
}

static void append_value_to_buffer(char *buffer, size_t size, Value *value) {
    if (!value || size == 0) return;
    char tmp[64];
    switch (value->type) {
        case VAL_NUMBER:
            snprintf(tmp, sizeof(tmp), "%g", value->number);
            append_to_buffer(buffer, size, tmp);
            break;
        case VAL_SYMBOL:
            append_to_buffer(buffer, size, value->symbol ? value->symbol : "#<symbol>");
            break;
        case VAL_PAIR:
            append_pair_to_buffer(buffer, size, value);
            break;
        case VAL_NIL:
            append_to_buffer(buffer, size, "()");
            break;
        case VAL_BUILTIN:
            append_to_buffer(buffer, size, "#<builtin>");
            break;
        case VAL_LAMBDA:
            append_to_buffer(buffer, size, "#<lambda>");
            break;
        default:
            append_to_buffer(buffer, size, "#<unknown>");
            break;
    }
}

#ifdef __EMSCRIPTEN__
static void wasm_emit_line(const char *line) {
    EM_ASM({ Module.print(UTF8ToString($0)); }, line);
}
#else
static void wasm_emit_line(const char *line) {
    (void)line;
}
#endif

static void emit_console_line(const char *line) {
    printf("%s\n", line);
    fflush(stdout);
    wasm_emit_line(line);
}

static void env_define(Env *env, const char *name, Value *value) {
    Binding *b = env->bindings;
    while (b) {
        if (strcmp(b->name, name) == 0) {
            binding_set_value(b, value);
            return;
        }
        b = b->next;
    }
    Binding *binding = (Binding*)gc_allocate(sizeof(Binding));
    gc_set_trace(binding, trace_binding);
    gc_set_tag(binding, GC_TAG_BINDING);
    binding->name = gc_copy_cstring(name);
    binding_set_value(binding, value);
    binding_set_next(binding, env->bindings);
    env_set_bindings(env, binding);
}

static int env_set(Env *env, const char *name, Value *value) {
    for (Env *e = env; e; e = e->parent) {
        Binding *b = e->bindings;
        while (b) {
            if (strcmp(b->name, name) == 0) {
                binding_set_value(b, value);
                return 1;
            }
            b = b->next;
        }
    }
    return 0;
}

static Value *env_lookup(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent) {
        Binding *b = e->bindings;
        while (b) {
            if (strcmp(b->name, name) == 0) {
                return b->value;
            }
            b = b->next;
        }
    }
    runtime_error("Undefined symbol: %s", name);
    return NIL;
}

static Value *read_form(void);
static Value *eval_value(Value *expr, Env *env);
static Value *eval_sequence(Value *exprs, Env *env);

static Value *read_list(void) {
    Value *head = NIL;
    Value **tail = &head;
    while (cur_token.type != TOK_RPAREN && cur_token.type != TOK_EOF) {
        Value *element = read_form();
        Value *node = make_pair(element, NIL);
        *tail = node;
        tail = &node->cdr;
    }
    consume(TOK_RPAREN);
    return head;
}

static Value *read_form(void) {
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
        return read_list();
    } else if (cur_token.type == TOK_QUOTE) {
        consume(TOK_QUOTE);
        Value *inner = read_form();
        Value *quote_sym = make_symbol_copy("quote");
        return make_pair(quote_sym, make_pair(inner, NIL));
    } else {
        runtime_error("Unexpected token while reading");
        return NIL;
    }
}

static Value *builtin_add(Value **args, int argc, Env *env) {
    (void)env;
    double sum = 0;
    for (int i = 0; i < argc; ++i) {
        if (!args[i] || args[i]->type != VAL_NUMBER) runtime_error("+ expects numbers");
        sum += args[i]->number;
    }
    return make_number(sum);
}

static Value *builtin_sub(Value **args, int argc, Env *env) {
    (void)env;
    if (argc == 0) runtime_error("- expects at least one argument");
    if (!args[0] || args[0]->type != VAL_NUMBER) runtime_error("- expects numbers");
    double result = args[0]->number;
    if (argc == 1) {
        result = -result;
    } else {
        for (int i = 1; i < argc; ++i) {
            if (!args[i] || args[i]->type != VAL_NUMBER) runtime_error("- expects numbers");
            result -= args[i]->number;
        }
    }
    return make_number(result);
}

static Value *builtin_mul(Value **args, int argc, Env *env) {
    (void)env;
    double product = 1;
    for (int i = 0; i < argc; ++i) {
        if (!args[i] || args[i]->type != VAL_NUMBER) runtime_error("* expects numbers");
        product *= args[i]->number;
    }
    return make_number(product);
}

static Value *builtin_div(Value **args, int argc, Env *env) {
    (void)env;
    if (argc == 0) runtime_error("/ expects at least one argument");
    if (!args[0] || args[0]->type != VAL_NUMBER) runtime_error("/ expects numbers");
    double result = args[0]->number;
    for (int i = 1; i < argc; ++i) {
        if (!args[i] || args[i]->type != VAL_NUMBER) runtime_error("/ expects numbers");
        result /= args[i]->number;
    }
    return make_number(result);
}

static Value *builtin_print(Value **args, int argc, Env *env);
static Value *builtin_cons(Value **args, int argc, Env *env);
static Value *builtin_car(Value **args, int argc, Env *env);
static Value *builtin_cdr(Value **args, int argc, Env *env);
static Value *builtin_list(Value **args, int argc, Env *env);
static Value *builtin_eq(Value **args, int argc, Env *env);
static Value *builtin_lt(Value **args, int argc, Env *env);
static Value *builtin_gt(Value **args, int argc, Env *env);
static Value *builtin_lte(Value **args, int argc, Env *env);
static Value *builtin_gte(Value **args, int argc, Env *env);
static Value *builtin_gc(Value **args, int argc, Env *env);
static Value *builtin_gc_threshold(Value **args, int argc, Env *env);
static Value *builtin_gc_stats(Value **args, int argc, Env *env);

static Value *builtin_print(Value **args, int argc, Env *env) {
    (void)env;
    char line[512];
    line[0] = '\0';
    for (int i = 0; i < argc; ++i) {
        if (i) append_to_buffer(line, sizeof(line), " ");
        append_value_to_buffer(line, sizeof(line), args[i]);
    }
    emit_console_line(line);
    return NIL;
}

static Value *builtin_cons(Value **args, int argc, Env *env) {
    (void)env;
    if (argc != 2) runtime_error("cons expects two arguments");
    return make_pair(args[0], args[1]);
}

static Value *builtin_car(Value **args, int argc, Env *env) {
    (void)env;
    if (argc != 1) runtime_error("car expects one argument");
    if (!args[0] || args[0]->type != VAL_PAIR) runtime_error("car expects a list");
    return args[0]->car ? args[0]->car : NIL;
}

static Value *builtin_cdr(Value **args, int argc, Env *env) {
    (void)env;
    if (argc != 1) runtime_error("cdr expects one argument");
    if (!args[0] || args[0]->type != VAL_PAIR) runtime_error("cdr expects a list");
    return args[0]->cdr ? args[0]->cdr : NIL;
}

static Value *builtin_list(Value **args, int argc, Env *env) {
    (void)env;
    Value *head = NIL;
    Value **tail = &head;
    for (int i = 0; i < argc; ++i) {
        Value *node = make_pair(args[i], NIL);
        *tail = node;
        tail = &node->cdr;
    }
    return head;
}

static Value *compare_chain(Value **args, int argc, int (*cmp)(double, double), const char *name) {
    if (argc < 2) runtime_error("%s expects at least two numbers", name);
    for (int i = 0; i < argc; ++i) {
        if (!args[i] || args[i]->type != VAL_NUMBER) runtime_error("%s expects numbers", name);
    }
    for (int i = 0; i < argc - 1; ++i) {
        if (!cmp(args[i]->number, args[i + 1]->number)) {
            return NIL;
        }
    }
    return TRUE;
}

static int cmp_eq(double a, double b) { return a == b; }
static int cmp_lt(double a, double b) { return a < b; }
static int cmp_gt(double a, double b) { return a > b; }
static int cmp_lte(double a, double b) { return a <= b; }
static int cmp_gte(double a, double b) { return a >= b; }

static Value *builtin_eq(Value **args, int argc, Env *env)   { (void)env; return compare_chain(args, argc, cmp_eq, "="); }
static Value *builtin_lt(Value **args, int argc, Env *env)   { (void)env; return compare_chain(args, argc, cmp_lt, "<"); }
static Value *builtin_gt(Value **args, int argc, Env *env)   { (void)env; return compare_chain(args, argc, cmp_gt, ">"); }
static Value *builtin_lte(Value **args, int argc, Env *env)  { (void)env; return compare_chain(args, argc, cmp_lte, "<="); }
static Value *builtin_gte(Value **args, int argc, Env *env)  { (void)env; return compare_chain(args, argc, cmp_gte, ">="); }
static Value *builtin_gc(Value **args, int argc, Env *env)   { (void)args; (void)argc; (void)env; gc_collect(); return NIL; }
static Value *builtin_gc_threshold(Value **args, int argc, Env *env) {
    (void)env;
    if (argc == 0) {
        return make_number((double)gc_get_threshold());
    }
    if (argc == 1) {
        if (!args[0] || args[0]->type != VAL_NUMBER) {
            runtime_error("gc-threshold expects a numeric byte value");
        }
        if (args[0]->number < 0) runtime_error("gc-threshold cannot be negative");
        gc_set_threshold((size_t)args[0]->number);
        return make_number((double)gc_get_threshold());
    }
    runtime_error("gc-threshold accepts zero or one argument");
    return NIL;
}

static Value *builtin_gc_stats(Value **args, int argc, Env *env) {
    (void)args; (void)argc; (void)env;
    GcStats stats;
    gc_get_stats(&stats);

    // Construct association list: ((collections . N) (allocated . N) (freed . N) (current . N))
    // We build it in reverse order to make it easier: current -> freed -> allocated -> collections
    
    Value *current_pair = make_pair(make_symbol_copy("current"), make_number((double)stats.current_bytes));
    Value *list = make_pair(current_pair, NIL); // (current . N)

    Value *freed_pair = make_pair(make_symbol_copy("freed"), make_number((double)stats.freed_bytes));
    list = make_pair(freed_pair, list); // (freed . N) ...

    Value *allocated_pair = make_pair(make_symbol_copy("allocated"), make_number((double)stats.allocated_bytes));
    list = make_pair(allocated_pair, list); // (allocated . N) ...

    Value *collections_pair = make_pair(make_symbol_copy("collections"), make_number((double)stats.collections));
    list = make_pair(collections_pair, list); // (collections . N) ...

    return list;
}

static void install_builtin(Env *env, const char *name, BuiltinFunc fn) {
    env_define(env, name, make_builtin(fn));
}

static void init_builtins(Env *env) {
    env_define(env, "nil", NIL);
    env_define(env, "t", TRUE);
    install_builtin(env, "+", builtin_add);
    install_builtin(env, "-", builtin_sub);
    install_builtin(env, "*", builtin_mul);
    install_builtin(env, "/", builtin_div);
    install_builtin(env, "print", builtin_print);
    install_builtin(env, "cons", builtin_cons);
    install_builtin(env, "car", builtin_car);
    install_builtin(env, "cdr", builtin_cdr);
    install_builtin(env, "list", builtin_list);
    install_builtin(env, "=", builtin_eq);
    install_builtin(env, "<", builtin_lt);
    install_builtin(env, ">", builtin_gt);
    install_builtin(env, "<=", builtin_lte);
    install_builtin(env, ">=", builtin_gte);
    install_builtin(env, "gc", builtin_gc);
    install_builtin(env, "gc-threshold", builtin_gc_threshold);
    install_builtin(env, "gc-stats", builtin_gc_stats);
}

static void runtime_init(void) {
    if (runtime_initialized) return;
    gc_init();
    global_env = env_new(NULL);
    gc_add_root((void**)&global_env);
    
    // Register temporary roots
    for (int i = 0; i < MAX_TEMP_ROOTS; ++i) {
        temp_roots[i] = NIL;
        gc_add_root((void**)&temp_roots[i]);
    }
    
    init_builtins(global_env);
    runtime_initialized = 1;
    load_standard_library();
}

static Value *eval_value(Value *expr, Env *env) {
    if (!expr) return NIL;
    switch (expr->type) {
        case VAL_NUMBER:
        case VAL_NIL:
        case VAL_LAMBDA:
        case VAL_BUILTIN:
            return expr;
        case VAL_SYMBOL:
            return env_lookup(env, expr->symbol);
        case VAL_PAIR: {
            Value *op = expr->car;
            if (!op) return NIL;
            if (op->type == VAL_SYMBOL) {
                const char *name = op->symbol;
                Value *args = expr->cdr;
                if (strcmp(name, "quote") == 0) {
                    if (is_nil(args)) runtime_error("quote expects an argument");
                    return args->car;
                } else if (strcmp(name, "define") == 0) {
                    if (is_nil(args)) runtime_error("define expects a symbol or list");
                    Value *target = args->car;
                    Value *value_exprs = args->cdr;
                    if (is_nil(value_exprs)) runtime_error("define missing value");
                    if (target->type == VAL_SYMBOL) {
                        Value *val = eval_value(value_exprs->car, env);
                        env_define(env, target->symbol, val);
                        return target;
                    } else if (target->type == VAL_PAIR) {
                        Value *fn_name = target->car;
                        if (!fn_name || fn_name->type != VAL_SYMBOL) {
                            runtime_error("define function requires a name");
                        }
                        Value *lambda_params = target->cdr;
                        Value *lambda_body = value_exprs;
                        Value *lambda_value = make_lambda(lambda_params, lambda_body, env);
                        env_define(env, fn_name->symbol, lambda_value);
                        return fn_name;
                    } else {
                        runtime_error("define expects a symbol or (name args)");
                    }
                } else if (strcmp(name, "lambda") == 0) {
                    if (is_nil(args)) runtime_error("lambda expects parameters");
                    Value *params = args->car;
                    Value *body = args->cdr;
                    if (is_nil(body)) runtime_error("lambda body cannot be empty");
                    return make_lambda(params, body, env);
                } else if (strcmp(name, "if") == 0) {
                    Value *test_expr = args ? args->car : NIL;
                    Value *rest = args ? args->cdr : NIL;
                    Value *then_expr = rest ? rest->car : NIL;
                    Value *else_expr = (rest && rest->cdr) ? rest->cdr->car : NIL;
                    Value *test_val = eval_value(test_expr, env);
                    if (is_truthy(test_val)) {
                        return eval_value(then_expr, env);
                    } else {
                        if (!is_nil(else_expr)) return eval_value(else_expr, env);
                        return NIL;
                    }
                } else if (strcmp(name, "begin") == 0) {
                    return eval_sequence(args, env);
                }
            }
            // General application
            int sp_start = temp_root_sp;
            Value *operator = eval_value(op, env);
            push_root(operator);
            
            Value *arg_values[MAX_ARGS];
            int argc = 0;
            Value *arg_list = expr->cdr;
            while (!is_nil(arg_list)) {
                if (arg_list->type != VAL_PAIR) runtime_error("Malformed argument list");
                if (argc >= MAX_ARGS) runtime_error("Too many arguments");
                Value *val = eval_value(arg_list->car, env);
                arg_values[argc++] = val;
                push_root(val);
                arg_list = arg_list->cdr;
            }
            
            Value *result = NIL;
            if (!operator) runtime_error("Attempt to call nil");
            if (operator->type == VAL_BUILTIN) {
                result = operator->builtin(arg_values, argc, env);
            } else if (operator->type == VAL_LAMBDA) {
                Env *call_env = env_new(operator->env ? operator->env : env);
                push_root((Value*)call_env);
                Value *param_list = operator->params;
                int index = 0;
                while (!is_nil(param_list)) {
                    if (param_list->type != VAL_PAIR) runtime_error("Malformed parameter list");
                    Value *param = param_list->car;
                    if (!param || param->type != VAL_SYMBOL) runtime_error("Parameters must be symbols");
                    if (index >= argc) runtime_error("Too few arguments supplied");
                    env_define(call_env, param->symbol, arg_values[index++]);
                    param_list = param_list->cdr;
                }
                if (index != argc) runtime_error("Too many arguments supplied");
                result = eval_sequence(operator->body, call_env);
                pop_root();
            } else {
                runtime_error("Attempt to call non-procedure");
            }
            
            temp_root_sp = sp_start;
            return result;
        }
        default:
            runtime_error("Cannot evaluate expression");
            return NIL;
    }
}

static Value *eval_sequence(Value *exprs, Env *env) {
    Value *result = NIL;
    Value *current = exprs;
    while (!is_nil(current)) {
        if (current->type != VAL_PAIR) runtime_error("Malformed expression list");
        result = eval_value(current->car, env);
        current = current->cdr;
    }
    return result;
}

static Value *eval_source(const char *src, int *out_error) {
    runtime_init();
    Value *result = NIL;
    eval_jmp_active = 1;
    if (setjmp(eval_jmp_buf) == 0) {
        input_ptr = src;
        cur_token = next_token();
        temp_root_sp = 0; // Reset temp roots at top level
        while (cur_token.type != TOK_EOF) {
            Value *form = read_form();
            push_root(form);
            result = eval_value(form, global_env);
            pop_root();
        }
        if (out_error) *out_error = 0;
    } else {
        if (out_error) *out_error = 1;
    }
    eval_jmp_active = 0;
    if (!out_error || !*out_error) {
        gc_add_root((void**)&result);
        gc_collect();
        gc_remove_root((void**)&result);
    } else {
        gc_collect();
    }
    return result;
}

static void trace_binding(void *obj) {
    Binding *binding = (Binding*)obj;
    if (binding->name) binding->name = (const char*)gc_mark_ptr((void*)binding->name);
    if (binding->value) binding->value = gc_mark_ptr(binding->value);
    if (binding->next) binding->next = (Binding*)gc_mark_ptr(binding->next);
}

static void trace_env(void *obj) {
    Env *env = (Env*)obj;
    if (env->parent) env->parent = (Env*)gc_mark_ptr(env->parent);
    if (env->bindings) env->bindings = (Binding*)gc_mark_ptr(env->bindings);
}

static void trace_value(void *obj) {
    Value *value = (Value*)obj;
    switch (value->type) {
        case VAL_PAIR:
            if (value->car) value->car = gc_mark_ptr(value->car);
            if (value->cdr) value->cdr = gc_mark_ptr(value->cdr);
            break;
        case VAL_SYMBOL:
            if (value->symbol) value->symbol = (char*)gc_mark_ptr(value->symbol);
            break;
        case VAL_LAMBDA:
            if (value->params) value->params = gc_mark_ptr(value->params);
            if (value->body) value->body = gc_mark_ptr(value->body);
            if (value->env) value->env = (Env*)gc_mark_ptr(value->env);
            break;
        case VAL_BUILTIN:
        case VAL_NUMBER:
        case VAL_NIL:
        default:
            break;
    }
}

static void print_pair(Value *value);
void print_value(Value *value) {
    if (is_nil(value)) {
        printf("()");
    } else if (value->type == VAL_NUMBER) {
        printf("%g", value->number);
    } else if (value->type == VAL_SYMBOL) {
        printf("%s", value->symbol);
    } else if (value->type == VAL_PAIR) {
        print_pair(value);
    } else if (value->type == VAL_BUILTIN) {
        printf("#<builtin>");
    } else if (value->type == VAL_LAMBDA) {
        printf("#<lambda>");
    } else {
        printf("<unknown>");
    }
}

static void print_pair(Value *value) {
    printf("(");
    while (1) {
        print_value(value->car ? value->car : NIL);
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

static void append_line(char **buffer, size_t *len, size_t *capacity, const char *line) {
    size_t line_len = strlen(line);
    size_t needed = *len + line_len + 2;
    if (needed > *capacity) {
        size_t new_cap = *capacity ? *capacity : 128;
        while (new_cap < needed) new_cap *= 2;
        char *tmp = realloc(*buffer, new_cap);
        if (!tmp) {
            fprintf(stderr, "Out of memory while reading input\n");
            exit(1);
        }
        *buffer = tmp;
        *capacity = new_cap;
    }
    memcpy(*buffer + *len, line, line_len);
    *len += line_len;
    (*buffer)[(*len)++] = '\n';
    (*buffer)[*len] = '\0';
}

static int buffer_has_content(const char *buffer) {
    if (!buffer) return 0;
    for (const char *p = buffer; *p; ++p) {
        if (!isspace((unsigned char)*p)) return 1;
    }
    return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int form_needs_more_input(const char *buffer) {
    if (!buffer) return 0;
    int depth = 0;
    int in_string = 0;
    int escaping = 0;
    for (const char *p = buffer; *p; ++p) {
        char c = *p;
        if (in_string) {
            if (escaping) {
                escaping = 0;
            } else if (c == '\\') {
                escaping = 1;
            } else if (c == '"') {
                in_string = 0;
            }
            continue;
        }
        if (c == '"') {
            in_string = 1;
            continue;
        }
        if (c == ';') {
            while (*p && *p != '\n') p++;
            if (!*p) break;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')') {
            if (depth > 0) depth--;
        }
    }
    return depth > 0 || in_string;
}

static void repl(void) {
    printf("Minimalisp REPL. Press Ctrl-D to exit.\n");
    char *form_buffer = NULL;
    size_t buf_len = 0;
    size_t buf_capacity = 0;
    while (1) {
        printf(buf_len ? "...> " : "ml> ");
        fflush(stdout);
        char *line = read_line();
        if (!line) {
            printf("\n");
            break;
        }
        if (line[0] == '\0' && buf_len == 0) {
            free(line);
            continue;
        }
        append_line(&form_buffer, &buf_len, &buf_capacity, line);
        free(line);
        if (!buffer_has_content(form_buffer)) {
            buf_len = 0;
            if (form_buffer) form_buffer[0] = '\0';
            continue;
        }
        if (form_needs_more_input(form_buffer)) {
            continue;
        }
        int had_error = 0;
        Value *value = eval_source(form_buffer, &had_error);
        if (!had_error) {
            print_value(value);
            printf("\n");
        }
        buf_len = 0;
        if (form_buffer) form_buffer[0] = '\0';
    }
    free(form_buffer);
}

static char *read_file_internal(const char *path, int warn_on_error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (warn_on_error) fprintf(stderr, "Failed to open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    if (fread(buffer, 1, size, f) != (size_t)size) {
        fclose(f);
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(f);
    return buffer;
}

static char *read_file(const char *path) {
    return read_file_internal(path, 1);
}

static void load_standard_library(void) {
    static int loaded = 0;
    if (loaded) return;
    loaded = 1;
    const char *paths[] = {
        "standard-lib.lisp",
#ifdef __EMSCRIPTEN__
        "/standard-lib.lisp",
#endif
        NULL
    };
    char *contents = NULL;
    for (int i = 0; paths[i]; ++i) {
        contents = read_file_internal(paths[i], 0);
        if (contents) break;
    }
    if (!contents) {
        fprintf(stderr, "Warning: standard-lib.lisp not found; continuing without standard library\n");
        return;
    }
    int had_error = 0;
    Value *value = eval_source(contents, &had_error);
    (void)value;
    if (had_error) {
        fprintf(stderr, "Warning: Failed to load standard-lib.lisp\n");
    }
    free(contents);
}

static void print_value_to_buffer(char *buffer, size_t size, Value *value) {
    if (size == 0) return;
    buffer[0] = '\0';
    append_value_to_buffer(buffer, size, value);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EMSCRIPTEN_KEEPALIVE
#endif
const char* eval(const char *src) {
    static char output_buffer[1024];
    output_buffer[0] = '\0';
    
    int had_error = 0;
    Value *value = eval_source(src, &had_error);
    
    if (had_error) {
        snprintf(output_buffer, sizeof(output_buffer), "Error");
        return output_buffer;
    }
    
    if (!value) {
        snprintf(output_buffer, sizeof(output_buffer), "nil");
        return output_buffer;
    }
    
    print_value_to_buffer(output_buffer, sizeof(output_buffer), value);
    return output_buffer;
}

#ifndef __EMSCRIPTEN__
int main(int argc, char **argv) {
    if (argc == 1) {
        repl();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        char *contents = read_file(argv[2]);
        if (!contents) return 1;
        int had_error = 0;
        Value *value = eval_source(contents, &had_error);
        free(contents);
        if (had_error) return 1;
        printf("Result: ");
        print_value(value);
        printf("\n");
        return 0;
    }
    int had_error = 0;
    Value *value = eval_source(argv[1], &had_error);
    if (had_error) return 1;
    printf("Result: ");
    print_value(value);
    printf("\n");
    return 0;
}
#else
int main() { return 0; }
#endif
