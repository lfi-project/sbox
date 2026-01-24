// Simple test library

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

char* process_string(char* s) {
    // Just return the string as-is for now
    return s;
}

typedef void (*callback_t)(int);
static callback_t stored_callback = 0;

void set_callback(callback_t cb) {
    stored_callback = cb;
}

void trigger_callback(int value) {
    if (stored_callback) {
        stored_callback(value);
    }
}
