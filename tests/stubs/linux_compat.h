#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

#include <sys/socket.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

#ifndef ENONET
#define ENONET ENETDOWN
#endif

static int accept4(int socket, struct sockaddr *address,
                   socklen_t *address_len, int flags) {
  (void)flags;
  return accept(socket, address, address_len);
}

#endif
