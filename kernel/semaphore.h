struct semaphore {
    int value;
    struct spinlock chan;
};