#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <signal.h>

int eventfd(unsigned int initval, int flags) {
    return syscall(SYS_eventfd2, initval, flags);
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    return syscall(SYS_accept4, sockfd, addr, addrlen, flags);
}

int dup3(int oldfd, int newfd, int flags) {
    return syscall(SYS_dup3, oldfd, newfd, flags);
}

ssize_t preadv64(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    return syscall(SYS_preadv, fd, iov, iovcnt, offset);
}

ssize_t pwritev64(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    return syscall(SYS_pwritev, fd, iov, iovcnt, offset);
}

int pipe2(int pipefd[2], int flags) {
    return syscall(SYS_pipe2, pipefd, flags);
}

int sendmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, unsigned int flags) {
    return syscall(SYS_sendmmsg, sockfd, msgvec, vlen, flags);
}

int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                unsigned int flags, struct timespec *timeout) {
    return syscall(SYS_recvmmsg, sockfd, msgvec, vlen, flags, timeout);
}

int epoll_create1(int flags) {
    return syscall(SYS_epoll_create1, flags);
}

int inotify_init1(int flags) {
    return syscall(SYS_inotify_init1, flags);
}

int futimens(int fd, const struct timespec times[2]) {
    return syscall(SYS_utimensat, fd, NULL, times, 0);
}

int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags) {
    return syscall(SYS_utimensat, dirfd, pathname, times, flags);
}

int sched_getcpu(void) {
#ifdef SYS_getcpu
    unsigned int cpu;
    int r = syscall(SYS_getcpu, &cpu, NULL, NULL);
    if (r < 0)
        return r;
    return (int)cpu;
#else
    errno = ENOSYS;
    return -1;
#endif
}

int __sched_cpucount(size_t setsize, const cpu_set_t *setp) {
    int count = 0;
    const unsigned char *p = (const unsigned char *)setp;
    const unsigned char *end = p + setsize;
    
    while (p < end) {
        unsigned char byte = *p++;
        while (byte) {
            count += byte & 1;
            byte >>= 1;
        }
    }
    return count;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask) {
    return syscall(SYS_epoll_pwait, epfd, events, maxevents, timeout, sigmask, sigmask ? sizeof(sigset_t) : 0);
}
