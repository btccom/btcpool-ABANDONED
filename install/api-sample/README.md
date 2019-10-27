Reference implementation of BTCPool dependent Web API
==================



`users.list_id_api_url` in [sserver.cfg](../src/sserver/sserver.cfg)
------------------

### Static User List

[userlist.php](userlist.php)


### User List with Automatic Registration Enabled

[userlist-autoreg.php](userlist-autoreg.php)

#### `UserAutoRegAPI.URL` in [Init User Coin](https://github.com/btccom/btcpool-go-modules/tree/master/initUserCoin)

It is used with the `userlist-autoreg.php` above.

[autoreg.php](autoreg.php)


`UserCoinMapURL` in [Switcher API Server](https://github.com/btccom/btcpool-go-modules/tree/master/switcherAPIServer)
------------------

[usercoin.php](usercoin.php)



How to quickly create a web server based on the script?
------------------

You can use `PHP-CLI` to quickly create a web server. Suppose you are using Ubuntu or Debian:
```
mkdir -p /var/www/html
cp *.php /var/www/html
apt install php-cli
php -S localhost:8000 -t /var/www/html
```

Then you can set `list_id_api_url ="http://localhost:8000/userlist.php"`.

Note: the performance of PHP-CLI's web server is very poor, don't use it in a production environment.
