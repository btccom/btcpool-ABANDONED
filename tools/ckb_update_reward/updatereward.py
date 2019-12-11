#!/usr/bin/python
# -*- coding: UTF-8 -*-
import requests
import json
import MySQLdb
import logging
import time
from configparser import ConfigParser

class RewardUpdate:
    
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
            self.databasehandle = MySQLdb.connect(host=self.config["MYSQL"]["hostname"], user=self.config["MYSQL"]["username"], passwd=self.config["MYSQL"]["password"], db=self.config["MYSQL"]["database"], charset='utf8')
            if self.databasehandle is not None:
                return self.databasehandle.cursor()
        except Exception as err:
            self.logger.error("connect mysql Error %s  " % (err))
    
    def update_reward(self, height, reward, is_orphaned):
        reward = reward if not is_orphaned else 0
        try:
            sql = "UPDATE %s SET rewards = %d , is_orphaned = %d WHERE height = %d;" %(self.config["MYSQL"]["table"], reward, is_orphaned, height)
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
        data = json.dumps({"id": 2, "jsonrpc": "2.0", "method":"get_tip_block_number","params": []})
        headers = {'content-type':'application/json'}
        try:
            r = requests.post(url= self.config["NODE"]["url"], data=data, headers = headers)
            if r.status_code == 200 and 'error' not in r.json():
                return int(r.json()['result'], 16)
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

    def get_block_reward(self, blockhash):
        data = '{ "id" : 2, "jsonrpc" : "2.0","method":"get_cellbase_output_capacity_details", "params" : ["%s"]}'%(blockhash)
        headers = {'content-type':'application/json'}
        try:
            r = requests.post(url=self.config["NODE"]["url"], data=data, headers = headers)
            if r.status_code == 200 and 'error' not in r.json():
                return int(r.json()['result']['total'], 16)
            return None
        except requests.RequestException as e:
            self.logger.error("get block reward error : %s  " % (e))
            return None
        

    def get_block_hash(self, height):
        data = '{ "id" : 2, "jsonrpc" : "2.0","method":"get_header_by_number", "params" : ["0x%x"]}' %(height)
        headers = {'content-type':'application/json'}
        try:
            r = requests.post(url=self.config["NODE"]["url"], data=data, headers = headers)
            if r.status_code == 200 and 'error' not in r.json():
                return r.json()['result']['hash']
            return None
        except requests.RequestException as e:
            self.logger.error("get block hash error : %s  " % (e))
            return None
        
    def run(self):
        while True:
            heights, height2hash = self.get_lattest_heights(self.lastheight)
            for height in heights:
                self.wait_untill_height(height + 11)
                curblockhash = self.get_block_hash(height)
                blockhash = self.get_block_hash(height + 11)
                reward = self.get_block_reward(blockhash)
                if blockhash is None or reward is None:
                    heights.append(height)
                else:
                    is_orphaned = (height2hash[height] != curblockhash)
                    self.update_reward(height, reward, is_orphaned)
                self.logger.info("update height: %d reeward : %d hash : %s  " % (height, reward, blockhash))
            time.sleep(5)


if __name__ == "__main__":
    handle = RewardUpdate("./updatereward.config")
    handle.run()

