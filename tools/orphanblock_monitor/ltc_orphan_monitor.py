#!/usr/bin/python
# -*- coding: UTF-8 -*-
import requests
import json
import pymysql
import logging
import time
import sys, getopt
from configparser import ConfigParser

class OrphanMontor:
    
    databasehandle = None

    def __init__(self, path):
        self._path = path
        self.config = ConfigParser()
        self.config.read(path, encoding='UTF-8')
        self.lastheight = 0
        handler = logging.FileHandler(self.config["LOG"]["path"])
        handler.setLevel(logging.INFO)
        logging.basicConfig(level = logging.INFO,format = '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
        self.logger = logging.getLogger(__name__)
        console = logging.StreamHandler()
        console.setLevel(logging.INFO)
        self.logger.addHandler(handler)
        self.logger.addHandler(console)

    def create_mysql_handle(self):
        try:
            self.databasehandle = pymysql.connect(host=self.config["MYSQL"]["hostname"], user=self.config["MYSQL"]["username"], passwd=self.config["MYSQL"]["password"], db=self.config["MYSQL"]["database"], charset='utf8')
            if self.databasehandle is not None:
                return self.databasehandle.cursor()
        except Exception as err:
            self.logger.error("connect mysql Error %s  " % (err))

    def update_orphan(self, height, is_orphaned):
        try:
            sql = "UPDATE %s SET is_orphaned = %d WHERE height = %d;" %(self.config["MYSQL"]["table"], is_orphaned, height)
            self.cursor = self.create_mysql_handle()
            self.cursor.execute(sql)
            self.databasehandle.commit()
        except Exception as err:
            self.logger.warning("execute sql Error %s  " % (err))
            self.cursor = create_mysql_handle()
        self.cursor.close() 

    def get_lattest_heights(self, lastupdatedheight):
        sql = "select height , hash from %s where height > %d;" %(self.config["MYSQL"]["table"], lastupdatedheight)
        heights = []
        height2hash = {}
        try:
            self.cursor = self.create_mysql_handle()
            self.cursor.execute(sql)
            rows = self.cursor.fetchall()
            for row in rows:
                heights.append(row[0])
                height2hash[row[0]] = row[1]
            heights.sort()
        except Exception as err:
            self.logger.warning("execute sql error %s  " % (err))
        if len(heights) > 0:
            self.lastheight = heights[-1]
        self.logger.info("current mysql hight : %d  " % (self.lastheight))
        self.cursor.close() 
        return heights, height2hash

    def get_chain_tip(self):
        try:
            node_url = self.config["NODE"]["url"]
            payload = {"jsonrpc": "2.0", "id":"1", "method": "getblockcount", "params": [] }
            headers = {'Content-type': 'application/json'}
            r = requests.post(node_url, data=json.dumps(payload), headers=headers, auth=( self.config["NODE"]["rpcusr"], self.config["NODE"]["rpcpwd"]))
            if r.status_code == 200 and 'result'  in r.json() and r.json()['result'] is not None:
                return int(r.json()['result'])
            return None
        except requests.RequestException as e:
            self.logger.error("get chain tip Error %s  " % (e))
            return None
    
    def wait_untill_height(self, height):
        try:
            while True:
                chainTip = self.get_chain_tip()
                if chainTip is not None and chainTip >= height:
                    self.logger.info("current tip :%d , mining height : %d" % (chainTip, height))
                    return True
                else:
                    time.sleep(1)
        except Exception as e:
            self.logger.error("wait until height error :%s  " % (e))
            return False
        

    def get_block_hash(self, height):
        try:

            node_url = self.config["NODE"]["url"]
            payload = {"jsonrpc": "2.0", "id":"1", "method": "getblockhash", "params": [height] }
            headers = {'Content-type': 'application/json'}
            r = requests.post(node_url, data=json.dumps(payload), headers=headers, auth=( self.config["NODE"]["rpcusr"], self.config["NODE"]["rpcpwd"]))
            if r.status_code == 200  and  r.json()['error'] is None:
                return r.json()['result']
            return None
        except requests.RequestException as e:
            self.logger.error("get block hash error : %s  " % (e))
            return None
        
    def run(self):
        while True:
            heights, height2hash = self.get_lattest_heights(self.lastheight)
            for height in heights:
                self.wait_untill_height(height)
                curblockhash = self.get_block_hash(height)
                if height2hash[height] != curblockhash :
                    is_orphaned = height2hash[height] != curblockhash
                    self.update_orphan(height, is_orphaned)
                    self.logger.info("update height: %d hash : %s minedhash : %s" % (height, blockhash, height2hash[height]))
            time.sleep(5)

if __name__ == "__main__":
    try:
        opts, args = getopt.getopt(sys.argv[1:],'-h-c:-v',['help','configure=','version'])    
        for opt, opt_value in opts:
            if opt == '-h':
                print ('python orphan_monitor.py -c <configure>')
                sys.exit()
            elif opt in ("-c", "--configure"):
                configurefilepath = opt_value
                handle = OrphanMontor(configurefilepath)
                handle.run()
            elif opt in ("-v", "--version"):
                print("[*] Version is 0.01 ")
                sys.exit()
    except getopt.GetoptError:
        print('python ae_orphan_monitor.py -c <configure>')
        sys.exit(2)

