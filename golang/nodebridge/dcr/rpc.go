package dcr

import (
	"context"
	"io/ioutil"
	"sync"
	"time"

	"github.com/Shopify/sarama"
	"github.com/decred/dcrd/rpc/jsonrpc/types"
	"github.com/decred/dcrd/rpcclient/v4"
	"github.com/golang/glog"
)

func rpcGetWork(wg *sync.WaitGroup, client *rpcclient.Client, workCh chan *types.GetWorkResult, errCh chan error) {
	glog.Info("Calling getwork")
	defer wg.Done()

	work, err := client.GetWork()
	if err != nil {
		glog.Errorf("Failed to get work: %v", err)
		errCh <- err
	} else {
		glog.Infof("Get work result: %s", *work)
		workCh <- work
	}
}

func rpcLoop(ctx context.Context, wg *sync.WaitGroup, producer sarama.AsyncProducer) error {
	certs, err := ioutil.ReadFile(rpcCertificate)
	if err != nil {
		glog.Fatalf("Failed to load RPC certificate: %v", err)
		return err
	}

	workCh := make(chan *types.GetWorkResult, 16)
	notifyCh := make(chan bool, 16)
	errCh := make(chan error)
	ntfnHandlers := rpcclient.NotificationHandlers{
		OnBlockConnected: func(blockHeader []byte, transactions [][]byte) {
			glog.Infof("Block connected")
			notifyCh <- true
		},
	}
	connCfg := &rpcclient.ConnConfig{
		Host:                rpcAddress,
		Endpoint:            "ws",
		User:                rpcUsername,
		Pass:                rpcPassword,
		Certificates:        certs,
		DisableConnectOnNew: true,
	}

	client, err := rpcclient.New(connCfg, &ntfnHandlers)
	if err != nil {
		glog.Errorf("Failed to create RPC client: %v", err)
		return err
	}
	defer client.WaitForShutdown()

	err = client.Connect(ctx, true)
	if err != nil {
		glog.Errorf("Failed to create RPC client: %v", err)
		return err
	}

	network, err := client.GetCurrentNet()
	if err != nil {
		glog.Errorf("Failed to get network type: %v", err)
		return err
	} else {
		glog.Infof("DCR network type: %s", network)
	}

	err = client.NotifyBlocks()
	if err != nil {
		glog.Errorf("Failed to register block notifications: %v", err)
		return err
	}

	intervalTimer := time.NewTimer(rpcInterval)
	notifyCh <- true

	for {
		select {
		case work := <-workCh:
			wg.Add(1)
			go processWork(wg, network, work, producer)
			intervalTimer.Stop()
			intervalTimer.Reset(rpcInterval)
		case <-intervalTimer.C:
			wg.Add(1)
			go rpcGetWork(wg, client, workCh, errCh)
		case <-notifyCh:
			wg.Add(1)
			go rpcGetWork(wg, client, workCh, errCh)
		case <-errCh:
			intervalTimer.Stop()
			intervalTimer.Reset(rpcInterval)
		case <-ctx.Done():
			client.Shutdown()
			return nil
		}
	}
}
