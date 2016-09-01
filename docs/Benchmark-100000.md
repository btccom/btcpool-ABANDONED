Benchmark
===========

* Stratum TCP Connections: `100,000`
* servers
  * 2 stratum server
  * 1 common server
  * 3 Kafka server
  * 1 MySQL(Aliyun RDS)

### Stratum Server

* CPU: 2 Cores, Intel(R) Xeon(R) CPU E5-2650 v2 @ 2.60GHz
* Memory: 2 GBytes
  * `free`: `1,054 MB`
* `50,000` connections for each stratum server

**top**

```
top - 19:58:51 up 14 days,  5:17,  1 user,  load average: 0.37, 0.36, 0.33
Tasks:   1 total,   0 running,   1 sleeping,   0 stopped,   0 zombie
%Cpu(s): 17.3 us,  4.4 sy,  0.0 ni, 74.7 id,  0.2 wa,  0.0 hi,  3.2 si,  0.2 st
KiB Mem:   2048532 total,  1257732 used,   790800 free,    31436 buffers
KiB Swap:        0 total,        0 used,        0 free.   256260 cached Mem

  PID USER      PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
21332 root      20   0 1726.0m 528.3m   5.3m S  49.0 26.4  25:56.32 sserver
```

**nload**

```
Device eth0 [xxx.xxx.xxx.xxx] (1/3):
================================================================================================
Incoming:
# ##             #####               ##### #                 #### |#
# ##             #####               ##### #                 #### ##
# ##             #####               #####.#                .#### ##
# ##             #####               #######                ##### ##
#.##             #####|              #######                #########
####             ######              #######                #########    .
####             ######              #######                #########    #  Curr: 5.00 MBit/s
####            #######              #######                #########    #  Avg: 7.82 MBit/s
####            #######              ########               #########    #  Min: 113.14 kBit/s
####            #######              ########               #########    #  Max: 41.50 MBit/s
####.           #######.             ########.              #########    #  Ttl: 7.90 GByte
Outgoing:






                                          .                                 Curr: 84.36 kBit/s
                    ||                 #  #                    .|           Avg: 900.41 kBit/s
                    ##                .#  #|                  .##           Min: 52.18 kBit/s
|||              #####|               ##|###                  ####||        Max: 4.13 MBit/s
#####            ######||            .###### ||              |######..      Ttl: 2.18 GByte
```

---

### Common Server

* CPU: 4 Cores, Intel(R) Xeon(R) CPU E5-2650 v2 @ 2.60GHz
* Memory: 8 GBytes
  * `free`: `6,743 MB`

**top**

```
top - 20:05:19 up 14 days,  5:42,  1 user,  load average: 0.29, 0.24, 0.23
Tasks: 121 total,   2 running, 119 sleeping,   0 stopped,   0 zombie
%Cpu(s):  1.7 us,  1.0 sy,  0.0 ni, 97.1 id,  0.0 wa,  0.0 hi,  0.2 si,  0.0 st
KiB Mem:   8176676 total,  5563316 used,  2613360 free,   377480 buffers
KiB Swap:        0 total,        0 used,        0 free.  3899504 cached Mem

  PID USER      PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
18215 root      20   0  877.7m 373.1m   4.2m S   3.3  4.7   7:49.93 statshttpd
18189 root      20   0 1832.8m 220.6m   4.6m S   3.3  2.8   9:38.25 blkmaker
19814 root      20   0  299.3m 123.6m   3.9m S   1.3  1.5   3:20.79 slparser
18191 root      20   0  784.7m  91.4m   3.7m S   1.3  1.1   4:35.30 jobmaker
19410 root      20   0  620.9m   8.3m   4.0m S   0.3  0.1   2:02.55 gbtmaker
18208 root      20   0  414.6m   7.2m   3.6m S   1.7  0.1   1:35.13 sharelogger
18202 root      20   0  406.4m   4.9m   3.8m S   0.3  0.1   0:36.61 poolwatcher
```

**nload**

```
Device eth0 [xxx.xxx.xxx.xxx] (1/3):
============================================================================================================
Incoming:
              ###  ##              ##  ##  |           ##  ##   #.           ##  ##
  ||          ### ###  # #         ##  ## .#..         ##  ##  .##           ##  ## .  Curr: 7.72 MBit/s
  ##|         ### ### ####         ##  ## ####         ##  ##  ###           ##  ## #  Avg: 11.35 MBit/s
 |###.#       ### ### ####|        ##  ## ####.        ##  ########          ##  ####  Min: 404.81 kBit/s
 #######      ### #########        ##  ##.#####        ##  ########||        ##  ####  Max: 102.76 MBit/s
|#######......###.#########........##..#########.......##..##########........##..####  Ttl: 1023.91 GByte
Outgoing:
               ##    ##      ##   ###                ######              ##  ###
               ##    ##      ##   ###                ######              ##  ###
               ##    ##      ##   ###                ######              ##  ###       Curr: 871.22 kBit/s
               ##    ##      ##   ###                ######              ##  ###       Avg: 10.83 MBit/s
               ##    ##      ##   ###                ######              ##  ###       Min: 320.68 kBit/s
               ##    ##      ##   ###                ######              ##  ###       Max: 150.60 MBit/s
..............|##..||##......##...###..||............######||............##..###.||..  Ttl: 513.43 GByte
```

---

### Kafka Server

* CPU: 2 Cores, Intel(R) Xeon(R) CPU E5-2650 v2 @ 2.60GHz
* Memory: 4 GBytes
  * `free`: `2,450 MB`

**top**

```
top - 20:15:26 up 14 days,  6:11,  1 user,  load average: 0.32, 0.30, 0.30
Tasks:   1 total,   0 running,   1 sleeping,   0 stopped,   0 zombie
%Cpu(s):  0.2 us,  4.2 sy,  4.5 ni, 90.3 id,  0.0 wa,  0.0 hi,  0.8 si,  0.0 st
KiB Mem:   4048124 total,  3915532 used,   132592 free,     6532 buffers
KiB Swap:        0 total,        0 used,        0 free.  2371136 cached Mem

  PID USER      PR  NI    VIRT    RES    SHR S  %CPU %MEM     TIME+ COMMAND
15783 root      25   5 3132.1m 1.237g   4.5m S  18.0 32.0   3825:23 java
```

**nload**

```
Device eth0 [xxx.xxx.xxx.xxx] (1/3):
======================================================================================
Incoming:
       ####                 ###                 ####
       ####                 ###                 ####
       ####                 ###                 ####
      |####                |###                 ####
      #####                ####               . ####            Curr: 1.47 MBit/s
     |#####               .####|              #|####            Avg: 6.18 MBit/s
     ######.|             ######              ######            Min: 907.76 kBit/s
.....########.............######||..... .....|######......  ..  Max: 71.38 MBit/s
##############################################################  Ttl: 451.25 GByte
Outgoing:
       ####                 ####                ####
       ####                 ####                ####
       ####                 ####                ####
       ####                 ####                ####
       ####                 ####                ####
       ####                 ####                ####            Curr: 1.79 MBit/s
       ####                 ####                ####            Avg: 15.49 MBit/s
       ####                 ####                ####            Min: 1.10 MBit/s
|||||######.|||||||||||||||#####||||||||||||||#|####.|||||||||  Max: 144.01 MBit/s
##############################################################  Ttl: 917.83 GByte
```

---

### MySQL(Aliyun RDS)

* Server Info:
  * MySQL 5.6
  * CPU: 4 Cores
  * Max MEM: 8192 MB
  * Max IOPS: 5000

* Usage 
  * CPU: 17 %
  * MEM: 86 %
  * IOPS: 60/seconds
