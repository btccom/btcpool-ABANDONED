FROM php:7.3-cli
RUN apt-get update && apt-get install -y libevent-dev libssl-dev && \
    docker-php-ext-install -j$(nproc) sockets && \
    pecl install event && \
    docker-php-ext-enable event && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*
ADD . /work/script
ENTRYPOINT ["php", "/work/script/server.php"]
