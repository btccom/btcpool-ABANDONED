<?php
require_once __DIR__.'/functions.php';
set_error_handler('exitOnWarning');

if ($argc < 3) {
    exitln("Usage:\n$argc[0] <json-lines-file> <stratum-server-host:port>");
}

$running = true;

$path = $argv[1];
$host = $argv[2];

$testData = parseStratumLines($path);
//echo json_encode($testData);die;

$fp = stream_socket_client($host, $errno, $errstr, 5);
if (!$fp) {
    exitln("Cannot connect to $host: $errstr");
}

writeToServer($fp, json_encode($testData['mining.subscribe'])."\n".json_encode($testData['mining.authorize'])."\n");
$requestNum = 2;
$responseNum = 0;

for ($readLines=0; $readLines<120; $readLines++) {
    $line = fgets($fp);
    $line = trim($line);
    $data = json_decode($line, true);
    $id = $data['id'];

    if (isset($data['method'])) {
        $method = $data['method'];
        switch ($method) {
        case 'mining.notify':
            $jobId = $data['params'][0];
            $hash = computeJobHash($data);
            
            echo "[INFO] New notify $hash from server\n";
            if (!isset($testData['notify'][$hash])) {
                continue;
            }

            if (!empty($testData['submit'][$hash])) {
                $jsonLines = '';
                foreach ($testData['submit'][$hash] as $submit) {
                    $submitData = $testData['request'][$submit['id']]['request'];
                    $submitData['params'][1] = $jobId;
                    $jsonLines .= json_encode($submitData)."\n";
                }
                writeToServer($fp, $jsonLines);
                $shareNum = count($testData['submit'][$hash]);
                echo "[INFO] Submit ", $shareNum, " shares to server\n";
                $requestNum += $shareNum;
                unset($testData['submit'][$hash]);
            }
        }
        continue;
    }

    if (!isset($testData['request'][$id])) {
        exitln("Cannot find the request $id from testData of the response $line");
    }

    $request = $testData['request'][$id];
    if ($request['response']['result'] != $data['result']) {
        exitln("Unexpected response of request '$id': ".json_encode($request['request'])."\nIt should be: ".json_encode($request['response'])."\nBut got this: $line");
    }

    $responseNum++;

    if (empty($testData['submit']) && ($requestNum == $responseNum)) {
        echo "[INFO] Read $readLines lines from server\n";
        break;
    }
}

if (!empty($testData['submit'])) {
    exitln("Missing 'mining.notify' of these submissions: ".json_encode($testData['submit']));
}

if ($requestNum != $responseNum) {
    exitln("Request number ($requestNum) != Response number ($responseNum)");
}

echo "[INFO] Sent $requestNum requests to server and got $responseNum responses\n";
echo "[INFO] All checking past\n";

fclose($fp);

//----------------------------------------------

function writeToServer($fp, $json) {
    $jsonLen = strlen($json);
    $len = fwrite($fp, $json);

    if ($len != $jsonLen) {
        exitln("Short write to server, should write $jsonLen bytes but only $len bytes be written");
    }
}

function parseStratumLines($path) {
    $result = [
        'request' => [],
        'notify' => [],
        'submit' => [],
    ];

    $notifyCache = [];

    $lines = file($path);
    if (empty($lines)) {
        exitln("Cannot load $file");
    }

    foreach ($lines as $line) {
        $line = trim($line);
        $data = json_decode($line, true);
        $id = $data['id'];

        if (isset($data['method'])) {
            $method = $data['method'];

            if ($id !== null) {
                $result['request'][$id] = [
                    'method' => $method,
                    'request' => $data,
                ];
            }

            switch ($method) {
            case 'mining.subscribe':
                $result[$method] = $data;
                break;
            case 'mining.authorize':
                $result[$method] = $data;
                $result['usr'] = $data['params'][0];
                $result['pwd'] = $data['params'][1];
                break;
            case 'mining.notify':
                $jobId = $data['params'][0];
                $hash = computeJobHash($data);
                $result['notify'][$hash] = [
                    'job' => $data,
                    'job_id' => $jobId,
                ];
                $notifyCache[$jobId] = $hash;
                break;
            case 'mining.submit':
                $jobId = $data['params'][1];
                if (!isset($notifyCache[$jobId])) {
                    exitln("Wrong test data, cannot find stratum job of the submit $line");
                }
                $jobHash = $notifyCache[$jobId];

                if (!isset($result['submit'][$hash])) {
                    $result['submit'][$hash] = [];
                }

                $result['submit'][$hash][] = [
                    'id' => $id,
                    'job_id' => $jobId,
                ];
                break;
            }
            continue;
        }

        if (!isset($result['request'][$id])) {
            exitln("Wrong test data, cannot find request of the response $line");
        }

        if (isset($result['request'][$id]['response'])) {
            exitln("Wrong test data, duplicated response $line of the request $id");
        }

        $result['request'][$id]['response'] = $data;
    }

    if (!isset($result['mining.subscribe']) || !isset($result['mining.authorize'])) {
        exitln("Wrong test data, missing mining.subscribe or mining.authorize");
    }

    return $result;
}

function computeJobHash($data) {
    $params = $data['params'];
    $text = implode ('|', [$params[1], $params[2], $params[3], $params[5], $params[6], $params[7]]);
    $text .= '|'.implode(',', $params[4]);
    return sha1($text);
}
