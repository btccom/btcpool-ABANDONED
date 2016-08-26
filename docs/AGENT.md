BTC Agent
==========

Communication Path:

```
Miners <-> BTCAgent <-> Pool
```

* `uint16_t` and `uint32_t` are using litten-endian.

## Ex-Mesasge 

### Format

Use `uint16_t` as message length, so max message length is `UINT16_MAX` 65535 bytes.

Name | Type | Description 
-----|------|-------------
magic_number | uint8_t | magic number for Ex-Message, always `0x7F`
type / cmd | uint8_t | message type
length | uint16_t | message length
message_body | uint8_t[] | message body

### Message Type

  Name |  Value | Description
-------|--------|------------
CMD\_REGISTER_WORKER | `0x01` | `Agent` -> `Pool`
CMD\_SUBMIT_SHARE | `0x02` |  `Agent` -> `Pool`
CMD\_SUBMIT\_SHARE\_WITH\_TIME | `0x03` |  `Agent` -> `Pool`
CMD\_UNREGISTER\_WORKER | `0x04` | `Agent` -> `Pool`
CMD\_MINING\_SET\_DIFF | `0x05` |  `Pool` -> `Agent`

**Session ID**

We use `uint16_t` as BTCAgent miner's session ID, so each agent could hold 65536 miners at most.


### CMD\_REGISTER\_WORKER

When a new Miner connect to Agent, Agent should send `CMD_REGISTER_WORKER` to Pool. Pool should know each miner which under Agent.

**Format**

```
| magic_number(1) | cmd(1) | len (2) | session_id(2) | client_agent | worker_name |
```

Name | Type | Description
-----|------|------------
`session_id ` | uint16_t | session ID
`client_agent` | string | end with `\0`
`worker_name ` | string | end with `\0`. If miner's setting is `kevin.s1099`, `worker_name` will be `s1099`.

**Example**

```
| 0x7F | 0x01 | 0x0020 | 0x1122 | "cgminer\0" | "s1099\0" |

Message(hex): 0x7F 01 2000 2211 63676d696e657200 733130393900
```

### CMD\_SUBMIT\_SHARE | CMD\_SUBMIT\_SHARE\_WITH\_TIME

When Miner's found a share and submit to Agent, Agent will convert it's format and than submit to Pool.

**Format**

```
| magic_number(1) | cmd(1) | len(2) | job_id(1) | session_id(2)
  | extra_nonce2(4) | nNonce(4) | nTime(4)(optional) |
```

Name | Type | Description
-----|------|------------
`job_Id` | uint8_t | stratum job ID
`session_id ` | uint16_t | session ID
`extra_nonce2 ` | uint32_t | 
`nNonce` | uint32_t | 
`nTime` | uint32_t, optional | if miner doesn't change the block time, no need to submit it


**Example**

```
#
# CMD_SUBMIT_SHARE
#
| 0x7F | 0x02 | 0x000f | 0xa9 | 0x1122 | 0x11223344 | 0xaabbccdd | 

Message(hex): 0x7f 02 0f00 a9 2211 44332211 ddccbbaa

#
# CMD_SUBMIT_SHARE_WITH_TIME
#
| 0x7F | 0x03 | 0x0013 | 0xa9 | 0x1122 | 0x11223344 | 0xaabbccdd | 0x57BD2AD0 |

Message(hex): 0x7f 03 1300 a9 2211 44332211 ddccbbaa d02abd57
```

### CMD\_UNREGISTER\_WORKER

If Miner disconnect to Agent, Agent should send this message to Pool.

**Format**

```
| magic_number(1) | cmd(1) | len(2) | session_id(2) |
```

Name | Type | Description
-----|------|------------
`session_id ` | uint16_t | session ID

**Example**

```
| 0x7F | 0x04 | 0x0006 | 0x1122 |

Message(hex): 0x7F 04 0600 2211
```

### CMD\_MINING\_SET\_DIFF

If Pool change the Miner's `difficulty`, it should send this message. Agent will convert to stratum message and send to Miners. Miners have the same `diff` will in one message.

**Format**

```
| magic_number(1) | cmd(1) | len(2) | diff_exp(1) | count(2) | session_id(2) | ... | session_id(2) |
```

Name | Type | Description
-----|------|------------
`diff_exp` | uint8_t | diff = 2^diff_exp
`count` | uint16_t | how many session ids in this message
`session_id` | uint16_t | session ID

**Example**

```
#
# diff: 8192 -> 2^13, 13 = 0x0d
# with two session ids
#
| 0x7F | 0x05 | 0x000b | 0x0d | 0x0002 | 0x1122 | 0x3344 |

Message(hex): 0x7F 05 0b00 0d 0200 2211 4433
```
