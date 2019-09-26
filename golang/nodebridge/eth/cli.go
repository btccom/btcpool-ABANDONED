package eth

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
	classic       bool
	rpcAddress    string
	rpcInterval   time.Duration
	notifyTimeout time.Duration
	kafkaBrokers  string
	rawGwTopic    string

	Command = cli.Command{
		Name:  "eth",
		Usage: "Node bridge for ETH/ETC network",
		Flags: []cli.Flag{
			cli.BoolFlag{
				Name:        "classic",
				Usage:       "Whether node bridge is connected to a ETC node",
				Destination: &classic,
			},
			cli.StringFlag{
				Name:        "rpc_addr",
				Usage:       "Node RPC address in <protocol>//<address>:<port> format",
				Destination: &rpcAddress,
				Required:    true,
			},
			cli.DurationFlag{
				Name:        "rpc_interval",
				Usage:       "Node RPC interval",
				Value:       3 * time.Second,
				Destination: &rpcInterval,
			},
			cli.DurationFlag{
				Name:        "notify_timeout",
				Usage:       "Node RPC notification timeout",
				Value:       30 * time.Second,
				Destination: &notifyTimeout,
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
