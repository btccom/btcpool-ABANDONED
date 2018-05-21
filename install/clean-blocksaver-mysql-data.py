#!/usr/bin/python

import MySQLdb
import datetime
import sys
import time
import signal

host = ""
user = ""
passwd = ""
db = ""
table = "filedata"
retention_time=11
running = True

def signal_handler(signal, frame):
    global running    
    print('\nExiting!')
    running = False

def usage():
    print("Options: ")
    print("--host, -h. mysql server address")
    print("--user, -u. mysql server username")
    print("--password, -p. mysql server password")
    print("--database, -d. mysql server used database")
    print("--table, -t. database table name. default 'filedata'")
    print("--retention_time, -t. data retention_time")

def read_arg():
    global host, user, passwd, db, table, retention_time
    arg_len = len(sys.argv)
    for i in xrange(0, arg_len):
        arg = sys.argv[i]
        if(arg == "--host" or arg == "-h"):
            host = sys.argv[i + 1]
        elif(arg == "--user" or arg == "-u"):
            user = sys.argv[i + 1]
        elif(arg == "--password" or arg == "-p"):
            passwd = sys.argv[i + 1]
        elif(arg == "--database" or arg == "-d"):
            db = sys.argv[i + 1]
        elif(arg == "--table" or arg == "-t"):
            table = sys.argv[i + 1]
        elif(arg == "--retention_time" or arg == "-rt"):
            try:
                time = int(sys.argv[i + 1])
                if(time >= 1):
                    retention_time = time
                else:
                    print("minimum retention time is 1 hour. retention time used is " + str(retention_time)) + " hours"
            except ValueError:
                print("--retention_time is not an int")

def run():
    global host, user, passwd, db, table, retention_time

    signal.signal(signal.SIGINT, signal_handler)

    # try connect for the first time
    try:
        testConnectDb = MySQLdb.connect(host=host,    # your host, usually localhost
                            user=user,         # your username
                            passwd=passwd,  # your password
                            db=db)        # name of the data base

        if testConnectDb.open == False:
            print("Cannot connect to db. Command line: " + str(sys.argv))
            usage()
            exit(-1)

        testConnectDb.close()
    except MySQLdb.Error as e:
        print("Mysql connection error: " + str(e))
        usage()
        exit(-1)


    while(running):
        try:
            conn = MySQLdb.connect(   host=host,    # your host, usually localhost
                                    user=user,         # your username
                                    passwd=passwd,  # your password
                                    db=db)        # name of the data base
        
            if conn.open == False:
                print("Cannot connect to db. Command line: " + str(sys.argv))
 

            cur = conn.cursor()

            statement = "delete FROM %s where datetime < DATE_SUB(NOW(), INTERVAL %d HOUR)" % (table, retention_time)
            print "[%s] SQL: %s" % (str(datetime.datetime.today()), statement)

            cur.execute(statement)
            conn.commit()
            row_affected = cur.rowcount
            print "Affected row = " + str(row_affected)
            conn.close()

        except MySQLdb.Error as e:
            print("Mysql connection error: " + str(e))

        # print "wait for 60 seconds before next delete"
        count = 0
        while(count < 60 and running):
            time.sleep(1)
            count += 1

if __name__ == "__main__":
    read_arg()
    run()