package main

import (
	"flag"
	"os"

	"github.com/golang/glog"
	"github.com/urfave/cli"

	"github.com/btccom/btcpool/reservoir"
	"github.com/btccom/btcpool/reservoir/nodebridge/eth"
)

func main() {
	// TODO: Make glog flags working
	_ = flag.CommandLine.Parse([]string{"-stderrthreshold=INFO"})

	app := cli.NewApp()
	app.Name = "nodebridge"
	app.Usage = "Bridges between the mining pool and the blockchain node"
	app.Version = reservoir.Version
	app.Commands = []cli.Command{
		eth.Command,
	}

	err := app.Run(os.Args)
	if err != nil {
		glog.Exit(err)
	}
}
