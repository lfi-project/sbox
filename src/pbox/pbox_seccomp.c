#define _GNU_SOURCE
#include "pbox_seccomp.h"

#include <asm/unistd.h>
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

// Architecture check value for x86-64
#if defined(__x86_64__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_X86_64
#elif defined(__i386__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_I386
#elif defined(__aarch64__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_AARCH64
#elif defined(__arm__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_ARM
#else
#error "Unsupported architecture"
#endif

// BPF macros for readability
#define ALLOW SECCOMP_RET_ALLOW
#define KILL SECCOMP_RET_KILL_PROCESS
#define ERRNO(e) (SECCOMP_RET_ERRNO | ((e) & SECCOMP_RET_DATA))

// Load syscall number
#define BPF_LOAD_SYSCALL_NR \
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr))

// Load architecture
#define BPF_LOAD_ARCH \
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch))

// Load syscall argument (0-5)
#define BPF_LOAD_ARG(n) \
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[n]))

// Jump if equal
#define BPF_SYSCALL_ALLOW(syscall_nr)                      \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, syscall_nr, 0, 1), \
        BPF_STMT(BPF_RET | BPF_K, ALLOW)

// Return action
#define BPF_RETURN(action) BPF_STMT(BPF_RET | BPF_K, action)

int pbox_install_seccomp(void) {
    struct sock_filter filter[] = {
        // Verify architecture
        BPF_LOAD_ARCH,
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_CURRENT, 1, 0),
        BPF_RETURN(KILL),

        // Load syscall number
        BPF_LOAD_SYSCALL_NR,

        // === Memory management ===
        BPF_SYSCALL_ALLOW(__NR_brk),
        BPF_SYSCALL_ALLOW(__NR_mmap),
        BPF_SYSCALL_ALLOW(__NR_munmap),
        BPF_SYSCALL_ALLOW(__NR_mprotect),
        BPF_SYSCALL_ALLOW(__NR_mremap),
        BPF_SYSCALL_ALLOW(__NR_madvise),

        // === File descriptors (before clone to avoid BPF issues) ===
        BPF_SYSCALL_ALLOW(__NR_close),
        BPF_SYSCALL_ALLOW(__NR_recvmsg),
#ifdef __NR_socketcall
        BPF_SYSCALL_ALLOW(__NR_socketcall),
#endif

        // === Threading (futex) ===
        BPF_SYSCALL_ALLOW(__NR_futex),
#ifdef __NR_futex_waitv
        BPF_SYSCALL_ALLOW(__NR_futex_waitv),
#endif
        BPF_SYSCALL_ALLOW(__NR_set_tid_address),
        BPF_SYSCALL_ALLOW(__NR_set_robust_list),
        BPF_SYSCALL_ALLOW(__NR_get_robust_list),
#ifdef __NR_rseq
        BPF_SYSCALL_ALLOW(__NR_rseq),
#endif

        // === Signals ===
        BPF_SYSCALL_ALLOW(__NR_rt_sigaction),
        BPF_SYSCALL_ALLOW(__NR_rt_sigprocmask),
        BPF_SYSCALL_ALLOW(__NR_rt_sigreturn),
        BPF_SYSCALL_ALLOW(__NR_sigaltstack),

        // === Process exit ===
        BPF_SYSCALL_ALLOW(__NR_exit),
        BPF_SYSCALL_ALLOW(__NR_exit_group),

    // === Architecture/TLS ===
#ifdef __NR_arch_prctl
        BPF_SYSCALL_ALLOW(__NR_arch_prctl),
#endif
        BPF_SYSCALL_ALLOW(__NR_prctl),

        // === Safe information queries ===
        BPF_SYSCALL_ALLOW(__NR_getpid),
        BPF_SYSCALL_ALLOW(__NR_gettid),
        BPF_SYSCALL_ALLOW(__NR_getuid),
        BPF_SYSCALL_ALLOW(__NR_geteuid),
        BPF_SYSCALL_ALLOW(__NR_getgid),
        BPF_SYSCALL_ALLOW(__NR_getegid),

        // === Misc commonly needed ===
        BPF_SYSCALL_ALLOW(__NR_getrandom),
        BPF_SYSCALL_ALLOW(__NR_clock_gettime),
        BPF_SYSCALL_ALLOW(__NR_clock_getres),
        BPF_SYSCALL_ALLOW(__NR_gettimeofday),
        BPF_SYSCALL_ALLOW(__NR_nanosleep),
#ifdef __NR_clock_nanosleep
        BPF_SYSCALL_ALLOW(__NR_clock_nanosleep),
#endif

        // === Scheduler (for threads) ===
        BPF_SYSCALL_ALLOW(__NR_sched_yield),
        BPF_SYSCALL_ALLOW(__NR_sched_getaffinity),

        // === Thread creation (for pthread_create) ===
        BPF_SYSCALL_ALLOW(__NR_clone),
#ifdef __NR_clone3
        BPF_SYSCALL_ALLOW(__NR_clone3),
#endif
#ifdef __NR_tgkill
        BPF_SYSCALL_ALLOW(__NR_tgkill),
#endif
#ifdef __NR_membarrier
        BPF_SYSCALL_ALLOW(__NR_membarrier),
#endif

        // === Default: return ENOSYS for unallowed syscalls ===
        BPF_RETURN(ERRNO(ENOSYS)),
    };

    struct sock_fprog prog = {
        .len = (unsigned short) (sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // Prevent privilege escalation via execve (even though we block it)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        return -1;
    }

    // Install the filter
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
        return -1;
    }

    return 0;
}

// Block clone syscall
#define BPF_SYSCALL_BLOCK(syscall_nr)                      \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, syscall_nr, 0, 1), \
        BPF_STMT(BPF_RET | BPF_K, ERRNO(ENOSYS))

int pbox_install_seccomp_worker(void) {
    // This filter only blocks clone/clone3, allowing everything else.
    // It stacks on top of the main filter installed by pbox_install_seccomp().
    struct sock_filter filter[] = {
        // Load syscall number
        BPF_LOAD_SYSCALL_NR,

        // Block clone
        BPF_SYSCALL_BLOCK(__NR_clone),
#ifdef __NR_clone3
        BPF_SYSCALL_BLOCK(__NR_clone3),
#endif

        // Allow everything else (defers to base filter)
        BPF_RETURN(ALLOW),
    };

    struct sock_fprog prog = {
        .len = (unsigned short) (sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // No need to set NO_NEW_PRIVS again - already set by main filter
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
        return -1;
    }

    return 0;
}
