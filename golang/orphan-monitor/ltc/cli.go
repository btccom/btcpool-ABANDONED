package ltc

import (
	"os"
	"os/signal"
	"orphan-monitor/util"
	"syscall"
	"strconv"
	"time"
	"bytes"
	"encoding/json"

	"github.com/golang/glog"
	"github.com/urfave/cli"
)

var (
	rpcAddress       string
	rpcUserName      string
	rpcPassWord      string
	dbConfig         util.DBInfo
	dbhandle         util.DBConnection
	rpcClient        *util.Client
	lastUpdatedHeight int

	Command = cli.Command{
		Name:  "ltc",
		Usage: "update orphan for aeternity",
		Flags: []cli.Flag{
			&cli.StringFlag{
				Name:        "rpc_addr",
				Usage:       "Node RPC address in <protocol>//<address>:<port> format",
				Destination: &rpcAddress,
				Required:    true,
			},
			&cli.StringFlag{
				Name:        "rpc_user",
				Usage:       "Node RPC allowed username",
				Destination: &rpcUserName,
				Required:    true,
			},
			&cli.StringFlag{
				Name:        "rpc_pass",
				Usage:       "Node RPC allowed username",
				Destination: &rpcPassWord,
				Required:    true,
			},
			&cli.StringFlag{
				Name:        "db_addr",
				Usage:       "mysql address in <protocol>//<address>:<port> format",
				Required:    true,
				Destination: &dbConfig.Host,
			},
			&cli.StringFlag{
				Name:        "db_user",
				Usage:       "User's name to connect to the database",
				Required:    true,
				Destination: &dbConfig.User,
			},
			&cli.StringFlag{
				Name:        "db_pass",
				Usage:       "User's pass to connect to the database",
				Required:    true,
				Destination: &dbConfig.Pass,
			},
			&cli.StringFlag{
				Name:        "db_name",
				Usage:       "database name",
				Required:    true,
				Destination: &dbConfig.Dbname,
			},
			&cli.StringFlag{
				Name:        "db_table",
				Usage:       "database table",
				Required:    true,
				Destination: &dbConfig.Table,
			},
			&cli.IntFlag {
				Name:        "lastupdatedheight",
				Usage:       "database table",
				Value:       0,
				Destination: &lastUpdatedHeight,
			},
		},
		Action: run,
	}
)
//{"result":1259646,"error":null,"id":"1"}
type RPCHeightResponse struct {
	ID     string `json:"id"`
	Result int    `json:"result"`
	Error  string `json:"error"`
}

type RPCHashResponse struct {
	ID     string `json:"id"`
	Result string `json:"result"`
	Error  string `json:"error"`
}

func GetChainTip() (height int, err error) {
	headers := make(map[string]string)
	payload := "{\"jsonrpc\": \"2.0\", \"id\":\"1\", \"method\": \"getblockcount\", \"params\": [] }"

	responseJSON, err := rpcClient.Post(rpcAddress, rpcUserName, rpcPassWord, bytes.NewBuffer([]byte(payload)), headers)
	if err != nil {
		return -1, err
	}

	response := new(RPCHeightResponse)
	err = json.Unmarshal(responseJSON, response)
	if err != nil {
		glog.Info("Parse Result Failed: ", err)
		return -1, err
	}
	height = response.Result

	return height, nil
}

func WaitUntilHeight(height int) (bool, error) {
	for {
		blocktip, err := GetChainTip ()
		if err != nil {
			glog.Info("get block chain tip failed ", err)
			return false, err
		}
		if blocktip < height {
			glog.Info("chain tip : ", blocktip, " less than mined height : ", height)
			time.Sleep(10 * time.Second)
		} else {
			return true, nil
		}
	}
}

func GetBlockHash(height int) (blockhash string, err error) {
	headers := make(map[string]string)
	payload := "{\"jsonrpc\": \"2.0\", \"id\":\"1\", \"method\": \"getblockhash\", \"params\": [" + strconv.Itoa(height) + "] }"

	responseJSON, err := rpcClient.Post(rpcAddress, rpcUserName, rpcPassWord, bytes.NewBuffer([]byte(payload)), headers)

	response := new(RPCHashResponse)
	err = json.Unmarshal(responseJSON, response)
	if err != nil {
		glog.Info("Parse Result Failed: ", err)
		return "", err
	}
	blockhash = response.Result
	return blockhash, err
}

func run(ctx *cli.Context) error {

	rpcClient = util.NewHttpClient()
	dbhandle.InitDB(dbConfig)

	for {
		heights, height2hash := dbhandle.GetLastestMinedHeight(lastUpdatedHeight)

		if len(heights) > 0 {
			lastUpdatedHeight = heights[len(heights) - 1]
		} else {
			glog.Info("current mined block height : ", lastUpdatedHeight, " in mysql")
		}

		for _, height := range heights {
			ok, err := WaitUntilHeight(lastUpdatedHeight)
			if err != nil || !ok {
				glog.Info("Wait block chain tip Until Height failed ", err)
				panic(err)
			}

			curblockhash, err := GetBlockHash(height)
			if height2hash[height] != curblockhash {
				dbhandle.UpdateOrphanBlock(height, true)
			}
			glog.Info("update height: ", height, " hash : ", curblockhash, " minedhash : ", height2hash[height] )
		}
		time.Sleep(10 * time.Second)

	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	glog.Info("Shutting down")

	return nil
}
