FROM ubuntu:18.04

ENV HOME /root
ENV TERM xterm

ARG APT_MIRROR_URL

COPY ./update_apt_sources.sh /tmp
RUN /tmp/update_apt_sources.sh

# install packages
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y apache2 libapache2-mod-php7.2 php-curl && \
    a2enmod php7.2 && \
    apt-get autoremove && \
    apt-get autoclean && \
    rm -rf /work/package /var/lib/apt/lists/*

# add DNS
RUN echo nameserver 114.114.114.114 >> /etc/resolv.conf && \
    echo nameserver 223.5.5.5       >> /etc/resolv.conf && \
    echo nameserver 8.8.8.8         >> /etc/resolv.conf

COPY ./public /var/www/html
RUN ls -lh /var/www/html/

ENTRYPOINT ["apache2ctl", "-DFOREGROUND"]
