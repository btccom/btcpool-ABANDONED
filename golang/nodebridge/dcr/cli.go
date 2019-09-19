package dcr

import (
	"context"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/Shopify/sarama"
	"github.com/golang/glog"
	"github.com/urfave/cli"
)

var (
	rpcAddress     string
	rpcUsername    string
	rpcPassword    string
	rpcCertificate string
	rpcInterval    time.Duration
	kafkaBrokers   string
	rawGwTopic     string

	Command = cli.Command{
		Name:  "dcr",
		Usage: "Node bridge for DCR network",
		Flags: []cli.Flag{
			cli.StringFlag{
				Name:        "rpc_addr",
				Usage:       "Node RPC address in <address>:<port> format",
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
				Destination: &rpcCertificate,
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
	config := sarama.NewConfig()
	producer, err := sarama.NewAsyncProducer(strings.Split(kafkaBrokers, ","), config)
	if err != nil {
		return err
	}

	var wg sync.WaitGroup
	runCtx, cancel := context.WithCancel(context.Background())

	wg.Add(1)
	go func(wg *sync.WaitGroup) {
		defer wg.Done()
		for err := range producer.Errors() {
			glog.Error(err)
		}
	}(&wg)

	wg.Add(1)
	go func(wg *sync.WaitGroup) {
		defer wg.Done()
		for {
			err := rpcLoop(runCtx, wg, producer)
			if err != nil && err != context.Canceled {
				glog.Error("Sleep 5 seconds before reconnecting")
				time.Sleep(5 * time.Second)
			} else {
				return
			}
		}
	}(&wg)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	glog.Info("Shutting down")
	cancel()
	producer.Close()
	wg.Wait()

	return nil
}
