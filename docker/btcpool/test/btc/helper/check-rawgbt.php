#!/usr/bin/env php
<?php
require_once __DIR__.'/functions.php';
set_error_handler('exitOnWarning');

if ($argc < 2) {
    exitln("Usage:\n$argc[0] <json-lines-file>");
}

$path = $argv[1];
$lines = file($path);

$rightObj = json_decode(
<<<'EOF'
{
	"created_at_ts": 1555485705,
	"block_template_base64": "eyJyZXN1bHQiOnsiY2FwYWJpbGl0aWVzIjpbInByb3Bvc2FsIl0sInZlcnNpb24iOjUzNjg3MDkxMiwicnVsZXMiOlsic2Vnd2l0Il0sInZiYXZhaWxhYmxlIjp7fSwidmJyZXF1aXJlZCI6MCwicHJldmlvdXNibG9ja2hhc2giOiI2MDJmOGQ2OTRjY2Y5YzRhM2I2ZDhlMDE2NzE5MDU4OWRlMmE0MTI4NTNlYWI2ZDQ5NThlNmY4ZmIwNDA1ODNjIiwidHJhbnNhY3Rpb25zIjpbXSwiY29pbmJhc2VhdXgiOnsiZmxhZ3MiOiIifSwiY29pbmJhc2V2YWx1ZSI6NTAwMDAwMDAwMCwibG9uZ3BvbGxpZCI6IjYwMmY4ZDY5NGNjZjljNGEzYjZkOGUwMTY3MTkwNTg5ZGUyYTQxMjg1M2VhYjZkNDk1OGU2ZjhmYjA0MDU4M2M4IiwidGFyZ2V0IjoiN2ZmZmZmMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMCIsIm1pbnRpbWUiOjE1NTU0ODU2OTYsIm11dGFibGUiOlsidGltZSIsInRyYW5zYWN0aW9ucyIsInByZXZibG9jayJdLCJub25jZXJhbmdlIjoiMDAwMDAwMDBmZmZmZmZmZiIsInNpZ29wbGltaXQiOjgwMDAwLCJzaXplbGltaXQiOjQwMDAwMDAsIndlaWdodGxpbWl0Ijo0MDAwMDAwLCJjdXJ0aW1lIjoxNTU1NDg1NzA1LCJiaXRzIjoiMjA3ZmZmZmYiLCJoZWlnaHQiOjYsImRlZmF1bHRfd2l0bmVzc19jb21taXRtZW50IjoiNmEyNGFhMjFhOWVkZTJmNjFjM2Y3MWQxZGVmZDNmYTk5OWRmYTM2OTUzNzU1YzY5MDY4OTc5OTk2MmI0OGJlYmQ4MzY5NzRlOGNmOSJ9LCJlcnJvciI6bnVsbCwiaWQiOiIxIn0K",
	"gbthash": "237c410d963dd50fa72ef1de54806217380e5778901e4fceeb318cf447ba2aa4"
}
EOF
);

$rightBlockTemplate = json_decode(base64_decode($rightObj->block_template_base64));

foreach ($lines as $line) {
    $obj = json_decode($line);
    if (FALSE === $obj) {
        exitln("Wrong JSON: $line");
    }

    try {
        checkObject($obj, $rightObj);
    } catch (Exception $ex) {
        exitln($ex->getMessage()."\ngbt: $line");
    }

    $blockTemplate = base64_decode($obj->block_template_base64);
    if (FALSE === $obj) {
        exitln("Wrong gbt field block_template_base64, base64_decode failed.\ngbt: $line");
    }
    $blockTemplate = json_decode($blockTemplate);
    if (FALSE === $blockTemplate) {
        exitln("Wrong gbt field block_template_base64, json_decode failed.\ngbt: $line");
    }

    try {
        checkObject($blockTemplate, $rightBlockTemplate);
    } catch (Exception $ex) {
        exitln("Wrong gbt field block_template_base64, ".$ex->getMessage()."\ngbt: $line");
    }
}

exitln("All RawGbts are OK.", 0);
