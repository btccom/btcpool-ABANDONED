Docker for RSK Node
============================

* OS: `Ubuntu 16.04 LTS`
* Docker image: build by yourself
* RSK version: latest

## Install Docker CE

```
# Use official mirrors
curl -fsSL https://get.docker.com | bash -s docker

# Or use Aliyun mirrors
curl -fsSL https://get.docker.com | bash -s docker --mirror Aliyun
```

## Change Image Mirrors and Data Root (optional)

```
mkdir -p /work/docker
vim /etc/docker/daemon.json
```

```
{
    "registry-mirrors": ["https://<REPLACED-TO-YOUR-MIRROR-ID>.mirror.aliyuncs.com"],
    "data-root": "/work/docker"
}
```

```
service docker restart
```

## Build Docker Image

```
git clone https://github.com/rsksmart/artifacts.git
cd artifacts/Dockerfiles/RSK-Node
docker build -t rsk-mainnet -f Dockerfile.MainNet .
```

## Create Config Files

```
mkdir /work/rsk
mkdir /work/rsk/etc
mkdir /work/rsk/lib
mkdir /work/rsk/log

vim /work/rsk/etc/node.conf
vim /work/rsk/etc/logback.xml
```

### Configure file template:
[node.conf](./node.conf)
You need to replace `<your-private-key>`, `<your IP out of docker or domain>` and `<your reward address>` in the configure file.

[logback.xml](./logback.xml)
You can use the file directly or modify it as needed.

#### How to get a private key that `node.conf` required?

You can follow [this article](https://github.com/rsksmart/rskj/wiki/Get-an-RSK-account) or run the following command:

```
docker run --name rsk-gen-key -it rsk-mainnet java -cp /usr/share/rsk/rsk.jar co.rsk.GenNodeKeyId
docker rm rsk-gen-key
```

## Start Docker Container

```
# start docker
docker run -it -v /work/rsk/etc:/etc/rsk -v /work/rsk/lib:/var/lib/rsk -v /work/rsk/log:/var/log/rsk --name rsk-mainnet -p 4444:4444 -p 5050:5050 --restart always -d rsk-mainnet

# see the log
tail -F /work/rsk/log/rsk.log

# login
docker exec -it rsk-mainnet /bin/bash
```

## Update RSK

```
# pull new version
cd /work/rsk-artifacts
git pull

# build without cache
cd /work/rsk-artifacts/Dockerfiles/RSK-Node
docker build --no-cache -t rsk-mainnet -f Dockerfile.MainNet .

# stop and remove the old container
docker stop rsk-mainnet
docker rm rsk-mainnet

# start a new container
docker run -it -v /work/rsk/etc:/etc/rsk -v /work/rsk/lib:/var/lib/rsk -v /work/rsk/log:/var/log/rsk --name rsk-mainnet -p 4444:4444 -p 5050:5050 --restart always -d rsk-mainnet

# see the log
tail -F /work/rsk/log/rsk.log
```
