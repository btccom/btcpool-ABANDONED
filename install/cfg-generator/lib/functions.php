<?php
// Function library for generating configuration files

function errPuts($msg) {
    fputs(STDERR, $msg);
}

function fatal($msg) {
    errPuts("\n\n============== FATAL ==============\n".$msg."\n\n");
    exit(1);
}

function finish($msg) {
    errPuts("\n\n============== FINISH ==============\n".$msg."\n\n");
    exit(0);
}

function notNull($envName) {
    if (!isset($_ENV[$envName])) {
        fatal("Environmental variable '$envName' must be defined");
    }
    if (empty($_ENV[$envName])) {
        fatal("Environmental variable '$envName' should not be empty");
    }
    return $_ENV[$envName];
}

function notNullTrim($envName) {
    if (!isset($_ENV[$envName])) {
        fatal("Environmental variable '$envName' must be defined");
    }
    $value = trim($_ENV[$envName]);
    if (empty($value)) {
        fatal("Environmental variable '$envName' should not be empty");
    }
    return $value;
}

function optional($envName, $defaultValue = '') {
    if (!isset($_ENV[$envName])) {
        return $defaultValue;
    }
    if (empty($_ENV[$envName])) {
        return $defaultValue;
    }
    return $_ENV[$envName];
}

function optionalTrim($envName, $defaultValue = '') {
    if (!isset($_ENV[$envName])) {
        return $defaultValue;
    }
    $value = trim($_ENV[$envName]);
    if (empty($value)) {
        return $defaultValue;
    }
    return $value;
}

function isTrue($envName) {
    $falseTags = ['false', 'null', 'nil', 'undefined'];

    if (!isset($_ENV[$envName])) {
        return false;
    }
    $value = strtolower(trim($_ENV[$envName]));
    if (in_array($value, $falseTags)) {
        return false;
    }
    return (bool)$value;
}

function toString($obj) {
    return json_encode($obj, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
}
