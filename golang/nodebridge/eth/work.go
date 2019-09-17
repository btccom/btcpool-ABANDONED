package eth

import (
	"fmt"
	"strconv"
	"sync"
	"time"

	"github.com/Shopify/sarama"
	"github.com/golang/glog"
)

func chainType(classic bool) string {
	if classic {
		return "ETC"
	} else {
		return "ETH"
	}
}

func processWork(wg *sync.WaitGroup, work [10]string, producer sarama.AsyncProducer) {
	glog.Infof("New work received: %v", work)
	defer wg.Done()

	height, err := strconv.ParseUint(work[3], 0, 64)
	if err != nil {
		glog.Errorf("Invalid height: %s", work[3])
		return
	}
	gasLimit, err := strconv.ParseUint(work[5], 0, 64)
	if err != nil {
		glog.Errorf("Invalid gas limit: %s", work[5])
		return
	}
	gasUsed, err := strconv.ParseUint(work[6], 0, 64)
	if err != nil {
		glog.Errorf("Invalid gas used: %s", work[6])
		return
	}
	transactions, err := strconv.ParseUint(work[7], 0, 64)
	if err != nil {
		glog.Errorf("Invalid transactions: %s", work[7])
		return
	}
	uncles, err := strconv.ParseUint(work[8], 0, 64)
	if err != nil || uncles > 2 {
		glog.Errorf("Invalid uncles: %s", work[8])
		return
	}
	rawGetWork := fmt.Sprintf(
		`{"created_at_ts":%d,"chainType":"%s","rpcAddress":"%s","rpcUserPwd":"","parent":"%s","target":"%s","hHash":"%s","sHash":"%s","height":%d,"uncles":%d,"transactions":%d,"gasUsedPercent":%f,"header":"%s"}`,
		time.Now().Unix(),
		chainType(classic),
		rpcAddress,
		work[4],
		work[2],
		work[0],
		work[1],
		height,
		uncles,
		transactions,
		float64(gasUsed)/float64(gasLimit)*100,
		work[9],
	)

	glog.Infof("Raw GW messages: %s", rawGetWork)
	msg := &sarama.ProducerMessage{
		Topic:     rawGwTopic,
		Partition: 0,
		Value:     sarama.StringEncoder(rawGetWork),
	}
	producer.Input() <- msg
}
