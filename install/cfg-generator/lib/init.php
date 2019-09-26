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


// For PHP <= 7.3.0 :
if (! function_exists("array_key_last")) {
    function array_key_last($array) {
        if (!is_array($array) || empty($array)) {
            return NULL;
        }
       
        return array_keys($array)[count($array)-1];
    }
}
