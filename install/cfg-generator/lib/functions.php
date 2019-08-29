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

function mayOptionalTrim($isOptional, $envName, $defaultValue = '') {
    $isOptional = valueIsTrue($isOptional);
    if ($isOptional) {
        return optionalTrim($envName, $defaultValue);
    }
    return notNullTrim($envName);
}

function valueIsTrue($value) {
    $falseTags = ['false', 'null', 'nil', 'undefined'];

    if (is_bool($value)) {
        return $value;
    }
    
    $value = strtolower(trim($value));
    if (in_array($value, $falseTags)) {
        return false;
    }
    return (bool)$value;
}

function isTrue($envName) {
    if (!isset($_ENV[$envName])) {
        return false;
    }
    return valueIsTrue($_ENV[$envName]);
}

function optionalBool($envName, $defaultValue, &$returnValue = null) {
    if (!isset($_ENV[$envName]) || empty(trim($_ENV[$envName]))) {
        return toString($returnValue = $defaultValue);
    }
    return toString($returnValue = isTrue($envName));
}

function notEmpty($var) {
    return !empty($var);
}

function toString($obj) {
    return json_encode($obj, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
}

function toJSON($obj) {
    return json_encode($obj, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
}

function removeEmptyItem($array) {
    foreach (array_keys($array) as $key) {
        if (empty($array[$key])) {
            unset($array[$key]);
        }
    }
    return $array;
}

function commaSplit($envName, $isOptional = true, $defaultValue = '') {
    $values = $isOptional ? optional($envName) : notNull($envName);
    if ($values == '') {
        return [];
    }
    return explode(',', $values);
}

function commaSplitTrim($envName, $isOptional = true, $defaultValue = '') {
    $result = explode(',', $isOptional ? optionalTrim($envName) : notNullTrim($envName));
    foreach ($result as &$item) {
        $item = trim($item);
    }
    return removeEmptyItem($result);
}

function isIntString($value) {
    $intValue = (int)$value;
    return "$intValue" === "$value";
}

// is $value = pow($base, N)?
function isPowerOf($value, $base = 2) {
    return isIntString(log($value, $base));
}

function powerOfOptionalTrim($envName, $defaultValue, $base = 2) {
    $value = optionalTrim($envName, $defaultValue);
    if (!isPowerOf($value, $base)) {
        fatal("Wrong value, '$envName' should be a power of $base, but it is $value");
    }
    return $value;
}

function powerOfMayOptionalTrim($isOptional, $envName, $defaultValue, $base = 2) {
    $isOptional = valueIsTrue($isOptional);
    $value = $isOptional ? optionalTrim($envName, $defaultValue) : notNullTrim($envName);
    if (!isPowerOf($value, $base)) {
        fatal("Wrong value, '$envName' should be a power of $base, but it is $value");
    }
    return $value;
}

function hexPowerOfOptionalTrim($envName, $defaultValue, $base = 2) {
    $value = hexdec(optionalTrim($envName, $defaultValue));
    $hex = dechex($value);
    if (!isPowerOf($value, $base)) {
        fatal("Wrong value, '$envName' should be a power of $base, but it is $value (0x$hex)");
    }
    return $hex;
}

function hexPowerOfMayOptionalTrim($isOptional, $envName, $defaultValue, $base = 2) {
    $isOptional = valueIsTrue($isOptional);
    $value = hexdec($isOptional ? optionalTrim($envName, $defaultValue) : notNullTrim($envName));
    $hex = dechex($value);
    if (!isPowerOf($value, $base)) {
        fatal("Wrong value, '$envName' should be a power of $base, but it is $value (0x$hex)");
    }
    return $hex;
}

function separator($outputValue, $array, $currentKey) {
    return ($currentKey != array_key_last($array)) ? $outputValue : '';
}
