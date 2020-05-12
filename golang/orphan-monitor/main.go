package main

import (
	"os"
	"flag"
	"orphan-monitor/aec"
	"orphan-monitor/ltc"
	"orphan-monitor/ckb"
	"github.com/urfave/cli"
	"github.com/golang/glog"
)

func main() {
	_ = flag.CommandLine.Parse([]string{"-stderrthreshold=INFO", "-logtostderr"})
	defer glog.Flush()
	app := cli.NewApp()
	app.Name = "orphan-monitor"
	app.Usage = "check mined block is orphaned and update mysql"
	app.Commands = []cli.Command{
		aec.Command,
		ltc.Command,
		ckb.Command,
	}

	err := app.Run(os.Args)
	if err != nil {
		glog.Fatal(err)
	}
}
