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


def create_kafka_consumer(topics, server, consumer_timeout = 12000):
    consumer = KafkaConsumer(bootstrap_servers=server, consumer_timeout_ms=consumer_timeout)
    tp = []
    for topic in topics:
        tp.append(TopicPartition(topic, 0))
    consumer.assign(tp)    
    return consumer


def forward_topics(settings):
    deveth_shareLog_topic = settings["deveth_shareLog_topic"]
    master_shareLog_topic = settings["master_shareLog_topic"]
    deveth_solvedShare_topic = settings["deveth_solvedShare_topic"]
    master_solvedShare_topic = settings["master_solvedShare_topic"]

    deveth_broker = settings["deveth_broker"]
    master_broker = settings["master_broker"]

    shareLog_consumer = create_kafka_consumer([deveth_shareLog_topic], deveth_broker)
    solvedShare_consumer = create_kafka_consumer([deveth_solvedShare_topic], deveth_broker)
    producer = KafkaProducer(bootstrap_servers=[master_broker])

    while True:
        # print("FORWARD TOPICS")
        polled_messages = solvedShare_consumer.poll(1000)
        for topic_partition, messages in polled_messages.items():
            for message in messages:
                # print("Receive message topic " + message.topic)
                producer.send(master_solvedShare_topic, message.value)

        polled_messages = shareLog_consumer.poll(100)
        for topic_partition, messages in polled_messages.items():
            for message in messages:
                # print("Receive message topic " + message.topic)
                producer.send(master_shareLog_topic, message.value[8:56])
        # sys.stdout.flush()
        time.sleep(0.05)

    exit(0)

def main():
    deveth_shareLog_topic = "ShareLog"
    master_shareLog_topic = "ShareLog"

    deveth_solvedShare_topic = "SolvedShare"
    master_solvedShare_topic = "SolvedShare"

    deveth_broker = "localhost:9092"
    master_broker = "otherkafkahost:9092" #Change this value

    # len_argv = len(sys.argv)
    # for i in range(0, len_argv):
    #     arg = sys.argv[i]
    #     if arg == "-d":
    #         deveth_shareLog_topic = sys.argv[i + 1]

    settings = {
        "deveth_shareLog_topic" : deveth_shareLog_topic,
        "master_shareLog_topic" : master_shareLog_topic,

        "deveth_solvedShare_topic" : deveth_solvedShare_topic,
        "master_solvedShare_topic" : master_solvedShare_topic,

        "deveth_broker" : deveth_broker,
        "master_broker" : master_broker
    }

    forward_topics(settings)

if __name__ == "__main__":
    main()