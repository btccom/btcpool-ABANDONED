package eth

import (
	"context"
	"errors"
	"sync"
	"time"

	"github.com/Shopify/sarama"
	"github.com/ethereum/go-ethereum/rpc"
	"github.com/golang/glog"
)

const (
	rpcGetWorkMethod = "eth_getWork"
	ethNamespace     = "eth"
	subNewWorks      = "newWorks"
)

func rpcGetWork(wg *sync.WaitGroup, client *rpc.Client, workCh chan<- [10]string) {
	glog.Infof("Calling RPC %s to %s", rpcGetWorkMethod, rpcAddress)
	defer wg.Done()

	var work [10]string
	err := client.Call(&work, "eth_getWork")
	if err != nil {
		glog.Errorf("Failed to call RPC %s to %s: %v", rpcGetWorkMethod, rpcAddress, err)
		return
	}
	workCh <- work
}

func rpcLoop(ctx context.Context, wg *sync.WaitGroup, producer sarama.AsyncProducer) error {
	client, err := rpc.DialContext(ctx, rpcAddress)
	if err != nil {
		glog.Errorf("Failed to dail %s: %v", rpcAddress, err)
		return err
	}
	defer client.Close()

	workCh := make(chan [10]string)
	subscription, err := client.Subscribe(ctx, ethNamespace, workCh, subNewWorks)
	if err != nil {
		glog.Errorf("Failed to subscribe to new works: %v", err)
		return err
	}
	defer subscription.Unsubscribe()

	expiryTimer := time.NewTimer(notifyTimeout)
	intervalTimer := time.NewTimer(rpcInterval)

	for {
		select {
		case work := <-workCh:
			wg.Add(1)
			go processWork(wg, work, producer)
			expiryTimer.Stop()
			expiryTimer.Reset(notifyTimeout)
			intervalTimer.Stop()
			intervalTimer.Reset(rpcInterval)
		case <-intervalTimer.C:
			wg.Add(1)
			go rpcGetWork(wg, client, workCh)
		case <-expiryTimer.C:
			return errors.New("subscription timeout")
		case err := <-subscription.Err():
			return err
		case <-ctx.Done():
			return nil
		}
	}
}
