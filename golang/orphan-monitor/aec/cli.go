package aec

import (
	"os"
	"os/signal"
	"orphan-monitor/util"
	"syscall"
	"strconv"
	"time"
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
		Name:  "aec",
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

type RPCResponse struct {
	Height        int      `json:"height"`
}

func GetChainTip() (height int, err error) {
	headers := make(map[string]string)
	endpoint := rpcAddress + "/v2/key-blocks/current/height"

	responseJSON, err := rpcClient.GetJson(endpoint, headers)
	if err != nil {
		return -1, err
	}

	response := new(RPCResponse)
	err = json.Unmarshal(responseJSON, response)
	if err != nil {
		glog.Info("Parse Result Failed: ", err)
		return -1, err
	}

	height = response.Height

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
			time.Sleep(10 * time.Second)
		} else {
			return true, nil
		}
	}
}

func GetBlockHash(height int) (blockhash string, err error) {
	headers := make(map[string]string)
	endpoint := rpcAddress + "/v2/key-blocks/height/" + strconv.Itoa(height)
	responseJSON, err := rpcClient.GetJson(endpoint, headers)

	var v interface{}
	err = json.Unmarshal(responseJSON, &v)
    if err != nil {
        glog.Info("Umarshal failed:", err)
        return "", err
    }
	response := v.(map[string]interface{})

	for key, value := range response {
		switch data := value.(type) {
		case string:
			if(key == "hash") {
				blockhash = data
			}
		}
    }

	return blockhash, err
}

func run(ctx *cli.Context) error {
	rpcClient = util.NewHttpClient()

	dbhandle.InitDB(dbConfig)

	for {
		heights, height2hash := dbhandle.GetLastestMinedHeight(lastUpdatedHeight)

		if len(heights) > 0 {
			lastUpdatedHeight = heights[len(heights) - 1]
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
			glog.Info("update height: %d hash : %s minedhash : %s ", height, curblockhash, height2hash[height])
		}

	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	glog.Info("Shutting down")

	return nil
}
