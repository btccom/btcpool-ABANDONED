<?php
require_once __DIR__.'/functions.php';
set_error_handler('exitOnWarning');

if ($argc < 2) {
    exitln("Usage:\n$argc[0] <json-lines-file>");
}

$path = $argv[1];
$lines = file($path);

$solvedShares = [];
foreach ($lines as $line) {
    $line = trim($line);
    if (empty($line)) {
        continue;
    }

    $obj = json_decode($line, true);

    if (!is_array($obj)) {
        exitln("Wrong testdata, json decode fail: $line");
    }

    $solvedShares[$obj['input']] = $line;
}

$goodJobObj = json_decode(
<<<EOF
{
	"jobId": 6729403906683568128,
	"chain": "BEAM",
	"height": 147,
	"blockBits": "03d95105",
	"input": "4f72019ae0d44ab8a1e7863742b8a363055d2fddc7d22605f9a91fa36b2fbbdf",
	"rpcAddress": "beam-node-simulator:9808",
	"rpcUserPwd": "12345678"
}
EOF
);

for (;;) {
    $line = fgets(STDIN);
    if (feof(STDIN)) {
        break;
    }
    
    $jobObj = json_decode($line);
    if (!is_object($jobObj)) {
        exitln("Wrong job, json decode fail: $line");
    }

    checkObject($jobObj, $goodJobObj);
    $input = $jobObj->input;

    if (!isset($solvedShares[$input])) {
        exitln("Unexpected job, cannot find input $input from testdata. Job json: $line");
    }

    echo $solvedShares[$input], "\n";
}
