package dcr

import (
	"fmt"
	"sync"
	"time"

	"github.com/Shopify/sarama"
	"github.com/decred/dcrd/rpc/jsonrpc/types"
	"github.com/decred/dcrd/wire"
	"github.com/golang/glog"
)

func processWork(wg *sync.WaitGroup, network wire.CurrencyNet, work *types.GetWorkResult, producer sarama.AsyncProducer) {
	defer wg.Done()

	rawGetWork := fmt.Sprintf(
		`{"created_at_ts":%d,"chainType":"DCR","rpcAddress":"%s","rpcUserPwd":"%s:%s","data":"%s","target":"%s","network":%d}`,
		time.Now().Unix(),
		rpcAddress,
		rpcUsername,
		rpcPassword,
		work.Data,
		work.Target,
		network,
	)

	glog.Infof("Raw GW messages: %s", rawGetWork)
	msg := &sarama.ProducerMessage{
		Topic:     rawGwTopic,
		Partition: 0,
		Value:     sarama.StringEncoder(rawGetWork),
	}
	producer.Input() <- msg
}