Decrypt TLS via a Proxy
==================

* Decrypt TLS via the man-in-the-middle attack.

### build

```bash
mkdir build
cd build
cmake ..
make
```

### run

```bash
cp ../tls_decrpyt_proxy.cfg .

openssl genrsa -out proxy.key 2048
openssl req -new -key proxy.key -out proxy.crt.req
openssl x509 -req -days 365 -in proxy.crt.req -signkey proxy.key -out proxy.crt

./tls_decrypt_proxy -c tls_decrpyt_proxy.cfg
./tls_decrypt_proxy -c tls_decrpyt_proxy.cfg -l stderr

mkdir log
./tls_decrypt_proxy -c tls_decrpyt_proxy.cfg -l log
```
