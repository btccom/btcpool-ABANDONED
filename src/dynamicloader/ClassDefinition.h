/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#ifndef CLASS_DEFINITION_H_
#define CLASS_DEFINITION_H_

/**
* Paste the method's formal parameters & actual parameter to here.
* 
* Return value's type: DYCLASS_<className>_<methodName>_RETURN
* Formal parameters:   DYCLASS_<className>_<methodName>_FPARAMS
* Actual parameters:   DYCLASS_<className>_<methodName>_APARAMS
*
* Use DYCLASS_<className>_<className>_FPARAMS as constructor.
* Return value's type is not required for constructor.
*/


//-------------- BlockMaker --------------

#define DYCLASS_BlockMaker_BlockMaker_FPARAMS \
  const char *kafkaBrokers, const MysqlConnectInfo &poolDB
#define DYCLASS_BlockMaker_BlockMaker_APARAMS \
  kafkaBrokers, poolDB

#define DYCLASS_BlockMaker_addBitcoind_RETURN \
  void
#define DYCLASS_BlockMaker_addBitcoind_FPARAMS \
  const string &rpcAddress, const string &rpcUserpass
#define DYCLASS_BlockMaker_addBitcoind_APARAMS \
  rpcAddress, rpcUserpass

#define DYCLASS_BlockMaker_init_RETURN \
  bool
#define DYCLASS_BlockMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_BlockMaker_stop_RETURN \
  void
#define DYCLASS_BlockMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_BlockMaker_run_RETURN \
  void
#define DYCLASS_BlockMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_BlockMaker_run_APARAMS \
  /*empty*/

//-------------- GbtMaker --------------

#define DYCLASS_GbtMaker_GbtMaker_FPARAMS \
  const string &zmqBitcoindAddr, \
  const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass, \
  const string &kafkaBrokers, uint32_t kRpcCallInterval, \
  bool isCheckZmq
#define DYCLASS_GbtMaker_GbtMaker_APARAMS \
  zmqBitcoindAddr, bitcoindRpcAddr, bitcoindRpcUserpass, \
  kafkaBrokers, kRpcCallInterval, isCheckZmq

#define DYCLASS_GbtMaker_init_RETURN \
  bool
#define DYCLASS_GbtMaker_init_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_init_APARAMS \
  /*empty*/

#define DYCLASS_GbtMaker_stop_RETURN \
  void
#define DYCLASS_GbtMaker_stop_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_stop_APARAMS \
  /*empty*/

#define DYCLASS_GbtMaker_run_RETURN \
  void
#define DYCLASS_GbtMaker_run_FPARAMS \
  /*empty*/
#define DYCLASS_GbtMaker_run_APARAMS \
  /*empty*/


#endif
