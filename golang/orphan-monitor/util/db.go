package util

import (
	"database/sql"
	"sort"
	"strings"

	_ "github.com/go-sql-driver/mysql"
	"github.com/golang/glog"
)

type DBInfo struct {
	Host   string
	User   string
	Pass   string
	Dbname string
	Table  string
}

type DBConnection struct {
	DbHandle *sql.DB
	DbConfig DBInfo
}

func (handle *DBConnection) InitDB(conf DBInfo) {
	handle.DbConfig = conf
	path := strings.Join([]string{conf.User, ":", conf.Pass, "@tcp(", conf.Host, ")/", conf.Dbname, "?charset=utf8"}, "")
	glog.Info("dbpath : " + path)
	handle.DbHandle, _ = sql.Open("mysql", path)
	handle.DbHandle.SetConnMaxLifetime(100)
	handle.DbHandle.SetMaxIdleConns(10)
	
	if err := handle.DbHandle.Ping(); err != nil {
		glog.Info("opon database fail : %s", err)
		return
	}
	glog.Info("connnect success")
}

func (handle *DBConnection) GetLastestMinedHeight(lastupdatedheight int) (heights []int, height2HashMap map[int]string) {
	height2HashMap = make(map[int]string)
	sql := "select height, hash from " + handle.DbConfig.Table + " where height > ?;"

	stmt, err := handle.DbHandle.Prepare(sql)
	if err != nil {
		glog.Info("db prepare sql :", sql, " failed,reason : ", err)
		panic(err)
	}

	rows, err := stmt.Query(lastupdatedheight)
	if err != nil {
		glog.Info("db prepare sql :", sql, " failed,reason : ", err)
		panic(err)
	}

	for rows.Next() {
		var height int
		var blockhash string
		err = rows.Scan(&height, &blockhash)
		height2HashMap[height] = blockhash
		heights = append(heights, height)
	}
	sort.Sort(sort.IntSlice(heights))
	return heights, height2HashMap
}

func (handle *DBConnection) UpdateOrphanBlock(height int, isOrphan bool) (bool, error) {
	sql := "update " +handle.DbConfig.Table + " set is_orphaned = ? WHERE height = ?;"

	stmt, err := handle.DbHandle.Prepare(sql)
	if err != nil {
		glog.Info("db prepare sql :", sql, " failed,reason : ", err)
		return false, err
	}

	_, err = stmt.Exec(isOrphan, height)
	if err != nil {
		glog.Info("db prepare sql :", sql, " failed,reason : ", err)
		return false, err
	}

	return true, err
}
