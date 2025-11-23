# Makefile for building the Lisp interpreter to WebAssembly or native
WASM_CC ?= emcc
WASM_CFLAGS ?= -O2 -s WASM=1 -s EXPORTED_FUNCTIONS='["_eval"]' -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]'
NATIVE_CC ?= gcc
NATIVE_CFLAGS ?= -lm
SRC = interpreter.c gc.c
WASM_TARGET = interpreter.wasm
NATIVE_TARGET = interpreter

.PHONY: all native test-native clean

all: $(WASM_TARGET)

$(WASM_TARGET): $(SRC)
	$(WASM_CC) $(WASM_CFLAGS) -o $@ $^

native: $(NATIVE_TARGET)

$(NATIVE_TARGET): $(SRC)
	$(NATIVE_CC) -o $@ $^ $(NATIVE_CFLAGS)

test-native: native
	./$(NATIVE_TARGET) "(+ 1 (* 2 3))" >/dev/null
	printf '(+ 1 2)\n' | ./$(NATIVE_TARGET) >/dev/null
	./$(NATIVE_TARGET) "(car (list 1 2 3))" >/dev/null
	./$(NATIVE_TARGET) "(cdr (list 1 2 3))" >/dev/null
	./$(NATIVE_TARGET) "'(1 2 (3 4))" >/dev/null
	./$(NATIVE_TARGET) "(begin (define (double x) (+ x x)) (double 8))" >/dev/null
	./$(NATIVE_TARGET) -f hanoi.lisp >/dev/null
	./$(NATIVE_TARGET) "(gc)" >/dev/null
	./$(NATIVE_TARGET) "(gc-threshold 2048)" >/dev/null

clean:
	rm -f $(WASM_TARGET) $(NATIVE_TARGET)
