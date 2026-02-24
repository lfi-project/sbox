// Simple functions to call from multiple threads

int add(int a, int b) {
    return a + b;
}

// Simulate some work
int slow_add(int a, int b) {
    volatile int sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += 1;
    }
    return a + b;
}
