Install Supervisor
==================

Supervisor is a client/server system that allows its users to monitor and control a number of processes on UNIX operating systems.

**install Supervisor**

```
apt-get install supervisor
```

**setup Supervisor for a certain process**

For each process a configuration file (for example. process.conf) must be created in the following path

`/etc/supervisor/conf.d/process.conf`:

with the following content

```
[program:process]
directory=/work/process
command=/work/process/bin/process-start.sh /work/process/config/process.properties
autostart=true
autorestart=true
startsecs=6
startretries=20
```

**start process**

```
$ supervisorctl
> reread
> update
> status

or 
> start/stop process
```
