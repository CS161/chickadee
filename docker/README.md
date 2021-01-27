CS 61/161 Docker
================

> **tl;dr**:
> * `docker build -t cs61:latest -f Dockerfile .` to build a Docker image
> * `docker system prune -a` to remove old Docker images

The [Docker][] container-based virtualization service lets you run a
minimal CS 161 environment, including a virtual Linux host, on a Mac
OS X or Windows host, without the overhead of a full virtual machine
solution like [VMware Workstation][], [VMware Fusion][], or
[VirtualBox][].

It should be possible to do *all* CS 161 problem sets on CS 61 Docker.
(However, you may prefer to set up a local environent.)

Advantages of Docker:

* Docker can start and stop virtual machines incredibly quickly.
* Docker-based virtual machines are leaner and take less space on your
  machine.
* With Docker, you can easily *edit* your code in your home
  *environment, but compile and run* it on a Linux host.

Disadvantages of Docker:

* Docker does not offer a full graphical environment. You will need to
  run QEMU exclusively in the terminal.
* Docker technology is less user-friendly than virtual machines.
  You’ll have to type weird commands.
* It will run slower.


## Preparing CS 161 Docker

To prepare to build your Docker environment:

1.  Download and install [Docker][].

2.  Clone a copy of the [chickadee repository][].

3.  Change into the `chickadee/docker` directory.

To build your Docker environment, run this command. It will take a couple
minutes. You’ll want to re-run this command every time the Docker image
changes, but later runs should be much faster since they’ll take advantage of
your previous work.

```shellsession
$ docker build -t cs61:latest -f Dockerfile .
```

## Running Docker by script

The Chickadee repository contains a `run-docker` script that
provides good arguments and boots Docker into a view of the current
directory.

For example:

```shellsession
$ cd ~/chickadee
$ ./run-docker
cs61-user@a47f05ea5085:~/chickadee$ echo Hello, Linux
Hello, Linux
cs61-user@a47f05ea5085:~/chickadee$ exit
exit
$ 
```

The script plonks you into a virtual machine! A prompt like
`cs61-user@a47f05ea5085:~$` means that your terminal is connected to the VM.
You can execute any commands you want. To escape from the VM, type Control-D
or run the `exit` command.

The script assumes your Docker container is named `cs61:latest`, as it
was above.


### Running Docker by hand

If you don’t want to use the script, use a command like the following.

```shellsession
$ docker run -it --rm -v ~/chickadee:/home/cs61-user/chickadee cs61:latest
```

Explanation:

* `docker run` tells Docker to start a new virtual machine.
* `-it` says Docker should run interactively (`-i`) using a terminal (`-t`).
* `--rm` says Docker should remove the virtual machine when it is done.
* `-v LOCALDIR:LINUXDUR` says Docker should share a directory between your
  host and the Docker virtual machine. Here, I’ve asked for the host’s
  `~/chickadee` directory to be mapped inside the virtual machine onto the
  `/home/cs61-user/chickadee` directory, which is the virtual machine
  user’s `~/chickadee` directory.
* `cs61:latest` names the Docker image to run (namely, the one you built).

Here’s an example session:

```shellsession
$ docker run -it --rm -v ~/chickadee:/home/cs61-user/chickadee cs61:latest
cs61-user@a15e6c4c8dbe:~$ ls
cs61-lectures
cs61-user@a15e6c4c8dbe:~$ echo "Hello, world"
Hello, world
cs61-user@a15e6c4c8dbe:~$ cs61-docker-version
3
cs61-user@a15e6c4c8dbe:~$ exit
exit
$ 
```

[Docker]: https://docker.com/
[VMware Workstation]: https://www.vmware.com/products/workstation-player.html
[VMware Fusion]: https://www.vmware.com/products/fusion.html
[VirtualBox]: https://www.virtualbox.org/
[Chickadee repository]: https://github.com/cs161/chickadee/
