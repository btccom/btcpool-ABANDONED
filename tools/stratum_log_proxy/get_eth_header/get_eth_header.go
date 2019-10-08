package main

// LICENSE:
//		GNU LESSER GENERAL PUBLIC LICENSE Version 3
//
// Notice:
// 		The file and its executable are licensed under the GNU LGPLv3 license because it copied the code from go-ethereum.
// 		Note that only this file is licensed under LGPLv3, which is not the case with other files in the repository.
// 		The file and its executables exist independently in the repository and are not dependent or dependent on other parts.
// 		You can find the full text of the GNU LGPLv3 license here: https://www.gnu.org/licenses/lgpl-3.0.txt
//

import (
	"bufio"
	"database/sql"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/signal"
	"reflect"
	"time"

	_ "github.com/go-sql-driver/mysql"
	"github.com/golang/glog"
	"github.com/gorilla/websocket"
)

var mysql = flag.String("mysql", "user:password@tcp(localhost:3306)/dbname", "Websocket address:port of Ethereum node")
var node = flag.String("node", "localhost:8546", "Websocket address:port of Ethereum node")
var new = flag.Bool("new", false, "Get the newly generated block")
var min = flag.Uint64("min", 0, "Get past blocks: min height")
var max = flag.Uint64("max", 0, "Get past blocks: max height")
var sqlFile = flag.String("sqlfile", "", "Loop execution of the specified SQL file")
var interval = flag.Uint64("interval", 15, "The interval at which the specified SQL file is executed (seconds)")

type Uint64 uint64

// Header represents a block header in the Ethereum blockchain.
// Contain only fields of interest
type Header struct {
	Height   Uint64 `json:"number"`
	Hash     string `json:"hash"`
	Time     Uint64 `json:"timestamp"`
	Diff     Uint64 `json:"difficulty"`
	GasLimit Uint64 `json:"gasLimit"`
	GasUsed  Uint64 `json:"gasUsed"`
	Nonce    Uint64 `json:"nonce"`
}

type PastHeader struct {
	Result Header `json:"result"`
}

type NewHeader struct {
	Params struct {
		Result Header `json:"result"`
	} `json:"params"`
}

var insertStmt *sql.Stmt

func writeHeader(header Header) {
	glog.Infof("[block] height: %v, hash: %s, time: %v, diff: %v, gasLimit: %v, gasUsed: %v, nonce: %016x", header.Height, header.Hash, header.Time, header.Diff, header.GasLimit, header.GasUsed, header.Nonce)
	_, err := insertStmt.Exec(header.Height, header.Hash, header.Time, header.Diff, header.GasLimit, header.GasUsed, header.Nonce)
	if err != nil {
		glog.Fatal("mysql error: ", err.Error())
	}
}

func main() {
	flag.Parse()
	defer glog.Flush()

	// ------------- mysql connection -------------
	glog.Info("connecting to MySQL...")
	db, err := sql.Open("mysql", *mysql)
	if err != nil {
		glog.Fatal("mysql error: ", err)
	}
	defer db.Close()

	err = db.Ping()
	if err != nil {
		glog.Fatal("mysql error: ", err.Error())
	}

	insertStmt, err = db.Prepare(`
		INSERT INTO
			blocks(height,hash,time,diff,gas_limit,gas_used,nonce)
		VALUES(?,?,?,?,?,?,?)
		ON DUPLICATE KEY UPDATE
			hash=VALUES(hash),
			time=VALUES(time),
			diff=VALUES(diff),
			gas_limit=VALUES(gas_limit),
			gas_used=VALUES(gas_used),
			nonce=VALUES(nonce)
	`)
	if err != nil {
		glog.Fatal("mysql error: ", err.Error())
	}
	defer insertStmt.Close()

	glog.Info("connected to MySQL")

	if len(*sqlFile) > 0 {
		// ------------- SQL loop execution -------------
		go func() {
			glog.Infof("Execute %v every %v seconds", *sqlFile, *interval)
			for {
				time.Sleep(time.Duration(*interval) * time.Second)
				f, err := os.Open(*sqlFile)
				if err != nil {
					glog.Warning("open sql failed: ", *sqlFile)
					continue
				}
				defer f.Close()

				rd := bufio.NewReader(f)
				for {
					line, err := rd.ReadString('\n')
					if err != nil || io.EOF == err {
						break
					}
					if glog.V(2) {
						glog.Info("[SQL exec] ", line)
					}
					result, err := db.Exec(line)
					if err == nil {
						if glog.V(1) {
							row, _ := result.RowsAffected()
							glog.Infof("[SQL result] updated %v row", row)
						}
					} else {
						glog.Warning("[SQL error] ", err)
					}
				}
			}
		}()
	}

	// ------------- websocket connection -------------
	interrupt := make(chan os.Signal, 1)
	signal.Notify(interrupt, os.Interrupt)

	u := url.URL{Scheme: "ws", Host: *node, Path: "/"}
	glog.Infof("connecting to Ethereum node %s", u.String())

	c, _, err := websocket.DefaultDialer.Dial(u.String(), nil)
	if err != nil {
		glog.Fatal("dial:", err)
	}
	defer c.Close()

	done := make(chan struct{})

	nextBlock := func() {
		*min++
		if *min <= *max {
			glog.Infof("Get block %v", *min)
			request := fmt.Sprintf("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"eth_getBlockByNumber\",\"params\":[\"0x%x\",false]}", *min)
			if glog.V(2) {
				glog.Info("send: ", request)
			}
			err = c.WriteMessage(websocket.TextMessage, []byte(request))
			if err != nil {
				glog.Warning("write:", err)
				return
			}
		}
	}

	go func() {
		defer close(done)
		for {
			_, message, err := c.ReadMessage()
			if err != nil {
				glog.Warning("read:", err)
				return
			}
			if glog.V(3) {
				glog.Infof("recv: %s", message)
			}

			if *min > 0 && *min <= *max {
				var header PastHeader
				err = json.Unmarshal([]byte(message), &header)
				if err == nil && header.Result.Height > 0 {
					writeHeader(header.Result)
					nextBlock()
					continue
				}
				if err != nil && glog.V(4) {
					glog.Warning(err)
				}
			}

			if *new {
				var header NewHeader
				err = json.Unmarshal([]byte(message), &header)
				if err == nil && header.Params.Result.Height > 0 {
					writeHeader(header.Params.Result)
					if *min > 0 && *max == 0 {
						*max = uint64(header.Params.Result.Height) - 1
						*min--
						nextBlock()
					}
					continue
				}
				if err != nil && glog.V(4) {
					glog.Warning(err)
				}
			}
		}
	}()

	if *max == 0 {
		*new = true
	} else {
		if *max > 0 && *min == 0 {
			*min = 1
		}
		*min--
		nextBlock()
	}

	if *new {
		glog.Infof("Waiting for the node to push a new block...")
		request := "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_subscribe\",\"params\":[\"newHeads\"]}"
		if glog.V(2) {
			glog.Info("send: ", request)
		}
		err = c.WriteMessage(websocket.TextMessage, []byte(request))
		if err != nil {
			glog.Warning("write:", err)
			return
		}
	}

	for {
		select {
		case <-done:
			return
		case <-interrupt:
			glog.Warning("interrupt")

			// Cleanly close the connection by sending a close message and then
			// waiting (with timeout) for the server to close the connection.
			err := c.WriteMessage(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""))
			if err != nil {
				glog.Warning("write close:", err)
				return
			}
			select {
			case <-done:
			case <-time.After(time.Second):
			}
			return
		}
	}
}

// Copied from:
// https://github.com/ethereum/go-ethereum/blob/master/common/hexutil/hexutil.go
// https://github.com/ethereum/go-ethereum/blob/master/common/hexutil/json.go
//
// Copyright 2016 The go-ethereum Authors
// This file is part of the go-ethereum library.
//
// The go-ethereum library is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// The go-ethereum library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the go-ethereum library. If not, see <http://www.gnu.org/licenses/>.

const uintBits = 32 << (uint64(^uint(0)) >> 63)
const badNibble = ^uint64(0)

var (
	ErrEmptyString   = errors.New("empty hex string")
	ErrSyntax        = errors.New("invalid hex string")
	ErrMissingPrefix = errors.New("hex string without 0x prefix")
	ErrOddLength     = errors.New("hex string of odd length")
	ErrEmptyNumber   = errors.New("hex string \"0x\"")
	ErrLeadingZero   = errors.New("hex number with leading zero digits")
	ErrUint64Range   = errors.New("hex number > 64 bits")
	ErrUintRange     = errors.New(fmt.Sprintf("hex number > %d bits", uintBits))
	ErrBig256Range   = errors.New("hex number > 256 bits")
)

var (
	uint64T = reflect.TypeOf(Uint64(0))
)

func decodeNibble(in byte) uint64 {
	switch {
	case in >= '0' && in <= '9':
		return uint64(in - '0')
	case in >= 'A' && in <= 'F':
		return uint64(in - 'A' + 10)
	case in >= 'a' && in <= 'f':
		return uint64(in - 'a' + 10)
	default:
		return badNibble
	}
}

// Don't want Uint64 to be hexadecimal when outputting
/*
// EncodeUint64 encodes i as a hex string with 0x prefix.
func EncodeUint64(i uint64) string {
	enc := make([]byte, 2, 10)
	copy(enc, "0x")
	return string(strconv.AppendUint(enc, i, 16))
}

// Uint64 marshals/unmarshals as a JSON string with 0x prefix.
// The zero value marshals as "0x0".

// MarshalText implements encoding.TextMarshaler.
func (b Uint64) MarshalText() ([]byte, error) {
	buf := make([]byte, 2, 10)
	copy(buf, `0x`)
	buf = strconv.AppendUint(buf, uint64(b), 16)
	return buf, nil
}

// String returns the hex encoding of b.
func (b Uint64) String() string {
	return EncodeUint64(uint64(b))
}
*/

// UnmarshalJSON implements json.Unmarshaler.
func (b *Uint64) UnmarshalJSON(input []byte) error {
	if !isString(input) {
		return errNonString(uint64T)
	}
	return wrapTypeError(b.UnmarshalText(input[1:len(input)-1]), uint64T)
}

// UnmarshalText implements encoding.TextUnmarshaler
func (b *Uint64) UnmarshalText(input []byte) error {
	raw, err := checkNumberText(input)
	if err != nil {
		return err
	}
	if len(raw) > 16 {
		return ErrUint64Range
	}
	var dec uint64
	for _, byte := range raw {
		nib := decodeNibble(byte)
		if nib == badNibble {
			return ErrSyntax
		}
		dec *= 16
		dec += nib
	}
	*b = Uint64(dec)
	return nil
}

// ImplementsGraphQLType returns true if Uint64 implements the provided GraphQL type.
func (b Uint64) ImplementsGraphQLType(name string) bool { return name == "Long" }

// UnmarshalGraphQL unmarshals the provided GraphQL query data.
func (b *Uint64) UnmarshalGraphQL(input interface{}) error {
	var err error
	switch input := input.(type) {
	case string:
		return b.UnmarshalText([]byte(input))
	case int32:
		*b = Uint64(input)
	default:
		err = fmt.Errorf("Unexpected type for Long: %v", input)
	}
	return err
}

func isString(input []byte) bool {
	return len(input) >= 2 && input[0] == '"' && input[len(input)-1] == '"'
}

func bytesHave0xPrefix(input []byte) bool {
	return len(input) >= 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X')
}

func checkText(input []byte, wantPrefix bool) ([]byte, error) {
	if len(input) == 0 {
		return nil, nil // empty strings are allowed
	}
	if bytesHave0xPrefix(input) {
		input = input[2:]
	} else if wantPrefix {
		return nil, ErrMissingPrefix
	}
	if len(input)%2 != 0 {
		return nil, ErrOddLength
	}
	return input, nil
}

func checkNumberText(input []byte) (raw []byte, err error) {
	if len(input) == 0 {
		return nil, nil // empty strings are allowed
	}
	if !bytesHave0xPrefix(input) {
		return nil, ErrMissingPrefix
	}
	input = input[2:]
	if len(input) == 0 {
		return nil, ErrEmptyNumber
	}

	// Don't want it. The nonce field will trigger this condition.
	/*if len(input) > 1 && input[0] == '0' {
		return nil, ErrLeadingZero
	}*/
	return input, nil
}

func wrapTypeError(err error, typ reflect.Type) error {
	if _, ok := err.(error); ok {
		return &json.UnmarshalTypeError{Value: err.Error(), Type: typ}
	}
	return err
}

func errNonString(typ reflect.Type) error {
	return &json.UnmarshalTypeError{Value: "non-string", Type: typ}
}
