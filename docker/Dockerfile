FROM ubuntu:focal

# set environment variables for tzdata
ARG TZ=America/New_York
ENV TZ=${TZ}

# include manual pages and documentation
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update &&\
  yes | unminimize

# install GCC-related packages
RUN apt-get -y install\
 binutils-doc\
 cpp-doc\
 gcc-doc\
 g++\
 g++-multilib\
 gdb\
 gdb-doc\
 glibc-doc\
 libblas-dev\
 liblapack-dev\
 liblapack-doc\
 libstdc++-10-doc\
 make\
 make-doc

# install clang-related packages
RUN apt-get -y install\
 clang\
 clang-10-doc\
 lldb

# install qemu for WeensyOS (sadly, this pulls in a lot of crap)
RUN apt-get -y install\
 qemu-system-x86

# install programs used for system exploration
RUN apt-get -y install\
 blktrace\
 linux-tools-generic\
 strace\
 tcpdump

# install interactive programs (emacs, vim, nano, man, sudo, etc.)
RUN apt-get -y install\
 bc\
 curl\
 dc\
 emacs-nox\
 git\
 git-doc\
 man\
 micro\
 nano\
 psmisc\
 sudo\
 vim\
 wget

# set up libraries
RUN apt-get -y install\
 libreadline-dev\
 locales\
 wamerican

# install programs used for networking
RUN apt-get -y install\
 dnsutils\
 inetutils-ping\
 iproute2\
 net-tools\
 netcat\
 telnet\
 time\
 traceroute

# set up default locale
RUN locale-gen en_US.UTF-8
ENV LANG en_US.UTF-8

# remove unneeded .deb files
RUN rm -r /var/lib/apt/lists/*

# set up passwordless sudo for user cs61-user
RUN useradd -m -s /bin/bash cs61-user && \
  echo "cs61-user ALL=(ALL:ALL) NOPASSWD: ALL" > /etc/sudoers.d/cs61-init

# create binary reporting version of dockerfile
RUN (echo '#\!/bin/sh'; echo 'echo 12') > /usr/bin/cs61-docker-version; chmod ugo+rx,u+w,go-w /usr/bin/cs61-docker-version

# git build arguments
ARG USER=CS61\ User
ARG EMAIL=nobody@example.com

# configure your environment
USER cs61-user
RUN git config --global user.name "${USER}" && \
  git config --global user.email "${EMAIL}" && \
  (echo "(custom-set-variables"; echo " '(c-basic-offset 4)"; echo " '(indent-tabs-mode nil))") > ~/.emacs && \
  (echo "set expandtab"; echo "set shiftwidth=4"; echo "set softtabstop=4") > ~/.vimrc && \
  (echo "if test -f /run/host-services/ssh-auth.sock; then"; echo "  sudo chown cs61-user:cs61-user /run/host-services/ssh-auth.sock"; echo "fi") > ~/.bash_profile && \
  echo ". ~/.bashrc" >> ~/.bash_profile && \
  rm -f ~/.bash_logout && \
  echo "add-auto-load-safe-path ~" > ~/.gdbinit

WORKDIR /home/cs61-user
CMD ["/bin/bash", "-l"]

# Initial version of this Dockerfile by Todd Morrill, CS 61 DCE student
