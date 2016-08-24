BTC Agent
==========

Communication Path:

```
Miners <-> BTCAgent <-> Pool
```

`uint16_t` and `uint32_t` are using litten-endian.

## Ex-Mesasge 

### Format

Name | Type | Description 
-----|------|-------------
magic_number | uint8_t | magic number for Ex-Message, always `0x7F`
command | uint8_t | 
message_body | char[] | message body

### Type

  Name |  Value | Description
-------|--------|------------
CMD\_REGISTER_WORKER | `0x00` | `Agent` -> `Pool`
CMD\_SUBMIT_SHARE | `0x01` |  `Agent` -> `Pool`
CMD\_SUBMIT\_SHARE\_WITH\_TIME | `0x02` |  `Agent` -> `Pool`
CMD\_MINING\_SET\_DIFF | `0x03` |  `Pool` -> `Agent`
CMD\_UNREGISTER_WORKER | `0x04` | `Agent` -> `Pool`

**Session ID**

We use `uint16_t` as BTCAgent miner's session ID, so each agent could hold 65536 miners at most.


### CMD\_REGISTER\_WORKER

When a new Miner connect to Agent, Agent should send `CMD_REGISTER_WORKER` to Pool. Pool should know each miner which under Agent.

**Format**

```
| magic_number(1) | cmd(1) | len (1) | session_id(2) | client_agent | worker_name |
```

Name | Type | Description
-----|------|------------
`len` | uint8_t | Total length of this message
`session_id ` | uint16_t | session ID
`client_agent` | string | end with `\0`
`worker_name ` | string | end with `\0`. If miner's setting is `kevin.s1099`, `worker_name` will be `s1099`.

**Example**

```
| 0x7F | 0x00 | 0x19 | 0x1122 | "cgminer\0" | "s1099\0" |

Message(hex): 0x7F 00 19 2211 63676d696e657200 733130393900
```

### CMD\_SUBMIT\_SHARE | CMD\_SUBMIT\_SHARE\_WITH\_TIME

When Miner's found a share and submit to Agent, Agent will convert it's format and than submit to Pool.

**Format**

```
| magic_number(1) | cmd(1) | job_Id (2) | session_id(2)
  | extra_nonce2(4) | nNonce(4) | nTime(4)(optional) |
```

Name | Type | Description
-----|------|------------
`job_Id` | uint8_t | 
`session_id ` | uint16_t | session ID
`extra_nonce2 ` | uint32_t | 
`nNonce` | uint32_t | 
`nTime` | uint32_t, optional | if miner doesn't change the block time, no need to submit it


**Example**

```
#
# CMD_SUBMIT_SHARE
#
| 0x7F | 0x01 | 0xa9 | 0x1122 | 0x11223344 | 0xaabbccdd | 

Message(hex): 0x7f 01 a9 2211 44332211 ddccbbaa

#
# CMD_SUBMIT_SHARE_WITH_TIME
#
| 0x7F | 0x02 | 0xa9 | 0x1122 | 0x11223344 | 0xaabbccdd | 0x57BD2AD0 |

Message(hex): 0x7f 02 a9 2211 44332211 ddccbbaa d02abd57
```

### CMD\_UNREGISTER\_WORKER

If Miner disconnect to Agent, Agent should send this message to Pool.

**Format**

```
| magic_number(1) | cmd(1) | session_id(2) |
```

Name | Type | Description
-----|------|------------
`session_id ` | uint16_t | session ID

**Example**

```
| 0x7F | 0x04 | 0x1122 |

Message(hex): 0x7F 04 2211
```

### CMD\_MINING\_SET\_DIFF

If Pool change the Miner's `difficulty`, it should send this message. Agent will convert to stratum message and send to Miners. Miners have the same `diff` will in one message.

**Format**

```
| magic_number(1) | cmd(1) | diff(4) | count(4) | session_id(2) | ... | session_id(2) |
```

Name | Type | Description
-----|------|------------
`diff` | uint32_t | mining difficulty, max diff is `UINT32_MAX`
`count` | uint32_t | how many session ids in this message
`session_id` | uint16_t | session ID

**Example**

```
#
# diff: 8192 -> 0x2000 -> 0x0020(little-endian)
# with two session ids
#
| 0x7F | 0x03 | 0x2000 | 0x00000002 | 0x1122 | 0x3344 |

Message(hex): 0x7F 03 0020 02000000 2211 4433
```
