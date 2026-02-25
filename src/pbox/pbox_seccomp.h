#ifndef PBOX_SECCOMP_H
#define PBOX_SECCOMP_H

// Install seccomp filter for control thread that allows clone for threading.
// Returns 0 on success, -1 on failure.
int pbox_install_seccomp(void);

// Install additional seccomp filter for worker threads that blocks clone.
// This should be called by worker threads to prevent them from spawning
// threads. Returns 0 on success, -1 on failure.
int pbox_install_seccomp_worker(void);

#endif  // PBOX_SECCOMP_H
