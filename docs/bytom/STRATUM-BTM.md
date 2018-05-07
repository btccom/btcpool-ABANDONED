# Stratum Mining Protocol

This is the description of stratum protocol used in this pool.

Stratum defines simple exception handling. Example of rejected share looks like:

```javascript
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: 23, message: "Invalid share" } }
```

Each response with exception is followed by disconnect.

## Authentication

Request looks like:

```javascript
{
  "id": 1,
  "jsonrpc": "2.0",
  "method": "login",
  "params": {
    "0xb85150eb365e7df0941f0cf08235f987ba91506a",//login
    "",//Pass
    "agent"//Agent
  }
}
```

Request can include additional 2nd param (email for example):

```javascript
{
  "id": 1,
  "jsonrpc": "2.0",
  "method": "login",
  "params": {
     "antminer",//login
     "001",//Pass
     "agent"//Agent
  }
}
```

Successful response:

```javascript
{
"id": 1,
"jsonrpc": "2.0",
"result": {
"id": "antminer_1",
    "job": {
        "version": "0100000000000000",
        "height": "552b000000000000",
        "previous_block_hash": "a381c915148d1374e106533822cb55b92a0676577909964af53204150e473108",
        "timestamp": "a846d95a00000000",
        "transactions_merkle_root": "542fb09c8f827e2bbcbf6612f64ccb83d84cc762c243d3366626443b893c2f49",
        "transaction_status_hash": "6978a65b4ee5b6f4914fe5c05000459a803ecf59132604e5d334d64249c5e50a",
        "nonce": "00000000a6040000",
        "bits": "a30baa000000001d",
        "job_id": "710425",
        "seed": "2947a722c7af35bca92dc612ed29a8f61cf59734b214a4994dcce2521220e4bc",
        "target": "c5a70000"
    },
    "status": "OK"
},
"error": null
}
```

Exceptions:

```javascript
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: -1, message: "Invalid login" } }
```

## Request For Job

Request looks like:

```javascript
{ "id": 1, "jsonrpc": "2.0", "method": "getwork" }
```

Successful response:

```javascript
{
  "id": 1,
  "jsonrpc": "2.0",
  "method": "job",
  "params": {
     "1",//JobId
     "1"//Version
     "1" //Height
     "e733c4b1c4ea57bc87346d9fce8c492248f1f414b9eac17faf9e9b8e0a107fa1", //PreviousBlockHash
     "5aa39c6e", //Timestamp
     "15bd7762b3ee8057ecb83b792e2168c6b6bddaf10163d110f7e63db387e6aacf", //TransactionsMerkleRoot
     "53c0ab896cb7a3778cc1d35a271264d991792b7c44f5c334116bb0786dbc5635", //TransactionStatusHash
     "8000000000000000", //Nonce
     "20000000007fffff", //Bits
     "e733c4b1c4ea57bc87346d9fce8c492248f1f414b9eac17faf9e9b8e0a107fa1",//Seed
     "bdba0400",//Target
    }
}
```

Exceptions:

```javascript
{ "id": 10, "result": null, "error": { code: 0, message: "Work not ready" } }
```

## New Job Notification

Server sends job to peers if new job is available:

```javascript
{
  "jsonrpc": "2.0",
  "params": {
       "1",//JobId
       "1"//Version
       "1" //Height
       "e733c4b1c4ea57bc87346d9fce8c492248f1f414b9eac17faf9e9b8e0a107fa1", //PreviousBlockHash
       "5aa39c6e", //Timestamp
       "15bd7762b3ee8057ecb83b792e2168c6b6bddaf10163d110f7e63db387e6aacf", //TransactionsMerkleRoot
       "53c0ab896cb7a3778cc1d35a271264d991792b7c44f5c334116bb0786dbc5635", //TransactionStatusHash
       "8000000000000000", //Nonce
       "20000000007fffff", //Bits
       "e733c4b1c4ea57bc87346d9fce8c492248f1f414b9eac17faf9e9b8e0a107fa1",//Seed
       "bdba0400",//Target
    }
}
```

## Share Submission

Request looks like:

```javascript
{
  "id": 1,
  "jsonrpc": "2.0",
  "method": "submit",
  "params": {
    "1", //job_id
    "800000000" //nonce
    "010000001f020000" //result
  }
}
```

Request can include optional `worker` param:

```javascript
{ "id": 1, "worker": "rig-1" /* ... */ }
```

Response:

```javascript
{ "id": 1, "jsonrpc": "2.0", "result": true }
{ "id": 1, "jsonrpc": "2.0", "result": false }
```

Exceptions:

Pool MAY return exception on invalid share submission usually followed by temporal ban.

```javascript
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: 23, message: "Invalid share" } }
```

```javascript
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: 22, message: "Duplicate share" } }
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: -1, message: "High rate of invalid shares" } }
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: 25, message: "Not subscribed" } }
{ "id": 1, "jsonrpc": "2.0", "result": null, "error": { code: -1, message: "Malformed PoW result" } }
```
