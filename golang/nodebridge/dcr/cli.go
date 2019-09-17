package dcr

import (
	"time"

	"github.com/urfave/cli"
)

var (
	rpcAddress     string
	rpcUsername    string
	rpcPassword    string
	rpcCertiticate string
	rpcInterval    time.Duration
	kafkaBrokers   string
	rawGwTopic     string

	Command = cli.Command{
		Name:  "dcr",
		Usage: "Node bridge for ETH/ETC network",
		Flags: []cli.Flag{
			cli.StringFlag{
				Name:        "rpc_addr",
				Usage:       "Node RPC address in <protocol>//<address>:<port> format",
				Destination: &rpcAddress,
				Required:    true,
			},
			cli.StringFlag{
				Name:        "rpc_user",
				Usage:       "Node RPC username",
				Destination: &rpcUsername,
				Required:    true,
			},
			cli.StringFlag{
				Name:        "rpc_pass",
				Usage:       "Node RPC password",
				Destination: &rpcPassword,
				Required:    true,
			},
			cli.StringFlag{
				Name:        "rpc_cert",
				Usage:       "Node RPC certificate",
				Destination: &rpcCertiticate,
				Required:    true,
			},
			cli.DurationFlag{
				Name:        "rpc_interval",
				Usage:       "Node RPC interval",
				Value:       3 * time.Second,
				Destination: &rpcInterval,
			},
			cli.StringFlag{
				Name:        "kafka_brokers",
				Usage:       "Kafka broker address in comma separated <address>:<port> format",
				Destination: &kafkaBrokers,
				Required:    true,
			},
			cli.StringFlag{
				Name:        "rawgw_topic",
				Usage:       "Kafka topic for raw getwork messages",
				Required:    true,
				Destination: &rawGwTopic,
			},
		},
		Action: run,
	}
)

func run(ctx *cli.Context) error {
	return nil
}
