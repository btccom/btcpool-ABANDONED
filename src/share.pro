syntax="proto2";
package sharebase;

message BitcoinMsg {
  required sint32     version       = 1;
  optional sint64     workerhashid  = 2;
  optional sint32     userid        = 3;
  optional sint32     status        = 4;
  optional sint64     timestamp     = 5;
  optional string     ip            = 6;
  optional uint64     jobid         = 7;
  optional uint64     sharediff     = 8;
  optional uint32     blkbits       = 9;
  optional uint32     height        = 10;
  optional uint32     nonce         = 11;
  optional uint32     sessionid     = 12;
  optional uint32     versionMask   = 13;
}


message EthMsg{
  required sint32     version       = 1;
  optional sint64     workerhashid  = 2;
  optional sint32     userid        = 3;
  optional sint32     status        = 4;
  optional sint64     timestamp     = 5;
  optional string     ip            = 6;
  optional uint64     headerhash    = 7;
  optional uint64     sharediff     = 8;
  optional uint64     networkdiff   = 9;
  optional uint32     height        = 10;
  optional uint64     nonce         = 11;
  optional uint32     sessionid     = 12;
}


message SiaMsg{
  required uint32     version       = 1;
  optional sint64     workerhashid  = 2;
  optional sint32     userid        = 3;
  optional sint32     status        = 4;
  optional sint64     timestamp     = 5;
  optional string     ip            = 6;
  optional uint64     jobid         = 7;
  optional uint64     sharediff     = 8;
  optional uint32     blkbits       = 9;
  optional uint32     height        = 10;
  optional uint32     nonce         = 11;
  optional uint32     sessionid     = 12;
}


message DecredMsg{
  required uint32     version       = 1;
  optional sint64     workerhashid  = 2;
  optional sint32     userid        = 3;
  optional sint32     status        = 4;
  optional sint64     timestamp     = 5;
  optional string     ip            = 6;
  optional uint64     jobid         = 7;
  optional uint64     sharediff     = 8;
  optional uint32     blkbits       = 9;
  optional uint32     height        = 10;
  optional uint32     nonce         = 11;
  optional uint32     sessionid     = 12;
  optional uint32     network       = 13;
  optional uint32     voters        = 14;
}

message BytomMsg{

  required uint32   version       = 1;
  optional uint64   jobid         = 2;
  optional sint64   workerhashid  = 3;
  optional sint64   timestamp     = 4;
  optional uint64   sharediff     = 5;
  optional uint64   blkbits       = 6;
  optional uint64   height        = 7;
  optional string   ip            = 8;
  optional bytes    combinedHeader= 9;
  optional sint32   userid        = 10;
  optional sint32   status        = 11;
}
