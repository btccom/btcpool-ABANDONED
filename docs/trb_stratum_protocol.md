## TRB MINING STRATUM

### mining.subscribe

- params: ["agent", null]
- result: [null, "nonce1"]

```json
request:
{
	"id": 1,
	"method": "mining.subscribe",
	"params": ["tellorminer-v1.0.0", null]
}

response:
{
	"id": 1,
	"result": [null, "01000000"],
	"error": null
}
```

nonce1 is first part of prefix nonce (in hex).

The length of nonce2 is not fixed , the miner can choose any length value behind nonce1, nonce2 can be 4 bytes, 12 bytes ... etc.
note: Recommended length value of nonce2 is 12 bytes


### mining.authorize

- params: ["username", "password"]
- result: true

```json
{
	"id": 2,
	"method": "mining.authorize",
	"params": ["username.worker1", "x"]
}

{"id":2,"result":true,"error":null}
```

### mining.notify

- params: ["jobId", "challenge", "publicaddress", difficulty, cleanJob]

```json
{
    "id":null,
    "method":"mining.notify",
    "params":[
        "5e70434f00000001",
        "967eb639f271b23105f203244adfa6a7c13c043269b01bca1225af117e83d6a1",
        "6da6b49ca093d1bd52ffb030c5c1d7e72149271b",
        6424980212,
        true
    ]
}
```
note : If the challenge value is "0000000000000000000000000000000000000000000000000000000000000000",it means that the job is an invalid job, and the miner should wait for the next job.

### mining.submit

- params: [ "username.workername", "jobId", "nonce2" ]
- result: true / false

```json
{
    "method":"mining.submit",
    "params":[
        "MPTest.014012",
        "5e70434f00000001",
        "000000001b4eaf0042ae5204"
    ],
    "id":2
}

{"id":2,"result":true,"error":null}    // accepted share response
{"id":2,"result":false,"error":[23,"low difficulty",null]}  // rejected share response
{"id":2,"result":false,"error":[36,"Job not found",null]}  // rejected share response

```
note :
```nonce = nonce1+nonce2```
```powhash = Sha256(cripemd160(keccak256(challenge + publicaddress + nonce)))```

example :
```
string nonce = "01000000" + "000000001b4eaf0042ae5204" = "01000000000000001b4eaf0042ae5204";
string input = challenge + publicaddress + nonce ="967eb639f271b23105f203244adfa6a7c13c043269b01bca1225af117e83d6a1" + "6da6b49ca093d1bd52ffb030c5c1d7e72149271b" + "01000000000000001b4eaf0042ae5204"
uint256 powhash = Sha256(cripemd160(keccak256(challenge + publicaddress + nonce))) = Sha256(cripemd160(keccak256(input))) = “817fbcc118de22d2be10968a054b6464b51a2948d03326836d6ff9b2523e31c0”
powhash (817fbcc118de22d2be10968a054b6464b51a2948d03326836d6ff9b2523e31c0) % jobdiff(6424980212) = 0

```