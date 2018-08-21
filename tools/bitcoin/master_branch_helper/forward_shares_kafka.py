import subprocess
import os
import sys
import time
import json
import datetime
import pickle
import MySQLdb
import copy
from struct import *
from subprocess import Popen, PIPE, STDOUT
from kafka import KafkaConsumer
from kafka import KafkaProducer
from kafka import TopicPartition
from os.path import isfile, join
from operator import itemgetter

from subprocess import check_output

class FileLogger(object):
    def __init__(self, file_out):
        self.terminal = sys.stdout
        self.log = open(file_out, "w+")

    def write(self, message):
        self.terminal.write(message)
        self.log.write(message)  

    def flush(self):
        self.terminal.flush()
        self.log.flush()


def create_kafka_consumer(topics, server, consumer_timeout = 12000):
    consumer = KafkaConsumer(bootstrap_servers=server, consumer_timeout_ms=consumer_timeout)
    tp = []
    for topic in topics:
        tp.append(TopicPartition(topic, 0))
    consumer.assign(tp)    
    return consumer

def deserialize_shareDevethBranch(msg_value):
    current_version = 0x00010002
    share_bitcoin_fmt = 'IIQqIiQqIiIQQ'
    res_bitcoin = {}

    expected_len = calcsize(share_bitcoin_fmt)
    if expected_len == len(msg_value):
        unpacked_bitcoin = unpack(share_bitcoin_fmt, msg_value)
        res_bitcoin = {
            "version" : unpacked_bitcoin[0],
            "checksum" : unpacked_bitcoin[1],

            "jobId" : unpacked_bitcoin[2],
            "workerHashId" : unpacked_bitcoin[3],
            "LegacyIP4" : unpacked_bitcoin[4],
            "userId" : unpacked_bitcoin[5],
            "shareDiff" : unpacked_bitcoin[6],
            "timeStamp" : unpacked_bitcoin[7],
            "blkBits" : unpacked_bitcoin[8],
            "status" : unpacked_bitcoin[9],

            "height" : unpacked_bitcoin[10],
            "IP_1" : unpacked_bitcoin[11],
            "IP_2" : unpacked_bitcoin[12]
        }
        if res_bitcoin["version"] != current_version:
            print("WARNING: Test is using share version " + hex(current_version) + " - received share version " + hex(res_bitcoin["version"]))
    else:
        print("wrong sharelog size. expected len %d. message len: %d" % (expected_len, len(msg_value)))
    return res_bitcoin

def forward_topics(settings):
    deveth_shareLog_topic = settings["deveth_shareLog_topic"]
    master_shareLog_topic = settings["master_shareLog_topic"]
    # deveth_solvedShare_topic = settings["deveth_solvedShare_topic"]
    # master_solvedShare_topic = settings["master_solvedShare_topic"]
    deveth_commonEvent_topic = settings["deveth_commonEvent_topic"]
    master_commonEvent_topic = settings["master_commonEvent_topic"]

    deveth_broker = settings["deveth_broker"]
    master_broker = settings["master_broker"]

    shareLog_consumer_created = False
    while(shareLog_consumer_created == False):
        try:
            shareLog_consumer = create_kafka_consumer([deveth_shareLog_topic], deveth_broker)
            shareLog_consumer_created = True
        except:
            print("Cannot create consumer for topic %s. Retry in 1 second" % deveth_shareLog_topic)
            time.sleep(1)

    # commonEvent_consumer_created = False
    # while(commonEvent_consumer_created == False):
    #     try:
    #         commonEvent_consumer = create_kafka_consumer([deveth_commonEvent_topic], deveth_broker)
    #         commonEvent_consumer_created = True
    #     except:
    #         print("Cannot create consumer for topic %s. Retry in 1 second" % deveth_commonEvent_topic)
    #         time.sleep(1)

    # solvedShare_consumer = create_kafka_consumer([deveth_solvedShare_topic], deveth_broker)
    producer_created = False
    while(producer_created == False):
        try:
            producer = KafkaProducer(bootstrap_servers=[master_broker])
            producer_created = True
        except:
            print("Cannot create producer for ip %s. Retry in 1 second" % master_broker)
            time.sleep(1)

    while True:
        # print("FORWARD TOPICS")
        # polled_messages = solvedShare_consumer.poll(1000)
        # for topic_partition, messages in polled_messages.items():
        #     for message in messages:
        #         # print("Receive message topic " + message.topic)
        #         producer.send(master_solvedShare_topic, message.value)

        # polled_messages = commonEvent_consumer.poll(1000)
        # for topic_partition, messages in polled_messages.items():
        #     for message in messages:
        #         print("Receive message topic " + message.topic)
        #         producer.send(master_commonEvent_topic, message.value)

        polled_messages = shareLog_consumer.poll(100)
        for topic_partition, messages in polled_messages.items():
            for message in messages:
                shareLog = deserialize_shareDevethBranch(message.value)
                if shareLog["status"] == 1798084231 or shareLog["status"] == 1422486894:
                    master_status = 1
                else:
                    master_status = 0

                master_fmt = 'QqIiQIIii'  # add last 4 bytes for padding
                master_message = pack(master_fmt
                        , shareLog["jobId"]
                        , shareLog["workerHashId"]
                        , shareLog["LegacyIP4"]
                        , shareLog["userId"]
                        , shareLog["shareDiff"]
                        , shareLog["timeStamp"]
                        , shareLog["blkBits"]
                        , master_status
                        , 0                        
                    )
                print("Receive message topic %s. Forwarding to current build container" % message.topic)
                # producer.send(master_shareLog_topic, message.value[8:56])
                producer.send(master_shareLog_topic, master_message)
        sys.stdout.flush()
        producer.flush()
        time.sleep(0.05)

    exit(0)

def main():
    sys.stdout = FileLogger("/root/work/poollogs/forwarder_output.txt")
    sys.stderr = sys.stdout

    print("Starting forward shares script")
    sys.stdout.flush()

    deveth_shareLog_topic = "ShareLog"
    master_shareLog_topic = "ShareLog"

    deveth_commonEvent_topic = "CommonEvents"
    master_commonEvent_topic = "CommonEvents"

    deveth_solvedShare_topic = "SolvedShare"
    master_solvedShare_topic = "SolvedShare"

    deveth_broker = "localhost:9092"
    master_broker = "otherkafkahost:9092" #Change this value

    deveth_broker = sys.argv[1]
    master_broker = sys.argv[2]

    print(str(sys.argv))

    # len_argv = len(sys.argv)
    # for i in range(0, len_argv):
    #     arg = sys.argv[i]
    #     if arg == "-d":
    #         deveth_shareLog_topic = sys.argv[i + 1]

    settings = {
        "deveth_shareLog_topic" : deveth_shareLog_topic,
        "master_shareLog_topic" : master_shareLog_topic,

        # "deveth_solvedShare_topic" : deveth_solvedShare_topic,
        # "master_solvedShare_topic" : master_solvedShare_topic,

        "deveth_commonEvent_topic" : deveth_commonEvent_topic,
        "master_commonEvent_topic" : master_commonEvent_topic,

        "deveth_broker" : deveth_broker,
        "master_broker" : master_broker
    }

    forward_topics(settings)

if __name__ == "__main__":
    main()