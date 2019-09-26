<?php
// PHP library for testcase

function exitOnWarning($errNo, $errStr, $errFile, $errLine) {
    $msg = "$errStr in $errFile on line $errLine";
    throw new ErrorException($msg, $errNo);
}

function exitln($line, $exitCode = 1) {
    echo $line, "\n";
    exit($exitCode);
}

function echoln($line) {
    echo $line, "\n";
}

function checkObject($obj, $rightObj, $parentKeys = []) {
    $type = gettype($obj);
    $rightType = gettype($rightObj);
    if ($type !== $rightType) {
        throw new Exception("wrong object type $type, should be $rightType");
    }

    if (is_object($rightObj)) {
        $getValue = function ($obj, $key) {
            return $obj->$key;
        };
        $isset = function ($obj, $key) {
            return isset($obj->$key);
        };
    } elseif (is_array($rightObj)) {
        $getValue = function ($obj, $key) {
            return $obj[$key];
        };
        $isset = function ($obj, $key) {
            return isset($obj[$key]);
        };
    } else {
        // $obj and $rightObj are of the same type
        return true;
    }

    foreach ($rightObj as $key => $rightValue) {
        $keyNodes = $parentKeys;
        $keyNodes[] = $key;
        $keyPath = implode('.', $keyNodes);

        if (!$isset($obj, $key)) {
            // isset() return FALSE if value is NULL.
            if (is_null($rightValue) && is_null($getValue($obj, $key))) {
                continue;
            }
            
            throw new Exception("object missing field: $keyPath");
        }

        $value = $getValue($obj, $key);
        $type = gettype($value);
        $rightType = gettype($rightValue);

        if (is_object($rightValue)) {
            if (!is_object($value)) {
                throw new Exception("wrong type $type of object field: $keyPath, should be object");
            }
            checkObject($value, $rightValue, $keyNodes);
            continue;
        }
        if (is_array($rightValue)) {
            if (!is_array($value)) {
                throw new Exception("wrong type $type of object field: $keyPath, should be array");
            }
            checkObject($value, $rightValue, $keyNodes);
            continue;
        }
        if ($type !== $rightType) {
            throw new Exception("wrong type $type of object field: $keyPath, should be $rightType");
        }
    }

    return true;
}
