<?php
require_once __DIR__.'/functions.php';

// A class to automatically load environment variable,
// fix $_ENV is empty due to php.ini
class EnvAutoLoader implements ArrayAccess {
    /* interface ArrayAccess */
    public function offsetExists($name) {
        return getenv($name) !== FALSE;
    }
    public function offsetGet($name) {
        return getenv($name);
    }
    public function offsetSet($name, $value) {
        throw new Exception('Not implemented');
    }
    public function offsetUnset($name) {
        throw new Exception('Not implemented');
    }
}

$_ENV = new EnvAutoLoader();
